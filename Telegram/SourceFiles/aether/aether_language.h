/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Aethergram: follow the Aether desktop language and set the layout
// direction (right-to-left for Hebrew and friends) from it.

class QString;

namespace Aether {

// True for language ids whose base (the part before '-') is one of the
// right-to-left languages we know about: he, iw, ar, fa, ur, yi.
[[nodiscard]] bool IsRightToLeftLanguage(const QString &id);

// Sets style::RightToLeft() and the Qt application layout direction from
// the current language pack (Lang::GetInstance().id()). The AETHERGRAM_LAYOUT
// environment variable (ltr|rtl) overrides the detection for testing.
// Must be called before the first widget is created.
void ApplyLayoutDirection();

// The language the Aether desktop is using, read (read-only!) from
// $QStandardPaths::GenericConfigLocation/aetherislandrc, [Interface] group,
// "Language" key. Lowercased ("he", "en", ...), or empty if unknown.
[[nodiscard]] QString DesktopLanguage();

// Follows the Aether desktop language: only switches the Aethergram language
// pack when the Aether desktop language *changes* (so a language picked in
// Aethergram's own Settings is respected until Aether changes its language).
// Also watches the rc file and re-checks on change.
void FollowDesktopLanguage();

} // namespace Aether
