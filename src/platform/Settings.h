// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// platform/Settings.h — what the application remembers between runs.
//
// Persistence is a desktop concern rather than an audio one: where the file
// lands is `QStandardPaths`' answer, not GStreamer's, which is why this was
// always going to end up in `platform/` and why ARCHITECTURE.md promised the
// move before the directory existed. `core/` acquires no dependency on it.
//
// **SPEC.md §Settings owns the keys.** Every string below appears there with a
// type, a default and a reason; none of them is invented here, and a key added
// to one without the other is an incomplete change. That is the whole point of
// gathering them: while they were spread through `main()` it was possible for
// `equaliser/preset` to be written on exit and never read on start, which is
// BUG-019 and went unnoticed until the panel had a lit field to show it in.
//
// Restoring is not simply the reverse of saving, and the order matters in three
// places that are commented where they happen. Anything that has to wait for a
// panel to exist goes through `attachWindow`, because the window is built by
// QML after everything else is already running.

#pragma once

#include <QObject>
#include <QString>

namespace ferrolux::app { class Player; }
namespace ferrolux::ui { class ThemeTokens; class VisualSettings; }

namespace ferrolux::platform {

class Settings : public QObject
{
    Q_OBJECT

public:
    // Holds what it persists rather than being handed it twice. Nothing is read
    // or written by the constructor: an object that touched the disk on
    // construction could not be built in a test without a settings file.
    Settings(app::Player *player, ui::VisualSettings *visuals, ui::ThemeTokens *theme,
             QObject *parent = nullptr);

    // Applies every stored value to the objects. Returns false only when the
    // *token set will not load at all*, which is fatal and is the caller's to
    // act on — a half-loaded set draws the chassis in whatever a missing colour
    // resolves to, which is black, and black chrome under a black readout is
    // not a visible failure. A stale theme *name* is a different thing and is
    // handled here, by falling back and saying so.
    bool restore();
    QString lastError() const { return m_error; }

    // The panel's own state, which cannot be applied until there is a panel.
    // Taken as a plain QObject and read by property name, so `platform/` needs
    // no dependency on Qt Quick to remember how the window was left.
    void attachWindow(QObject *window);

public slots:
    // Connect once to `aboutToQuit`. Not to a destructor and not to a timer:
    // SIGTERM never reaches `aboutToQuit` either, so anything verifying this
    // has to close the window rather than kill the process.
    void save() const;

private:
    app::Player *m_player = nullptr;
    ui::VisualSettings *m_visuals = nullptr;
    ui::ThemeTokens *m_theme = nullptr;
    QObject *m_window = nullptr;
    QString m_error;
};

} // namespace ferrolux::platform
