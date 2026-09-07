// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "platform/X11MediaKeys.h"

#include <QGuiApplication>
#include <QLoggingCategory>

#include "app/Player.h"

#ifdef FERROLUX_HAVE_X11
#include <QtGui/qguiapplication_platform.h>

#include <X11/Xlib.h>
#include <xcb/xcb.h>

// X11's headers define these as macros, and they collide with names used all
// over Qt. Undefined immediately, after the two headers that need them.
#undef Bool
#undef Status
#undef None
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef Expose
#undef Always
#undef CursorShape
#endif

namespace ferrolux::platform {

namespace {

Q_LOGGING_CATEGORY(lcGrab, "ferrolux.platform.grab")

// What each key means. Values are indices into the switch in the filter rather
// than an enum, because they cross a C boundary and a plain int is what a
// keycode map can hold without ceremony.
enum Action { Play = 0, Stop = 1, Previous = 2, Next = 3 };

#ifdef FERROLUX_HAVE_X11

// The four transport keysyms. `XF86AudioPlay` is the toggle on a keyboard with
// one transport key, which is nearly all of them.
constexpr unsigned long kPlay = 0x1008FF14;
constexpr unsigned long kStop = 0x1008FF15;
constexpr unsigned long kPrev = 0x1008FF16;
constexpr unsigned long kNext = 0x1008FF17;

// A grab is made against an exact modifier state, so a key pressed with Num
// Lock on is a different grab from the same key with it off. Every combination
// of the three locking modifiers has to be taken separately or the key works
// until somebody presses Num Lock and then silently stops — which is the
// characteristic bug of hand-written global hotkeys.
constexpr unsigned int kIgnored[] = {
    0,
    LockMask,                       // Caps Lock
    Mod2Mask,                       // Num Lock, conventionally
    Mod5Mask,                       // Scroll Lock, on some layouts
    LockMask | Mod2Mask,
    LockMask | Mod5Mask,
    Mod2Mask | Mod5Mask,
    LockMask | Mod2Mask | Mod5Mask,
};

Display *x11Display()
{
    const auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->display() : nullptr;
}

// Xlib reports errors asynchronously through a global handler, so a failed
// `XGrabKey` does not return anything — the BadAccess arrives later, out of
// band, and by default kills the process with a message about a request nobody
// recognises. This catches it instead.
bool g_grabFailed = false;
int (*g_previousHandler)(Display *, XErrorEvent *) = nullptr;

int noteGrabFailure(Display *, XErrorEvent *)
{
    g_grabFailed = true;
    return 0;
}

#endif // FERROLUX_HAVE_X11

} // namespace

X11MediaKeys::X11MediaKeys(app::Player *player, QObject *parent)
    : QObject(parent)
    , m_player(player)
{
}

X11MediaKeys::~X11MediaKeys()
{
    release();
}

bool X11MediaKeys::possible()
{
#ifdef FERROLUX_HAVE_X11
    return x11Display() != nullptr;
#else
    return false;
#endif
}

bool X11MediaKeys::grab()
{
#ifdef FERROLUX_HAVE_X11
    if (m_holding)
        return true;

    Display *display = x11Display();
    if (!display)
        return false;

    // Rebuilt every time. A layout changed mid-session moves keycodes, and a
    // map kept from the last grab would then dispatch the wrong action or none.
    m_bound.clear();
    const struct { unsigned long keysym; int action; } wanted[] = {
        { kPlay, Play }, { kStop, Stop }, { kPrev, Previous }, { kNext, Next },
    };

    const Window root = DefaultRootWindow(display);

    g_grabFailed = false;
    g_previousHandler = XSetErrorHandler(noteGrabFailure);

    int taken = 0;
    for (const auto &entry : wanted) {
        const int code = XKeysymToKeycode(display, entry.keysym);
        if (code == 0)
            continue; // this keyboard does not have the key at all

        bool ok = true;
        for (unsigned int mask : kIgnored) {
            g_grabFailed = false;
            XGrabKey(display, code, mask, root, /*owner_events=*/True,
                     GrabModeAsync, GrabModeAsync);
            XSync(display, False);
            if (g_grabFailed) {
                ok = false;
                break;
            }
        }

        if (ok) {
            m_bound.insert(code, entry.action);
            ++taken;
        } else {
            // Somebody else holds it. Give back the combinations that did
            // succeed rather than leaving a half-grab behind — a key taken and
            // not listened for is worse than a key not taken.
            for (unsigned int mask : kIgnored)
                XUngrabKey(display, code, mask, root);
            XSync(display, False);
            qCInfo(lcGrab) << "another client already holds keysym"
                           << Qt::hex << entry.keysym << "— leaving it alone";
        }
    }

    XSetErrorHandler(g_previousHandler);

    if (taken == 0)
        return false;

    qApp->installNativeEventFilter(this);
    m_holding = true;
    qCDebug(lcGrab) << "grabbed" << taken << "media keys on the root window";
    return true;
#else
    return false;
#endif
}

void X11MediaKeys::release()
{
#ifdef FERROLUX_HAVE_X11
    if (!m_holding)
        return;

    if (Display *display = x11Display()) {
        const Window root = DefaultRootWindow(display);
        for (auto code = m_bound.keyBegin(); code != m_bound.keyEnd(); ++code) {
            for (unsigned int mask : kIgnored)
                XUngrabKey(display, *code, mask, root);
        }
        XSync(display, False);
    }

    qApp->removeNativeEventFilter(this);
    m_bound.clear();
    m_holding = false;
    qCDebug(lcGrab) << "released the media keys";
#endif
}

bool X11MediaKeys::nativeEventFilter(const QByteArray &type, void *message, qintptr *)
{
#ifdef FERROLUX_HAVE_X11
    if (!m_holding || type != QByteArrayLiteral("xcb_generic_event_t"))
        return false;

    auto *event = static_cast<xcb_generic_event_t *>(message);

    // The high bit marks an event as sent by another client rather than by the
    // server; it is not part of the type.
    if ((event->response_type & ~0x80) != XCB_KEY_PRESS)
        return false;

    auto *key = reinterpret_cast<xcb_key_press_event_t *>(event);
    const auto found = m_bound.constFind(key->detail);
    if (found == m_bound.constEnd())
        return false;

    switch (found.value()) {
    case Play:     m_player->playPause(); break;
    case Stop:     m_player->stop(); break;
    case Previous: m_player->previous(); break;
    case Next:     m_player->next(); break;
    default:       return false;
    }

    // Handled, and not passed on. Nothing else in this application wants a
    // media key, and a grabbed key delivered onward would act twice.
    return true;
#else
    Q_UNUSED(type); Q_UNUSED(message);
    return false;
#endif
}

} // namespace ferrolux::platform
