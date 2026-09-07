// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// tests/Check.h — the two lines every suite prints. IMP-004.
//
// Six suites had their own copy of this, about fifteen lines apiece, and the
// duplication was deliberate for as long as there were four: each suite was one
// self-contained file that could be read start to finish without following an
// include, and fifteen repeated lines are cheaper than a permanent coupling.
//
// The deferral was against a fifth suite arriving. Two did.
//
// **The copies had already drifted**, which is the part worth recording rather
// than the count. `tokens_test`'s `check` was missing the `std::fflush` the
// other five had — so a suite that crashed mid-run lost its last few lines of
// output, and lost exactly the lines that would have said where it got to. That
// is the failure mode duplication produces: not a wrong answer, but five
// correct copies and one that is subtly less useful in the one situation you
// need it.
//
// Kept deliberately small. This is a printer and a counter; anything that grows
// here becomes a test framework, and the suites are readable now because they
// are plain programs that print what they checked.

#pragma once

#include <QString>

#include <cstdio>

namespace ferrolux::tests {

// One per executable. Each suite is a single translation unit, so this is a
// counter rather than shared state between anything.
inline int failures = 0;

// Prints one line per assertion, whether it passed or not. Every suite prints
// its passes as well as its failures on purpose: a run that says only what went
// wrong cannot be read as evidence that anything went right, and the count of
// `[pass]` lines is how the checks in this project are counted at all.
inline void check(bool ok, const char *what, const QString &detail = {})
{
    std::printf("  [%s] %s%s%s\n", ok ? "pass" : "FAIL", what,
                detail.isEmpty() ? "" : " — ", detail.isEmpty() ? "" : qPrintable(detail));

    // Flushed every line. A suite that aborts — and several of these drive a
    // real pipeline, which can — otherwise loses the buffered tail, which is
    // the part naming the check it died on.
    std::fflush(stdout);

    if (!ok)
        ++failures;
}

// The last line, and the process's exit status. Returned rather than exited
// from, so a suite can still do its own cleanup after saying how it went.
inline int summary()
{
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    std::fflush(stdout);
    return failures ? 1 : 0;
}

} // namespace ferrolux::tests
