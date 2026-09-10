// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// core/Engine.h — playback engine for Ferrolux RS-1.
//
// Delivers F-001 through F-004. Owns the GStreamer pipeline outright:
// per ARCHITECTURE.md §Key invariants item 2, no GstElement* exists outside
// core/, which is why this header forward-declares the GStreamer types rather
// than including gst/gst.h and leaking them to every translation unit that
// needs to talk to the engine.
//
// State machine
// -------------
// Five states. Transitions not listed below do not occur; anything else
// arriving from the pipeline is a defect rather than a state to handle.
//
//   From      Event                          To         Notes
//   --------  -----------------------------  ---------  ------------------------
//   any       setSource(url)                 Loading    Clears any error
//   Loading   ASYNC_DONE (preroll complete)  Playing    Or Paused if play() has
//                                                       not been called yet
//   Loading   bus ERROR                      Error      Recoverable: F-001 says
//                                                       advance, do not stall
//   Playing   pause()                        Paused     Position preserved
//   Paused    play()                         Playing    Resumes from position
//   Playing   EOS                            Stopped    Emits endOfStream()
//   Playing   bus ERROR                      Error
//   Paused    bus ERROR                      Error
//   any       stop()                         Stopped    Position reset to zero
//   Error     setSource(url)                 Loading    The only exit from Error
//
// Stop resets position to zero and pause preserves it, per F-002.
//
// Threading
// ---------
// Everything here runs on the Qt main thread. The bus watch is installed with
// gst_bus_add_watch, which dispatches on the main loop, not on a streaming
// thread — see ARCHITECTURE.md §Key invariants item 1 and AV-001. No callback
// in this class may be moved to a sync handler without revisiting that.

#pragma once

#include <QAtomicInt>
#include <QList>
#include <QByteArray>
#include <QMutex>

#include "core/StreamTimer.h"
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QLoggingCategory>

#include "core/Equaliser.h"

// Forward declarations, so that gst/gst.h stays inside core/.
typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;
typedef struct _GstMessage GstMessage;

Q_DECLARE_LOGGING_CATEGORY(lcCore)

