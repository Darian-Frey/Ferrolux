// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
#include "core/Engine.h"

#include "core/StreamTimer.h"

#include <QFileInfo>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

#include <gst/gst.h>

Q_LOGGING_CATEGORY(lcCore, "ferrolux.core")

namespace ferrolux::core {
namespace {

// Whether a local file can actually be opened. Existence is not enough — a file
// present but unreadable fails in the same place and for the same reason.
bool readable(const QUrl &url)
{
    const QFileInfo info(url.toLocalFile());
    return info.exists() && info.isFile() && info.isReadable();
}

// How long a failure stays on the panel once playback has moved on. Long enough
// to read one line, short enough that it is gone before it could describe the
// wrong track. F-001, BUG-025.
constexpr int kErrorHoldMs = 6000;

constexpr qint64 kPreviousTrackWindowNs = 3'000'000'000; // F-002: three seconds

// audiomixmatrix takes an array of out-channel rows, each an array of
// in-channel coefficients. A diagonal matrix is therefore a per-channel gain,
// which is exactly what a balance control is.
void setDiagonalMatrix(GstElement *element, double left, double right)
{
    GValue matrix = G_VALUE_INIT;
    g_value_init(&matrix, GST_TYPE_ARRAY);

    const double gains[2] = { left, right };
    for (int out = 0; out < 2; ++out) {
        GValue row = G_VALUE_INIT;
        g_value_init(&row, GST_TYPE_ARRAY);
        for (int in = 0; in < 2; ++in) {
            GValue cell = G_VALUE_INIT;
            g_value_init(&cell, G_TYPE_DOUBLE);
            g_value_set_double(&cell, out == in ? gains[out] : 0.0);
            gst_value_array_append_value(&row, &cell);
            g_value_unset(&cell);
        }
        gst_value_array_append_value(&matrix, &row);
        g_value_unset(&row);
    }

    g_object_set_property(G_OBJECT(element), "matrix", &matrix);
    g_value_unset(&matrix);
}

} // namespace

// Bus watch. Runs on the main loop, not on a streaming thread — see AV-001.
int busDispatch(GstBus *, GstMessage *message, void *data)
{
    static_cast<Engine *>(data)->handleMessage(message);
    return TRUE; // stay installed
}

// Gapless handover. THIS RUNS ON A STREAMING THREAD (AV-001), and is the only
// code in Ferrolux that does. It is deliberately three operations: take a leaf
// mutex, copy a pre-encoded URI, set one property. No allocation of application
// objects, no signal emission, no query of the playlist model — which lives on
// the main thread and must not be reached from here (AV-006).
//
// The application is told about the handover later, from the bus handler, when
// GST_MESSAGE_STREAM_START arrives on the main loop.
void aboutToFinish(GstElement *playbin, void *data)
{
    Engine *engine = static_cast<Engine *>(data);

    // AV-001, and the two halves are timed apart because they answer to
    // different people. Everything up to the handover is ours; the handover
    // itself is a property write that `playbin3` chooses how to service, and it
    // is the whole mechanism gapless playback works by. Timing them together
    // reports a number nobody can act on — the first run of
    // `tools/stress-audio.sh` did exactly that, showing 2.4 ms for a callback
    // whose own work is a mutex and a refcount.
    QByteArray uri;
    {
        const StreamScope ours(engine->m_streamTimer);

        {
            QMutexLocker locker(&engine->m_nextMutex);
            uri = engine->m_nextUri;
            if (!uri.isEmpty())
                engine->m_handoverUri = uri;
        }

        if (!uri.isEmpty())
            engine->m_handoverPending.storeRelease(1);
    }

    if (uri.isEmpty())
        return; // end of the playlist: let it finish and post EOS

    {
        const StreamScope handover(engine->m_handoverTimer);
        g_object_set(playbin, "uri", uri.constData(), nullptr);
    }
}

Engine::Engine(QObject *parent)
    : QObject(parent)
{
    buildPipeline();
}

Engine::~Engine()
{
    teardownPipeline();
}

void Engine::buildPipeline()
{
    m_pipeline = gst_element_factory_make("playbin3", "ferrolux-playbin");
    if (!m_pipeline) {
        fail(QStringLiteral("playbin3 is unavailable. Install the GStreamer base plugin set."));
        return;
    }

    if (GstElement *filter = buildAudioFilter())
        g_object_set(m_pipeline, "audio-filter", filter, nullptr);

    GstElement *sink = gst_element_factory_make("autoaudiosink", "ferrolux-sink");
    if (sink)
        g_object_set(m_pipeline, "audio-sink", sink, nullptr);

    g_signal_connect(m_pipeline, "about-to-finish", G_CALLBACK(aboutToFinish), this);

    GstBus *bus = gst_element_get_bus(m_pipeline);
    m_busWatch = gst_bus_add_watch(bus, busDispatch, this);
    gst_object_unref(bus);

    applyVolume();
    applyBalance();
}

// Phase 1 carries only what F-004 needs. The equaliser (Phase 3) is inserted
// *before* the balance element and the level and spectrum analysis elements
// (Phase 4) *after* it, giving the chain in SPEC.md §Pipeline. That ordering is
// deliberate: the meters show the signal as heard, so the listening adjustment
// has to be upstream of them. See BUG-002.
//
// Balance is implemented with audiomixmatrix rather than audiopanorama because a
// diagonal mix matrix is exactly a per-channel gain. Neither audiopanorama mode
// applies the specified law: "simple" scales one channel and "psychoacoustic"
// applies a model of its own.
GstElement *Engine::buildAudioFilter()
{
    GstElement *bin = gst_bin_new("ferrolux-audio-filter");
    GstElement *convertIn = gst_element_factory_make("audioconvert", "fx-convert-in");
    GstElement *stereo = gst_element_factory_make("capsfilter", "fx-stereo");
    GstElement *matrix = gst_element_factory_make("audiomixmatrix", "fx-balance");
    GstElement *convertOut = gst_element_factory_make("audioconvert", "fx-convert-out");

    // SPEC.md §Pipeline: both analysis elements are pass-through and sit last,
    // after the equaliser and the balance, so the meters show the signal as
    // heard rather than as decoded.
    GstElement *level = gst_element_factory_make("level", "fx-level");
    GstElement *spectrum = gst_element_factory_make("spectrum", "fx-spectrum");
    if (level) {
        g_object_set(level,
                     "post-messages", TRUE,
                     "interval", guint64(16000000),      // ~one per frame at 60 fps
                     "peak-ttl", guint64(1500000000),
                     "peak-falloff", 20.0,
                     nullptr);
    }
    if (spectrum) {
        g_object_set(spectrum,
                     "post-messages", TRUE,
                     "bands", guint(512),                // analysis resolution, not display
                     "interval", guint64(16000000),
                     "threshold", gint(-80),
                     "multi-channel", FALSE,
                     nullptr);
    }
    m_levelElement = level;
    m_spectrumElement = spectrum;

    const bool haveEqualiser = m_equaliser.createElements();
    if (!haveEqualiser)
        qCWarning(lcCore) << "equaliser unavailable; the filter chain will be flat";

    if (!bin || !convertIn || !stereo || !matrix || !convertOut) {
        qCWarning(lcCore) << "audio filter unavailable; balance will be inoperative";
        if (bin) gst_object_unref(bin);
        if (convertIn) gst_object_unref(convertIn);
        if (stereo) gst_object_unref(stereo);
        if (matrix) gst_object_unref(matrix);
        if (convertOut) gst_object_unref(convertOut);
        return nullptr;
    }

    // Two constraints, both load-bearing, pinned in one place.
    //
    // Channels: audiomixmatrix declares channels [1, MAX] on its pads, so
    // nothing else in the chain pins the count and a mono source would
    // negotiate one channel against an element configured for two — which fails
    // at set_caps with "Erroneous matrix detected" rather than at link time.
    // Forcing stereo makes audioconvert up-mix mono and guarantees a match.
    //
    // Format: without this the chain negotiates S16LE for a 16-bit source, and
    // the equaliser then runs ten cascaded IIR biquads in 16-bit integer,
    // rounding after every section. The result is audibly gritty on real music.
    // Caps negotiation propagates upstream, so constraining here puts the whole
    // filter chain — preamp, bands and balance — into float, and the trailing
    // audioconvert returns to whatever the sink wants. See BUG-007.
    GstCaps *caps = gst_caps_new_simple("audio/x-raw",
                                        "format", G_TYPE_STRING, "F32LE",
                                        "channels", G_TYPE_INT, 2, nullptr);
    g_object_set(stereo, "caps", caps, nullptr);
    gst_caps_unref(caps);

    g_object_set(matrix, "in-channels", 2, "out-channels", 2, nullptr);

    // The matrix must carry correctly sized rows before the link is attempted:
    // audiomixmatrix refuses to link at all while its matrix is empty.
    m_balanceElement = matrix;
    applyBalance();

    // SPEC.md §Pipeline order: preamp, then the band filters, then balance,
    // then (from Phase 4) the analysis elements. Everything that changes what
    // is heard sits upstream of everything that measures it.
    bool linked = false;
    GstElement *tail = matrix;
    if (haveEqualiser) {
        GstElement *preamp = m_equaliser.preampElement();
        GstElement *bands = m_equaliser.filterElement();
        gst_bin_add_many(GST_BIN(bin), convertIn, preamp, bands, stereo, matrix, nullptr);
        linked = gst_element_link_many(convertIn, preamp, bands, stereo, matrix, nullptr);
    } else {
        gst_bin_add_many(GST_BIN(bin), convertIn, stereo, matrix, nullptr);
        linked = gst_element_link_many(convertIn, stereo, matrix, nullptr);
    }

    if (linked && level && spectrum) {
        gst_bin_add_many(GST_BIN(bin), level, spectrum, nullptr);
        linked = gst_element_link_many(tail, level, spectrum, nullptr);
        tail = spectrum;
    } else if (level || spectrum) {
        qCWarning(lcCore) << "analysis elements unavailable; the meters will be blank";
        if (level) gst_object_unref(level);
        if (spectrum) gst_object_unref(spectrum);
        m_levelElement = nullptr;
        m_spectrumElement = nullptr;
    }

    if (linked) {
        gst_bin_add(GST_BIN(bin), convertOut);
        linked = gst_element_link(tail, convertOut);
    }

    if (!linked) {
        qCWarning(lcCore) << "could not link the audio filter chain";
        m_balanceElement = nullptr;
        gst_object_unref(bin);
        return nullptr;
    }

    // Kept so the negotiated input format can be read back. The bin's sink is
    // upstream of the capsfilter that pins stereo F32LE, so it carries the
    // stream's own rate and channel count; anything downstream of that filter
    // would report the format we imposed rather than the one we were given.
    m_filterBin = bin;

    GstPad *sinkPad = gst_element_get_static_pad(convertIn, "sink");
    gst_element_add_pad(bin, gst_ghost_pad_new("sink", sinkPad));
    gst_object_unref(sinkPad);

    GstPad *srcPad = gst_element_get_static_pad(convertOut, "src");
    gst_element_add_pad(bin, gst_ghost_pad_new("src", srcPad));
    gst_object_unref(srcPad);

    return bin; // matrix is owned by the bin, which is owned by playbin3
}

void Engine::teardownPipeline()
{
    if (!m_pipeline)
        return;

    if (m_busWatch) {
        g_source_remove(m_busWatch);
        m_busWatch = 0;
    }

    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    // Blocks until the state change completes, so that the twenty stop-start
    // cycles in the Phase 1 acceptance criterion cannot leave a pipeline
    // half-torn-down behind them.
    gst_element_get_state(m_pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);

    gst_object_unref(m_pipeline);
    m_pipeline = nullptr;
    m_balanceElement = nullptr;
}

void Engine::setSource(const QUrl &url)
{
    if (!m_pipeline)
        return;

    // A source change always returns to NULL first: playbin3 will not accept a
    // new uri while it is playing, and going through NULL is what makes Error
    // recoverable.
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    gst_element_get_state(m_pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);

    m_source = url;
    m_position = 0;
    m_duration = -1;
    m_seekable = false;
    m_playRequested = false;
    m_handoverPending.storeRelease(0);
    m_analysisRate = 0;

    // Cleared with the rest of the per-stream state. Tags are not re-sent for a
    // track that has none, so a stream without a codec tag would otherwise
    // report the previous track's — which is the kind of wrong that looks
    // entirely plausible.
    m_codec.clear();
    m_bitrateKbps = 0;
    refreshStreamFormat();

    // **The error text is deliberately not cleared here.** Loading the next
    // source is exactly what happens when a broken one is stepped over, and
    // clearing it at that moment would wipe the message off the panel in the
    // same instant it became true — F-001 asks for a visible error *and* an
    // advance, and doing both meant the error surviving the advance. It is
    // cleared when something actually plays instead; see `setState`.

    g_object_set(m_pipeline, "uri", url.toString().toUtf8().constData(), nullptr);

    emit sourceChanged();
    emit positionChanged();
    emit durationChanged();
    emit seekableChanged();

    setState(Loading);
    gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    qCInfo(lcCore) << "loading" << url.toString();
}

void Engine::setNextSource(const QUrl &url)
{
    // **A next source that cannot be opened is not handed over.** `playbin3`
    // reports a failure to prepare the next URI on the same bus as a failure of
    // the one playing, and there is nothing in the message to say which it was —
    // so the track being listened to is ended by a fault in a file nobody has
    // reached yet. Measured before this check existed: a 6.97 s file whose next
    // entry was missing stopped at about 5.3 s, losing the last second and a
    // half to a handover that could never have worked. BUG-027.
    //
    // Declining the handover costs nothing. The stream finishes normally, posts
    // EOS, and the playlist advances onto the unusable entry in the ordinary
    // way — where it fails as the *current* source, is reported, and is stepped
    // over by BUG-025's handling. The file is skipped either way; this is about
    // not damaging the track before it.
    //
    // Checked here rather than in `aboutToFinish`, which runs on a streaming
    // thread and may not touch the filesystem (AV-001). This runs on the main
    // thread, once per track change, and is one `stat`.
    const bool usable = !url.isEmpty() && (!url.isLocalFile() || readable(url));

    const QByteArray encoded = usable ? url.toString().toUtf8() : QByteArray();
    QMutexLocker locker(&m_nextMutex);
    m_nextUri = encoded;
}

void Engine::play()
{
    if (!m_pipeline || m_source.isEmpty())
        return;

    // Asking to play something that has already failed is itself a failed
    // attempt to play it, and saying so is the only way anything can react.
    // Without this the source that could not be loaded is silently retried: the
    // state goes back to `Loading`, the retry produces no new error because
    // nothing new is attempted, and the player sits in `Loading` for ever —
    // which MPRIS reports as playing, with a position that never moves. That
    // was the visible half of BUG-025.
    //
    // A file that failed to load will fail again, so nothing is lost by not
    // retrying it. What is gained is that the playlist can move past it.
    if (m_state == Error) {
        emit sourceFailed(m_source, m_errorText, true);
        return;
    }

    m_playRequested = true;

    // Playing from Stopped means the pipeline is in READY and has to preroll
    // again, so it re-enters Loading and reaches Playing on ASYNC_DONE. Without
    // this the state would stay Stopped while audio was audibly running.
    if (m_state == Stopped)
        setState(Loading);

    if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        fail(QStringLiteral("The pipeline could not be started."));
        return;
    }
    if (m_state == Paused)
        setState(Playing);
}

