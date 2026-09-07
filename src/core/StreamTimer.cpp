// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "core/StreamTimer.h"

namespace ferrolux::core {

QString StreamTimer::summary() const
{
    return QStringLiteral("calls=%1 worst=%2us mean=%3us budget=%4us over=%5")
        .arg(calls())
        .arg(worstUs(), 0, 'f', 1)
        .arg(meanUs(), 0, 'f', 1)
        .arg(kBudgetUs, 0, 'f', 0)
        .arg(overBudget());
}

} // namespace ferrolux::core
