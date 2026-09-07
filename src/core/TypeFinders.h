// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// core/TypeFinders.h — formats GStreamer can decode but cannot recognise.
//
// A decoder is only reachable if something identifies the stream first, and the
// two are registered independently: a format can be perfectly decodable and
// completely unplayable at the same time. That is BUG-024, and WavPack is the
// case of it.
//
// This lives in `core/` because it touches GStreamer, which nothing outside
// that directory may do (ARCHITECTURE.md invariant 2). It is registration
// rather than pipeline construction, so it belongs beside `Engine` rather than
// inside it — `Engine` builds a pipeline per player, and a type finder is
// registered once for the process.

#pragma once

namespace ferrolux::core {

// Registers the type finders this application supplies for itself. Call once,
// after `gst_init` and before any pipeline is built. Registering the same name
// twice is harmless; registering after a pipeline has already typed a stream is
// too late for that stream.
void registerTypeFinders();

} // namespace ferrolux::core