void Engine::pause()
{
    if (!m_pipeline || m_state != Playing)
        return;

    if (gst_element_set_state(m_pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        fail(QStringLiteral("The pipeline could not be paused."));
        return;
    }
    m_playRequested = false;
    setState(Paused);
}

void Engine::stop()
{
    if (!m_pipeline)
        return;

    gst_element_set_state(m_pipeline, GST_STATE_READY);
    m_playRequested = false;

    // F-002: stop resets position to zero, pause preserves it.
    if (m_position != 0) {
        m_position = 0;
        emit positionChanged();
    }
    setState(Stopped);
}

void Engine::seek(qint64 positionNs)
{
    if (!m_pipeline || !m_seekable)
        return;

    positionNs = qBound<qint64>(0, positionNs, m_duration > 0 ? m_duration : positionNs);

    const auto flags = GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);
    if (!gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME, flags, positionNs)) {
        qCWarning(lcCore) << "seek to" << positionNs << "was refused";
        return;
    }

    // Reflect the target immediately, so the position bar does not snap back to
    // the old value for the frames before the flush completes.
    m_position = positionNs;
    emit positionChanged();
    emit seeked(positionNs);
}

void Engine::previous()
{
    if (m_position > kPreviousTrackWindowNs) {
        seek(0);
        return;
    }
    emit previousTrackRequested();
}

