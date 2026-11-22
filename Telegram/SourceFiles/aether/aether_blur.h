/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Aethergram: KWin blurs what lies behind a window that asks for it, which
// is what makes the desktop's glass (the island, the dock, Dolphin under the
// Aether theme). Qt has no call for it and KDE's own library is not in the
// build, so this speaks the small ext-background-effect protocol itself.

class QWidget;

namespace Aether {

// Asks the compositor to blur everything behind `window`. Does nothing when
// the session is not Wayland or the compositor offers no blur (not KDE). It
// follows the window through its surface being recreated or resized.
void SetupWindowBlur(not_null<QWidget*> window);

} // namespace Aether
