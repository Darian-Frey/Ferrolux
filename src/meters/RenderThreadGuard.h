// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// meters/RenderThreadGuard.h — detection for AV-007.
//
// ARCHITECTURE.md §Key invariants item 3: texture uploads happen only on the
// render thread. `MeterTexture` is built so that they do — band values are
// staged into a plain image on the GUI thread, and the texture is created only
// inside a slot direct-connected to `beforeSynchronizing`, which the scene graph
// emits on the render thread with the GUI thread blocked.
//
// That is the design. This is what would notice if it stopped being true.
//
// **The failure has no symptom on the machine that introduces it.** Uploading
// from a bus handler works under the basic render loop, because there the
// render thread *is* the GUI thread and every rule about which one you are on
// is trivially satisfied. It then corrupts or crashes under the threaded loop,
// on somebody else's driver, at a frame rate they cannot reproduce. Qt does not
// warn: `createTextureFromImage` off the render thread returns a texture that
// mostly works.
//
// So the check is a thread identity, and it has a precondition. Compare the
// thread doing the uploading against the GUI thread, and require them to differ.
// Under the basic loop they never differ and the check must not run at all —
// which is why the counters below also record whether the two threads were ever
// seen to be distinct. A run that cannot tell the threads apart has proved
// nothing, and must report that rather than a pass; `tools/verify-render-thread.sh`
// forces `QSG_RENDER_LOOP=threaded` and fails if this reports otherwise.
//
// The counters are process-wide statics rather than members. The question AV-007
// asks is not whether a particular item misbehaved but whether *any* upload in
// the process happened on the wrong thread, and a per-item tally would have to
// be collected from items the detection cannot enumerate.

#pragma once

#include <QString>

#include <atomic>

class QThread;

namespace ferrolux::meters {

class RenderThreadGuard
{
public:
    // Called when a `MeterTexture` is constructed, which happens on the GUI
    // thread during QML instantiation. First caller wins: every later item
    // confirms the same thread rather than replacing it, so an item built
    // somewhere unexpected shows up as a mismatch instead of quietly becoming
    // the new definition of "the GUI thread".
    static void noteGuiThread(QThread *thread);

    // Called from the staging path, which must be the GUI thread, and from the
    // upload path, which must not be.
    static void noteStage(QThread *thread);
    static void noteUpload(QThread *thread);

    static qint64 uploads();
    static qint64 uploadsOnGuiThread();
    static qint64 stages();
    static qint64 stagesOffGuiThread();

    // Whether an upload was ever seen on a thread other than the GUI thread.
    // False means one of two very different things — the render loop was basic,
    // or nothing was ever drawn — and neither is a pass.
    static bool renderThreadSeen();

    static QString summary();
    static void reset();
};

} // namespace ferrolux::meters