void Engine::setVolume(double taperPosition)
{
    taperPosition = qBound(0.0, taperPosition, 1.0);
    if (qFuzzyCompare(taperPosition, m_volume))
        return;

    m_volume = taperPosition;
    applyVolume();
    emit volumeChanged();
}

void Engine::setBalance(double balance)
{
    balance = qBound(-1.0, balance, 1.0);
    if (qFuzzyCompare(balance + 2.0, m_balance + 2.0))
        return;

    m_balance = balance;
    applyBalance();
    emit balanceChanged();
}

// SPEC.md §Volume taper: displayed volume maps to amplitude as a = v³.
double Engine::volumeAmplitude(double taperPosition)
{
    const double v = qBound(0.0, taperPosition, 1.0);
    return v * v * v;
}

// SPEC.md §Volume taper: attenuate-only balance.
//   left  = min(1, 1 − b)
//   right = min(1, 1 + b)
// Centre is unity on both channels and neither gain ever exceeds unity, so the
// control cannot contribute to clipping under AV-003. The constant-power
// cos/sin law this replaced is the law for panning a mono source; applied to an
// already-stereo signal it attenuated centred playback by 3 dB and made a
// channel louder when the control moved off centre. See BUG-003.
void Engine::balanceGains(double balance, double &left, double &right)
{
    const double b = qBound(-1.0, balance, 1.0);
    left = std::min(1.0, 1.0 - b);
    right = std::min(1.0, 1.0 + b);
}

