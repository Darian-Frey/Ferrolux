#include "core/Engine.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

#include <gst/gst.h>

Q_LOGGING_CATEGORY(lcCore, "ferrolux.core")

namespace ferrolux::core {
namespace {

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

    if (!bin || !convertIn || !stereo || !matrix || !convertOut) {
        qCWarning(lcCore) << "audio filter unavailable; balance will be inoperative";
        if (bin) gst_object_unref(bin);
        if (convertIn) gst_object_unref(convertIn);
        if (stereo) gst_object_unref(stereo);
        if (matrix) gst_object_unref(matrix);
        if (convertOut) gst_object_unref(convertOut);
        return nullptr;
    }

    // audiomixmatrix declares channels [1, MAX] on its pads, so nothing else in
    // the chain pins the count and a mono source would negotiate one channel
    // against an element configured for two — which fails at set_caps with
    // "Erroneous matrix detected" rather than at link time. Forcing stereo here
    // makes audioconvert up-mix mono and guarantees the matrix matches.
    GstCaps *caps = gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT, 2, nullptr);
    g_object_set(stereo, "caps", caps, nullptr);
    gst_caps_unref(caps);

    g_object_set(matrix, "in-channels", 2, "out-channels", 2, nullptr);

    // The matrix must carry correctly sized rows before the link is attempted:
    // audiomixmatrix refuses to link at all while its matrix is empty.
    m_balanceElement = matrix;
    applyBalance();

    gst_bin_add_many(GST_BIN(bin), convertIn, stereo, matrix, convertOut, nullptr);
    if (!gst_element_link_many(convertIn, stereo, matrix, convertOut, nullptr)) {
        qCWarning(lcCore) << "could not link the audio filter chain";
        m_balanceElement = nullptr;
        gst_object_unref(bin);
        return nullptr;
    }

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

    if (!m_errorText.isEmpty()) {
        m_errorText.clear();
        emit errorTextChanged();
    }

    g_object_set(m_pipeline, "uri", url.toString().toUtf8().constData(), nullptr);

    emit sourceChanged();
    emit positionChanged();
    emit durationChanged();
    emit seekableChanged();

    setState(Loading);
    gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    qCInfo(lcCore) << "loading" << url.toString();
}

void Engine::play()
{
    if (!m_pipeline || m_source.isEmpty())
        return;

    m_playRequested = true;

    // Playing from Stopped means the pipeline is in READY and has to preroll
    // again, so it re-enters Loading and reaches Playing on ASYNC_DONE. Without
    // this the state would stay Stopped while audio was audibly running.
    if (m_state == Stopped || m_state == Error)
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

void Engine::handleMessage(GstMessage *message)
{
    switch (GST_MESSAGE_TYPE(message)) {
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

    case GST_MESSAGE_ASYNC_DONE:
        // Preroll complete: duration and seekability are answerable now.
        refreshSeekable();
        if (m_state == Loading)
            setState(m_playRequested ? Playing : Paused);
        break;

    case GST_MESSAGE_DURATION_CHANGED:
        m_duration = -1; // forces the next poll() to re-query
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
    emit stateChanged();
}

void Engine::fail(const QString &text)
{
    m_errorText = text;
    emit errorTextChanged();
    setState(Error);
}

} // namespace ferrolux::core
