/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Aethergram: no title bar. The window is the field with its shelves from
// edge to edge, and its three buttons float in the corner of the outermost
// shelf, drawn the way the Aether window decoration draws them.

#include "aether/aether_surface.h"

#include <QtCore/QMargins>
#include <QtCore/QRect>

class QWidget;

namespace style {
struct WindowTitle;
} // namespace style

namespace Ui {
class RpWindow;
} // namespace Ui

namespace Aether {

// Whether the window keeps the desktop's own frame and title bar instead,
// AETHERGRAM_NATIVE_FRAME=1 (a way back if something goes wrong).
[[nodiscard]] bool NativeFrame();

// The frameless window's title: no height, no buttons of its own.
[[nodiscard]] const style::WindowTitle &FlatTitle();

// Lets the field (the gaps around and between the shelves) move the window,
// maximize it on a double click and show the window menu on a right one.
void SetupFieldDrag(not_null<Ui::RpWindow*> window);

// Puts the buttons in the corner of the outermost shelf on the side the
// desktop keeps them (kwinrc), centred on that shelf's header row.
void PlaceWindowButtons(
	not_null<QWidget*> host,
	const std::vector<Shelf> &shelves);

// Room a header must leave free at its left and right ends, because the
// window buttons float over that corner of the shelf it sits on.
[[nodiscard]] QMargins ButtonsReserve(not_null<const QWidget*> widget);

// Fires once the shelves have been laid out again, so a header that asked
// for its reserve while it was still being placed can ask again.
[[nodiscard]] rpl::producer<> ShelvesLaidOut();

} // namespace Aether