void Engine::applyVolume()
{
    if (!m_pipeline)
        return;
    g_object_set(m_pipeline, "volume", volumeAmplitude(m_volume), nullptr);
}

void Engine::applyBalance()
{
    if (!m_balanceElement)
        return;
    double left = 1.0;
    double right = 1.0;
    balanceGains(m_balance, left, right);
    setDiagonalMatrix(m_balanceElement, left, right);
}

void Engine::poll()
{
    if (!m_pipeline || (m_state != Playing && m_state != Paused))
        return;

    gint64 value = 0;

    if (gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &value)) {
        if (value != m_position) {
            m_position = value;
            emit positionChanged();
        }
    }

    if (gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &value)) {
        if (value != m_duration) {
            m_duration = value;
            emit durationChanged();
        }
    }
}

qint64 Engine::runningTime() const
{
    if (!m_pipeline)
        return -1;
    const GstClockTime now = gst_element_get_current_running_time(m_pipeline);
    return GST_CLOCK_TIME_IS_VALID(now) ? qint64(now) : -1;
}

void Engine::refreshSeekable()
{
    if (!m_pipeline)
        return;

    GstQuery *query = gst_query_new_seeking(GST_FORMAT_TIME);
    gboolean seekable = FALSE;
    if (gst_element_query(m_pipeline, query))
        gst_query_parse_seeking(query, nullptr, &seekable, nullptr, nullptr);
    gst_query_unref(query);

    if (bool(seekable) != m_seekable) {
        m_seekable = seekable;
        emit seekableChanged();
    }
}