namespace ferrolux::core {

class Engine : public QObject
{
    Q_OBJECT

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool seekable READ isSeekable NOTIFY seekableChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(double balance READ balance WRITE setBalance NOTIFY balanceChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

    // What the stream actually is: sample rate, channel count, codec and
    // bitrate, assembled into one line for the display. Empty until the
    // pipeline has negotiated and the tags have arrived, which is not at the
    // same moment — the caps are known at ASYNC_DONE and the tags turn up on
    // the bus whenever the demuxer gets to them, so this is rebuilt from
    // whatever is known each time either changes rather than composed once.
    Q_PROPERTY(QString streamFormat READ streamFormat NOTIFY streamFormatChanged)

public:
    enum State { Stopped, Loading, Playing, Paused, Error };
    Q_ENUM(State)

    explicit Engine(QObject *parent = nullptr);
    ~Engine() override;

    // The equaliser is owned here because it owns GStreamer objects, which
    // ARCHITECTURE.md invariant 2 confines to core/. It is exposed by pointer
    // so that QML and the settings code can reach it without either learning
    // anything about the backend.
    Equaliser *equaliser() { return &m_equaliser; }

    // AV-001's figures, for whoever is measuring. Const, because a caller that
    // could reset this could also make a stress run report a clean sheet it did
    // not earn.
    const StreamTimer &streamTimer() const { return m_streamTimer; }
    const StreamTimer &handoverTimer() const { return m_handoverTimer; }

    State state() const { return m_state; }
    QUrl source() const { return m_source; }
    bool isSeekable() const { return m_seekable; }
    QString errorText() const { return m_errorText; }
    QString streamFormat() const { return m_streamFormat; }

    // Cached values. Invariant 4: these are refreshed only by poll(), so every
    // consumer reads the same number within a frame and no consumer triggers a
    // pipeline query of its own.
    qint64 position() const { return m_position; }
    qint64 duration() const { return m_duration; }

    // Taper position in 0.0..1.0, not amplitude. SPEC.md §Volume taper.
    double volume() const { return m_volume; }
    double balance() const { return m_balance; }

    // The gain laws, as pure functions so that they can be tested without a
    // pipeline. Both are specified in SPEC.md §Volume taper. balanceGains
    // attenuates only and never returns a value above unity — see BUG-003,
    // where a constant-power panning law had been specified for what is a
    // stereo balance control.
    static double volumeAmplitude(double taperPosition);
    static void balanceGains(double balance, double &left, double &right);

public slots:
    void setSource(const QUrl &url);

    // Arms the gapless handover (F-005). The playlist model calls this on the
    // main thread whenever the next entry changes; playbin3's about-to-finish
    // handler consumes it on a streaming thread. Passing an empty URL disarms,
    // which is how the end of a playlist stops rather than looping.
    //
    // The cached URI is a pre-encoded QByteArray behind a dedicated mutex, so
    // the streaming thread performs one short lock and a memcpy and touches no
    // application state at all. That mutex is a leaf: it is never held while
    // anything else is taken, and never contended with UI work. This is the
    // narrow reading of ARCHITECTURE.md invariant 1 that F-005 requires — see
    // AV-001 and AV-006.
    void setNextSource(const QUrl &url);

    void play();
    void pause();
    void stop();

    // Seeks to an absolute position in nanoseconds. Flushing and accurate:
    // F-003 requires a VBR MP3 seek to land within 500 ms of the target, which
    // the default key-unit seek cannot guarantee in a variable-bitrate stream.
    void seek(qint64 positionNs);

    // F-002: within the first three seconds, previous means the previous track
    // and is the playlist's decision (invariant 5), so this emits rather than
    // acts. Later than that, it restarts the current track.
    void previous();

    void setVolume(double taperPosition);
    void setBalance(double balance);

    // Invariant 4. Called once per rendered frame from the QML harness and
    // from nowhere else. Refreshes the position and duration caches, emitting
    // change signals only when a value has actually moved.
    void poll();

    // The pipeline's current running time, which is the moment actually being
    // rendered. -1 when there is no clock yet.
    qint64 runningTime() const;

signals:
    void stateChanged();
    void sourceChanged();
    void positionChanged();

    // A discontinuity, as distinct from ordinary progress. `positionChanged`
    // fires every frame and says nothing about how the position got there, so a
    // consumer that has to redraw a timeline only when it jumps — MPRIS's
    // `Seeked`, per F-050 — cannot use it. Carries the new position in
    // nanoseconds.
    void seeked(qint64 positionNs);
    void durationChanged();
    void seekableChanged();
    void volumeChanged();
    void balanceChanged();
    void errorTextChanged();
    void streamFormatChanged();

    void endOfStream();

    // A source could not be played. Carries the file, the message shown to the
    // user, and whether playback had actually been asked for — a track that was
    // merely selected and turned out to be broken should report itself and stay
    // put, while one that was playing should be stepped over. F-001's second
    // acceptance clause, BUG-025.
    void sourceFailed(const QUrl &url, const QString &text, bool wasPlaying);
    void previousTrackRequested();

    // Analysis, parsed from element messages on the bus and handed on as plain
    // values. GStreamer types stop here, per ARCHITECTURE.md invariant 2, which
    // is what lets meters/ be tested without a pipeline. All in decibels, as
    // the elements report them.
    // runningTimeNs is the moment in the stream these figures describe, which
    // is well ahead of what is being heard — see BUG-011. Consumers schedule
    // against it rather than applying on arrival.
    void levelMeasured(const QList<double> &rmsDb, const QList<double> &peakDb,
                       const QList<double> &decayDb, qint64 runningTimeNs);
    void spectrumMeasured(const QList<float> &magnitudesDb, int sampleRate,
                          qint64 runningTimeNs);

    // Playback has already crossed into the next track without a gap. Emitted
    // from the bus handler on the main loop, never from the streaming thread
    // that performed the handover. The playlist advances its cursor in response;
    // it must not start playback, because playback never stopped.
    void gaplessAdvance();

private:
    friend int busDispatch(GstBus *, GstMessage *, void *);
    friend void aboutToFinish(GstElement *, void *);

    void buildPipeline();
    void teardownPipeline();
    GstElement *buildAudioFilter();
    void applyVolume();
    void applyBalance();
    void setState(State state);
    void fail(const QString &text);
    void handleMessage(GstMessage *message);
    void handleAnalysisMessage(GstMessage *message);
    void refreshSeekable();

    Equaliser m_equaliser;

    GstElement *m_pipeline = nullptr;   // playbin3
    GstElement *m_balanceElement = nullptr; // audiomixmatrix inside the filter bin
    GstElement *m_filterBin = nullptr;  // for the negotiated input caps
    GstElement *m_levelElement = nullptr;
    GstElement *m_spectrumElement = nullptr;
    int m_analysisRate = 0;                 // cached from negotiated caps
    unsigned int m_busWatch = 0;

    State m_state = Stopped;
    QUrl m_source;
    qint64 m_position = 0;
    qint64 m_duration = -1;
    bool m_seekable = false;
    double m_volume = 0.7;              // SPEC.md §Settings default
    double m_balance = 0.0;
    QString m_errorText;

    // Holds the last error on screen for a few seconds after playback resumes.
    // Skipping a broken file takes well under a second, so clearing the message
    // the moment something plays makes it flash past unread — which is not the
    // "visible error" F-001 asks for. See BUG-025.
    QTimer *m_errorHold = nullptr;

    // Rebuilt from these whenever any of them changes; see streamFormat.
    void refreshStreamFormat();
    QString m_streamFormat;
    QString m_codec;
    int m_bitrateKbps = 0;
    bool m_playRequested = false;

    // Written on the main thread, read on a streaming thread.
    // AV-001's detection. Written from the streaming thread inside
    // `aboutToFinish`, read from the main thread by whoever is measuring; the
    // class is all atomics, so neither needs the mutex below.
    StreamTimer m_streamTimer;

    // The handover call alone. Separate because it is GStreamer's time rather
    // than ours: `g_object_set` on `uri` is how `about-to-finish` is answered,
    // and what `playbin3` does inside it is not this project's to shorten. It
    // is measured because it is still time on a streaming thread, and the
    // device is the judge of whether it was too much.
    StreamTimer m_handoverTimer;

    QMutex m_nextMutex;
    QByteArray m_nextUri;
    // The URI the streaming thread actually handed over to, so that the main
    // thread can bring m_source up to date when STREAM_START arrives. Without
    // it, source() keeps reporting the previous track across a gapless join and
    // anything keyed on it — the duration correction, most obviously — lands on
    // the wrong entry.
    QByteArray m_handoverUri;
    QAtomicInt m_handoverPending{0};

    // BUG-027. Set when the source armed for a gapless handover failed, so the
    // current track was left to finish a stream that `playbin3` will never end.
    // `poll()` watches for it running out and ends it.
    // BUG-027, the fix. The next source is rehearsed on a throwaway pipeline
    // before it is armed: a second `playbin3` with fake sinks, brought to
    // PAUSED asynchronously on the main thread. If it prerolls, the real
    // handover will too and the URI is committed to `m_nextUri`; if it errors,
    // nothing is armed and the stream ends normally with EOS. The race that
    // cost the current track its tail cannot be lost, because it never starts.
    GstElement *m_rehearsal = nullptr;
    unsigned int m_rehearsalWatch = 0;
    QByteArray m_rehearsing;
    void rehearse(const QByteArray &uri);
    void cancelRehearsal();
    void handleRehearsalMessage(GstMessage *message);
    friend int rehearsalDispatch(GstBus *, GstMessage *, void *);

    bool m_handoverFailed = false;
    qint64 m_lastPolledPosition = -1;
    int m_stalledPolls = 0;
};

} // namespace ferrolux::core
