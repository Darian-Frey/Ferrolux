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

#include <QObject>
#include <QString>
#include <QUrl>
#include <QLoggingCategory>

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

public:
    enum State { Stopped, Loading, Playing, Paused, Error };
    Q_ENUM(State)

    explicit Engine(QObject *parent = nullptr);
    ~Engine() override;

    State state() const { return m_state; }
    QUrl source() const { return m_source; }
    bool isSeekable() const { return m_seekable; }
    QString errorText() const { return m_errorText; }

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

signals:
    void stateChanged();
    void sourceChanged();
    void positionChanged();
    void durationChanged();
    void seekableChanged();
    void volumeChanged();
    void balanceChanged();
    void errorTextChanged();

    void endOfStream();
    void previousTrackRequested();

private:
    friend int busDispatch(GstBus *, GstMessage *, void *);

    void buildPipeline();
    void teardownPipeline();
    GstElement *buildAudioFilter();
    void applyVolume();
    void applyBalance();
    void setState(State state);
    void fail(const QString &text);
    void handleMessage(GstMessage *message);
    void refreshSeekable();

    GstElement *m_pipeline = nullptr;   // playbin3
    GstElement *m_balanceElement = nullptr; // audiomixmatrix inside the filter bin
    unsigned int m_busWatch = 0;

    State m_state = Stopped;
    QUrl m_source;
    qint64 m_position = 0;
    qint64 m_duration = -1;
    bool m_seekable = false;
    double m_volume = 0.7;              // SPEC.md §Settings default
    double m_balance = 0.0;
    QString m_errorText;
    bool m_playRequested = false;
};

} // namespace ferrolux::core