// Parses the two analysis messages into plain values. Runs on the main loop, so
// nothing here is on a streaming thread — see AV-001.
void Engine::handleAnalysisMessage(GstMessage *message)
{
    const GstStructure *structure = gst_message_get_structure(message);
    if (!structure)
        return;
    const gchar *name = gst_structure_get_name(structure);

    if (g_strcmp0(name, "level") == 0) {
        // `level` carries its per-channel figures in a GValueArray — GLib's
        // deprecated boxed type — not a GstValueArray, and not a GstValueList
        // as `spectrum` uses for its magnitudes. Three similarly named
        // container types, three different accessors, and reaching for the
        // wrong one returns empty rather than failing. See BUG-011.
        const auto readArray = [structure](const char *field) {
            QList<double> values;
            const GValue *boxed = gst_structure_get_value(structure, field);
            if (!boxed || !G_VALUE_HOLDS(boxed, G_TYPE_VALUE_ARRAY))
                return values;

            const auto *array = static_cast<const GValueArray *>(g_value_get_boxed(boxed));
            if (!array)
                return values;

            values.reserve(int(array->n_values));
            for (guint i = 0; i < array->n_values; ++i) {
                values.append(g_value_get_double(
                    g_value_array_get_nth(const_cast<GValueArray *>(array), i)));
            }
            return values;
        };
        guint64 runningTime = 0;
        gst_structure_get_clock_time(structure, "running-time", &runningTime);
        emit levelMeasured(readArray("rms"), readArray("peak"), readArray("decay"),
                           qint64(runningTime));
        return;
    }

    if (g_strcmp0(name, "spectrum") == 0) {
        // Magnitude arrives as a GstValueList, not a GstValueArray. They are
        // different types with different accessors, and reaching for the wrong
        // one yields an empty result rather than an error.
        const GValue *list = gst_structure_get_value(structure, "magnitude");
        if (!list || !GST_VALUE_HOLDS_LIST(list))
            return;

        if (m_analysisRate <= 0 && m_spectrumElement) {
            if (GstPad *pad = gst_element_get_static_pad(m_spectrumElement, "sink")) {
                if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
                    if (GstStructure *audio = gst_caps_get_structure(caps, 0))
                        gst_structure_get_int(audio, "rate", &m_analysisRate);
                    gst_caps_unref(caps);
                }
                gst_object_unref(pad);
            }
        }
        if (m_analysisRate <= 0)
            return; // without the rate the frequency mapping is meaningless

        const guint count = gst_value_list_get_size(list);
        QList<float> magnitudes;
        magnitudes.reserve(int(count));
        for (guint i = 0; i < count; ++i)
            magnitudes.append(g_value_get_float(gst_value_list_get_value(list, i)));

        guint64 runningTime = 0;
        gst_structure_get_clock_time(structure, "running-time", &runningTime);
        emit spectrumMeasured(magnitudes, m_analysisRate, qint64(runningTime));
    }
}

