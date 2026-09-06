// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley

#include "platform/Settings.h"

#include <QSettings>
#include <QVariant>

#include "app/Player.h"
#include "core/Engine.h"
#include "core/Equaliser.h"
#include "library/PlaylistModel.h"
#include "meters/MeterSource.h"
#include "ui/ThemeTokens.h"
#include "ui/VisualSettings.h"

namespace ferrolux::platform {

using core::Equaliser;
using library::PlaylistModel;
using meters::MeterSource;

namespace {

// `playback/repeat` is stored as a word rather than as the enumerator's number,
// so that a hand-edited file reads as something a person meant. The pair is
// here rather than at the two call sites because a spelling that round-trips in
// one direction only is a setting that quietly resets itself.
QString repeatToKey(PlaylistModel::RepeatMode mode)
{
    switch (mode) {
    case PlaylistModel::RepeatAll: return QStringLiteral("all");
    case PlaylistModel::RepeatOne: return QStringLiteral("one");
    case PlaylistModel::RepeatOff: break;
    }
    return QStringLiteral("off");
}

PlaylistModel::RepeatMode repeatFromKey(const QString &key)
{
    if (key == QLatin1String("all"))
        return PlaylistModel::RepeatAll;
    if (key == QLatin1String("one"))
        return PlaylistModel::RepeatOne;
    return PlaylistModel::RepeatOff;
}

} // namespace

Settings::Settings(app::Player *player, ui::VisualSettings *visuals,
                   ui::ThemeTokens *theme, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_visuals(visuals)
    , m_theme(theme)
{
}

bool Settings::restore()
{
    const QSettings settings;

    // ---- playback ----------------------------------------------------------
    core::Engine *engine = m_player->engine();
    engine->setVolume(settings.value(QStringLiteral("playback/volume"), 0.7).toDouble());
    engine->setBalance(settings.value(QStringLiteral("playback/balance"), 0.0).toDouble());

    PlaylistModel *playlist = m_player->playlist();
    playlist->setShuffle(settings.value(QStringLiteral("playback/shuffle"), false).toBool());
    playlist->setRepeat(repeatFromKey(
        settings.value(QStringLiteral("playback/repeat"), QStringLiteral("off")).toString()));

    // ---- equaliser ---------------------------------------------------------
    Equaliser *equaliser = m_player->equaliser();
    {
        // Band gains before the enabled flag, so that switching on applies the
        // stored curve in one step rather than ramping to flat and then to the
        // real values.
        QList<double> storedBands;
        for (const QVariant &value : settings.value(QStringLiteral("equaliser/bands")).toList())
            storedBands.append(value.toDouble());
        if (storedBands.size() == Equaliser::kBandCount)
            equaliser->setBands(storedBands);

        equaliser->setPreamp(settings.value(QStringLiteral("equaliser/preamp"), 0.0).toDouble());
        equaliser->setEnabled(settings.value(QStringLiteral("equaliser/enabled"), false).toBool());

        // Last, and only as a label. The curve is already loaded; this asks the
        // equaliser whether that curve is still the preset it was saved under,
        // and takes the name only if it is. Written on exit and never read, the
        // key left the panel reporting `flat` over somebody else's curve —
        // BUG-019.
        equaliser->adoptPreset(settings.value(QStringLiteral("equaliser/preset")).toString());
    }

    // ---- meters ------------------------------------------------------------
    MeterSource *meters = m_player->meters();
    meters->setMode(settings.value(QStringLiteral("meters/mode"),
                                   QStringLiteral("spectrum")).toString());
    meters->setReferenceLevel(settings.value(QStringLiteral("meters/reference-level"),
                                             MeterSource::kDefaultReferenceDb).toDouble());
    meters->setBandCount(settings.value(QStringLiteral("meters/bands"),
                                        MeterSource::kSpectrumBands).toInt());

    // The proportions of the displays. `VisualSettings` reads its own nine keys
    // because it also clamps them, and clamping on load as well as on write is
    // the whole reason that class exists — see F-036.
    m_visuals->load();

    // ---- appearance --------------------------------------------------------
    // A *name* that no longer resolves is a stale setting, and `loadNamed`
    // falls back for it. A set that will not load at all is fatal, and the
    // caller decides what to do about that.
    if (!m_theme->loadNamed(settings.value(QStringLiteral("ui/theme"),
                                           ui::ThemeTokens::defaultName()).toString())) {
        m_error = m_theme->lastError();
        return false;
    }

    return true;
}

void Settings::attachWindow(QObject *window)
{
    m_window = window;
    if (!m_window)
        return;

    const QSettings settings;

    // F-042. Set directly rather than through the panel's own fold, which
    // animates the window down from whatever height it had: at startup there is
    // nothing to come down from, and the height it would be told to return to
    // would be the default rather than the one the user last had.
    if (settings.value(QStringLiteral("ui/compact"), false).toBool())
        m_window->setProperty("compact", true);

    m_window->setProperty("displayInverted",
                          settings.value(QStringLiteral("ui/display-inverted"), false).toBool());
}

void Settings::save() const
{
    QSettings settings;

    const core::Engine *engine = m_player->engine();
    settings.setValue(QStringLiteral("playback/volume"), engine->volume());
    settings.setValue(QStringLiteral("playback/balance"), engine->balance());

    const PlaylistModel *playlist = m_player->playlist();
    settings.setValue(QStringLiteral("playback/shuffle"), playlist->shuffle());
    settings.setValue(QStringLiteral("playback/repeat"), repeatToKey(playlist->repeat()));

    // SPEC.md §Settings: the preset name is recorded, but the band values are
    // what is authoritative on restore — a preset may have been edited, or its
    // definition may have changed since it was chosen.
    const Equaliser *equaliser = m_player->equaliser();
    QVariantList bands;
    for (double gain : equaliser->bands())
        bands.append(gain);
    settings.setValue(QStringLiteral("equaliser/enabled"), equaliser->isEnabled());
    settings.setValue(QStringLiteral("equaliser/preamp"), equaliser->preamp());
    settings.setValue(QStringLiteral("equaliser/bands"), bands);
    settings.setValue(QStringLiteral("equaliser/preset"), equaliser->preset());

    const MeterSource *meters = m_player->meters();
    settings.setValue(QStringLiteral("meters/mode"), meters->mode());
    settings.setValue(QStringLiteral("meters/reference-level"), meters->referenceLevel());
    settings.setValue(QStringLiteral("meters/bands"), meters->bandCount());

    m_visuals->save();

    settings.setValue(QStringLiteral("ui/theme"), m_theme->name());

    // Nothing to say about the panel if there was never a panel — a run that
    // failed before the window loaded should not overwrite how the user left it.
    if (m_window) {
        settings.setValue(QStringLiteral("ui/compact"), m_window->property("compact"));
        settings.setValue(QStringLiteral("ui/display-inverted"),
                          m_window->property("displayInverted"));
    }
}

} // namespace ferrolux::platform
