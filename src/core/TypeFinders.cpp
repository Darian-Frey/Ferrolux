// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "core/TypeFinders.h"

#include <QLoggingCategory>

#include <gst/gst.h>

#include <cstring>

namespace ferrolux::core {

namespace {

Q_LOGGING_CATEGORY(lcTypes, "ferrolux.core.typefind")

// WavPack. Every file begins with the four ASCII bytes `wvpk`, which is the
// whole of the check — a four-byte magic at offset zero is specific enough that
// a false positive would have to be a file deliberately built to look like one,
// and the alternative is parsing a block header to prove something the decoder
// is about to prove anyway.
//
// **This is a workaround for a defect that is not ours.** GStreamer registers a
// type finder for `audio/x-wavpack` and it does not match: across five files —
// including one written by the reference `wavpack` encoder, which `wvunpack`
// verifies as lossless — it reported `video/x-h264` twice and nothing at all
// three times. `wavpackparse ! wavpackdec` decodes those same files perfectly,
// so the decoder was always there and nothing could reach it. See BUG-024.
//
// Registered at `GST_RANK_PRIMARY` rather than higher. If the upstream finder
// is fixed, both will match and GStreamer takes the more confident answer,
// which costs nothing; ranking this above everything else would instead mean
// out-voting a correct answer with ours.
void findWavpack(GstTypeFind *find, gpointer)
{
    const guint8 *head = gst_type_find_peek(find, 0, 4);
    if (!head || std::memcmp(head, "wvpk", 4) != 0)
        return;

    GstCaps *caps = gst_caps_new_empty_simple("audio/x-wavpack");
    gst_type_find_suggest(find, GST_TYPE_FIND_MAXIMUM, caps);
    gst_caps_unref(caps);
}

} // namespace

void registerTypeFinders()
{
    GstCaps *wavpack = gst_caps_new_empty_simple("audio/x-wavpack");

    // The extensions are a hint for the cases where content is not available;
    // the magic above is what actually decides. `NULL` for the plugin, because
    // this is registered by the application rather than by a plugin being
    // loaded.
    const gboolean ok = gst_type_find_register(
        nullptr, "ferrolux-wavpack", GST_RANK_PRIMARY,
        findWavpack, "wv,wvp", wavpack, nullptr, nullptr);
    gst_caps_unref(wavpack);

    if (!ok)
        qCWarning(lcTypes) << "could not register the WavPack type finder;"
                           << "`.wv` files will not play — see BUG-024";
}

} // namespace ferrolux::core