// One line describing the stream, from three sources that report at three
// different moments: the sample rate from the spectrum element's caps, the
// channel count from the filter bin's own sink pad, and the codec and bitrate
// from tags on the bus. None of them is guaranteed to have arrived, so this
// prints what is known and leaves out what is not rather than waiting for a
// complete set that some streams never provide.
//
// The channel count is read at the bin's sink, upstream of the capsfilter that
// pins stereo. Downstream of it every stream is stereo by construction, so a
// readout taken there would report our own imposition back to us and call a
// mono recording stereo.
void Engine::refreshStreamFormat()
{
    QStringList parts;

    if (m_analysisRate > 0) {
        const double kHz = m_analysisRate / 1000.0;
        parts << (qFuzzyCompare(kHz, qRound(kHz))
                      ? QStringLiteral("%1 kHz").arg(qRound(kHz))
                      : QStringLiteral("%1 kHz").arg(kHz, 0, 'f', 1));
    }

    if (m_filterBin) {
        if (GstPad *pad = gst_element_get_static_pad(m_filterBin, "sink")) {
            if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
                gint channels = 0;
                const GstStructure *structure = gst_caps_get_structure(caps, 0);
                if (structure && gst_structure_get_int(structure, "channels", &channels)) {
                    parts << (channels == 1   ? QStringLiteral("mono")
                              : channels == 2 ? QStringLiteral("stereo")
                                              : QStringLiteral("%1 ch").arg(channels));
                }
                gst_caps_unref(caps);
            }
            gst_object_unref(pad);
        }
    }

    if (!m_codec.isEmpty())
        parts << m_codec;
    if (m_bitrateKbps > 0)
        parts << QStringLiteral("%1 kbps").arg(m_bitrateKbps);

    const QString line = parts.join(QStringLiteral("  ·  "));
    if (line == m_streamFormat)
        return;
    m_streamFormat = line;
    emit streamFormatChanged();
}

