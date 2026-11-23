/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Aethergram: the main window is a quiet field with shelves floating on it,
// like the island, the dock and the console - not three panels edge to edge.

#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtGui/QPainterPath>
#include <QtGui/QColor>

#include <vector>

class QWidget;

namespace style {
struct FlatButton;
} // namespace style
class QPainter;
class QPainterPath;

namespace Aether {

constexpr auto kGap = 10;      // between shelves, and from the window edge
constexpr auto kRadius = 18;   // the shelves' corners

// What shows between and around the shelves: the window colour, one step
// darker, so a shelf reads as something resting on it.
[[nodiscard]] QColor FieldColor();

// The material, copied from the island's and the console's cards
// (components/shared/AetherShared/CardSurface.qml), made flat: a body under
// an even milky veil, the grain picture laid over it at 4%, and a one pixel
// rim in the accent's hue. Each is drawn over what is already there, inside
// `path`, so a rounded shelf or a bubble with its own corners keeps its shape.
void PaintSheen(QPainter &p, const QRect &rect, const QPainterPath &path);
void PaintGrain(QPainter &p, const QRect &rect, const QPainterPath &path);
void PaintRim(QPainter &p, const QPainterPath &path);

// The same for a plain rounded rectangle.
void PaintMaterial(QPainter &p, const QRect &rect, int radius);

// A card that floats inside the chat (the header, the bars under it, the
// compose pill): a body of see-through window colour, then the material.
[[nodiscard]] QColor GlassColor();
void PaintGlass(QPainter &p, const QRect &rect, int radius);

constexpr auto kPillRadius = 16; // the cards inside the chat

// A full width button at a chat's bottom ("Join", "Mute"), drawn on the
// glass its parent paints, so with no fill of its own.
[[nodiscard]] const style::FlatButton &GlassButton(
	const style::FlatButton &base);

// A rounded rectangle with a radius of its own at each corner.
[[nodiscard]] QPainterPath RoundedPath(
	const QRectF &rect,
	double topLeft,
	double topRight,
	double bottomRight,
	double bottomLeft);

struct Shelf {
	QRect rect;
	// Whether the surface is rough. The chat's own card is not: it is the
	// backdrop, the quietest thing on the screen; the grain belongs to the
	// cards that lie on it (the list, the info panel, the bubbles).
	bool grain = true;
	// How far below the shelf's top its header row is centred: the window
	// buttons line up with it when this shelf is the outermost one.
	int header = 26;
	// How far in from the shelf's sides that header row starts (the chat's
	// header is a pill a gap inside its card).
	int headerInset = 0;
};

// Whether `point` (in `host`) is on the field: in the gaps around and
// between the shelves, not on any of them.
[[nodiscard]] bool OnField(not_null<QWidget*> host, QPoint point);

// Puts the shelves' overlay back on top. Telegram re-orders a window's
// children whenever a chat opens, which would otherwise bury it.
void RaiseShelves(not_null<QWidget*> host);

// Tells `host` where its shelves are: the field's rounded cut-outs are drawn
// over everything inside it, so the children need no rounding of their own.
void SetShelves(not_null<QWidget*> host, std::vector<Shelf> shelves);

} // namespace Aether