void Engine::handleMessage(GstMessage *message)
{
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ELEMENT:
        handleAnalysisMessage(message);
        break;

    case GST_MESSAGE_ERROR: {
        GError *error = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        qCWarning(lcCore) << "error from" << GST_OBJECT_NAME(message->src)
                          << ":" << error->message << "|" << (debug ? debug : "");
        fail(QString::fromUtf8(error->message));
        g_clear_error(&error);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_EOS:
        qCInfo(lcCore) << "end of stream";
        stop();
        emit endOfStream();
        break;

    case GST_MESSAGE_TAG: {
        // Codec and bitrate come from the demuxer as tags rather than from
        // caps, and they arrive whenever it gets to them — often after the
        // stream is already playing, and sometimes more than once.
        GstTagList *tags = nullptr;
        gst_message_parse_tag(message, &tags);
        if (tags) {
            gchar *codec = nullptr;
            if (gst_tag_list_get_string(tags, GST_TAG_AUDIO_CODEC, &codec) && codec) {
                m_codec = QString::fromUtf8(codec);
                g_free(codec);
            }

            // Nominal is the one a variable-bitrate stream declares; the plain
            // bitrate on such a stream is whatever the last frame happened to
            // be, which would make the readout flicker.
            guint bitrate = 0;
            if (gst_tag_list_get_uint(tags, GST_TAG_NOMINAL_BITRATE, &bitrate)
                || gst_tag_list_get_uint(tags, GST_TAG_BITRATE, &bitrate)) {
                if (bitrate > 0)
                    m_bitrateKbps = int(bitrate / 1000);
            }

            gst_tag_list_unref(tags);
            refreshStreamFormat();
        }
        break;
    }

    case GST_MESSAGE_ASYNC_DONE:
        // Preroll complete: duration, seekability and the negotiated caps are
        // all answerable now.
        refreshSeekable();
        refreshStreamFormat();
        if (m_state == Loading)
            setState(m_playRequested ? Playing : Paused);
        break;

    case GST_MESSAGE_DURATION_CHANGED:
        m_duration = -1; // forces the next poll() to re-query
        break;

    case GST_MESSAGE_STREAM_START:
        // Distinguishes a gapless handover from an ordinary first start: only
        // the about-to-finish path arms the flag, and consuming it here means a
        // normal setSource() cannot be mistaken for one.
        if (m_handoverPending.fetchAndStoreOrdered(0) == 1) {
            QByteArray handedTo;
            {
                QMutexLocker locker(&m_nextMutex);
                handedTo = m_handoverUri;
            }
            if (!handedTo.isEmpty()) {
                m_source = QUrl(QString::fromUtf8(handedTo));
                emit sourceChanged();
            }

            // A gapless handover never goes through setSource(), so the
            // per-stream tags have to be cleared here as well. The next track's
            // own tags follow on the bus; until they do, showing nothing is
            // right and showing the previous track's is not.
            m_codec.clear();
            m_bitrateKbps = 0;
            refreshStreamFormat();

            m_position = 0;
            m_duration = -1;
            emit positionChanged();
            emit durationChanged();
            qCInfo(lcCore) << "gapless handover to" << m_source.toString();
            emit gaplessAdvance();
        }
        break;

    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(m_pipeline)) {
            GstState newState = GST_STATE_NULL;
            gst_message_parse_state_changed(message, nullptr, &newState, nullptr);
            // Safety net for transitions that complete synchronously and so
            // post no ASYNC_DONE.
            if (newState == GST_STATE_PLAYING && m_playRequested
                && (m_state == Paused || m_state == Loading))
                setState(Playing);
        }
        break;

    default:
        break;
    }
}

void Engine::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    qCDebug(lcCore) << "state ->" << state;

    // Something is playing, so whatever went wrong before it is over — but not
    // yet. Skipping a broken file takes a fraction of a second, so clearing the
    // message here made it flash past unread, and an error nobody can read is
    // not the visible one F-001 asks for. It is held for a few seconds instead,
    // and a fresh failure in the meantime replaces it and starts the wait
    // again. See BUG-025.
    if (state == Playing && !m_errorText.isEmpty()) {
        if (!m_errorHold) {
            m_errorHold = new QTimer(this);
            m_errorHold->setSingleShot(true);
            m_errorHold->setInterval(kErrorHoldMs);
            connect(m_errorHold, &QTimer::timeout, this, [this] {
                if (m_errorText.isEmpty())
                    return;
                m_errorText.clear();
                emit errorTextChanged();
            });
        }
        m_errorHold->start();
    }

    emit stateChanged();
}

void Engine::fail(const QString &text)
{
    // A new failure replaces the last one and restarts its time on screen,
    // rather than being hidden by a hold started for the previous file.
    if (m_errorHold)
        m_errorHold->stop();

    m_errorText = text;
    emit errorTextChanged();

    // Read before the state changes, because a consumer that reacts by moving
    // to the next track will have changed both by the time it can ask.
    const QUrl failed = m_source;
    const bool wasPlaying = m_playRequested;

    setState(Error);
    emit sourceFailed(failed, text, wasPlaying);
}

} // namespace ferrolux::core
