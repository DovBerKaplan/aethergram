/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stdafx.h"

#include "aether/aether_window.h"
#include "aether/aether_surface.h"

#include "ui/rp_widget.h"
#include "ui/painter.h"
#include "ui/widgets/rp_window.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"

#include <QtCore/QDir>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QSettings>
#include <QtGui/QPainterPath>
#include <QtGui/QWindow>

namespace Aether {
namespace {

// The Aether window decoration (theme/generators/build_aurorae.py and
// theme/tokens.toml): 22px circles, 8px apart, 12px from the edge.
constexpr auto kButton = 22;
constexpr auto kSpacing = 8;
constexpr auto kEdge = 12;
constexpr auto kButtonsName = "aetherWindowButtons";

enum class Control {
	Minimize,
	Maximize,
	Close,
};

struct Layout {
	std::vector<Control> controls;
	bool left = false;
};

[[nodiscard]] std::vector<Control> ParseControls(const QString &letters) {
	auto result = std::vector<Control>();
	for (const auto letter : letters) {
		if (letter == 'I') {
			result.push_back(Control::Minimize);
		} else if (letter == 'A') {
			result.push_back(Control::Maximize);
		} else if (letter == 'X') {
			result.push_back(Control::Close);
		}
	}
	return result;
}

// Where the desktop keeps the buttons: KWin's ButtonsOnLeft/Right (Aether
// sets ButtonsOnRight=IAX), read the way the title bars read it.
[[nodiscard]] Layout ReadLayout() {
	const auto path = QDir::homePath() + u"/.config/kwinrc"_q;
	auto settings = QSettings(path, QSettings::IniFormat);
	settings.beginGroup(u"org.kde.kdecoration2"_q);
	const auto left = ParseControls(
		settings.value(u"ButtonsOnLeft"_q, u"MS"_q).toString());
	const auto right = ParseControls(
		settings.value(u"ButtonsOnRight"_q, u"HIAX"_q).toString());
	if (!right.empty() || left.empty()) {
		return { right.empty()
			? std::vector{ Control::Minimize, Control::Maximize, Control::Close }
			: right, false };
	}
	return { left, true };
}

[[nodiscard]] int ButtonsWidth(int count) {
	return count ? (count * kButton + (count - 1) * kSpacing) : 0;
}

[[nodiscard]] bool Dark() {
	return st::windowBg->c.lightnessF() < 0.5;
}

// The glyphs of build_aurorae.py, in its 24 unit box.
[[nodiscard]] QPainterPath Glyph(Control control, bool maximized) {
	auto path = QPainterPath();
	switch (control) {
	case Control::Close:
		path.moveTo(9, 9);
		path.lineTo(15, 15);
		path.moveTo(15, 9);
		path.lineTo(9, 15);
		break;
	case Control::Minimize:
		path.moveTo(8.5, 12);
		path.lineTo(15.5, 12);
		break;
	case Control::Maximize:
		// Two overlapping squares, for maximize and restore alike.
		path.moveTo(10.5, 9.5);
		path.lineTo(10.5, 8.5);
		path.lineTo(15.5, 8.5);
		path.lineTo(15.5, 13.5);
		path.lineTo(14.5, 13.5);
		path.addRoundedRect(QRectF(8.5, 10.5, 5, 5), 0.7, 0.7);
		break;
	}
	return path;
}

class WindowButtons final : public Ui::RpWidget {
public:
	explicit WindowButtons(not_null<QWidget*> parent)
	: RpWidget(parent) {
		setObjectName(QString::fromLatin1(kButtonsName));
		setMouseTracking(true);
		_layout = ReadLayout();
		resizeToLayout();

		// KWin replaces the file when it saves it, so the watch is renewed.
		const auto path = QDir::homePath() + u"/.config/kwinrc"_q;
		_watcher.addPath(path);
		QObject::connect(&_watcher, &QFileSystemWatcher::fileChanged, [=] {
			if (!_watcher.files().contains(path)) {
				_watcher.addPath(path);
			}
			_layout = ReadLayout();
			resizeToLayout();
			_relayout.fire({});
		});

		window()->installEventFilter(this);
		style::PaletteChanged() | rpl::on_next([=] {
			update();
		}, lifetime());
	}

	[[nodiscard]] const Layout &layout() const {
		return _layout;
	}
	[[nodiscard]] rpl::producer<> relayoutRequests() const {
		return _relayout.events();
	}

protected:
	bool eventFilter(QObject *o, QEvent *e) override {
		if (o == window()) {
			const auto type = e->type();
			if (type == QEvent::ActivationChange
				|| type == QEvent::WindowStateChange) {
				update();
			}
		}
		return RpWidget::eventFilter(o, e);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto active = window()->isActiveWindow();
		const auto maximized = window()->isMaximized();
		const auto dark = Dark();
		const auto dot = dark ? QColor(255, 255, 255) : QColor(0x1a, 0x1d, 0x20);
		const auto dotAlpha = dark ? 0.14 : 0.10;
		const auto ink = st::windowFg->c;
		const auto negative = dark ? QColor(0xff, 0x6b, 0x6b) : QColor(0xd6, 0x45, 0x45);
		const auto scale = kButton / 24.;
		for (auto i = 0; i != int(_layout.controls.size()); ++i) {
			const auto control = _layout.controls[i];
			const auto rect = buttonRect(i);
			const auto over = (_over == i);
			const auto down = over && (_down == i);
			auto fill = over
				? ((control == Control::Close) ? negative : st::windowBgActive->c)
				: dot;
			fill.setAlphaF(over
				? (down ? 0.75 : 1.)
				: (dotAlpha * (active ? 1. : 0.6)));
			p.setPen(Qt::NoPen);
			p.setBrush(fill);
			p.drawEllipse(QRectF(rect));

			auto glyphColor = over ? QColor(0x0b, 0x0e, 0x14) : ink;
			glyphColor.setAlphaF(over ? 0.9 : (active ? 0.9 : 0.5));
			auto pen = QPen(glyphColor, 1.5);
			pen.setCapStyle(Qt::RoundCap);
			pen.setJoinStyle(Qt::RoundJoin);
			p.save();
			p.translate(rect.topLeft());
			p.scale(scale, scale);
			p.setBrush(Qt::NoBrush);
			p.setPen(pen);
			p.drawPath(Glyph(control, maximized));
			p.restore();
		}
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		setOver(buttonAt(e->pos()));
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			_down = buttonAt(e->pos());
			update();
		}
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() != Qt::LeftButton) {
			return;
		}
		const auto was = std::exchange(_down, -1);
		update();
		if (was >= 0 && was == buttonAt(e->pos())) {
			activate(_layout.controls[was]);
		}
	}

	void leaveEventHook(QEvent *e) override {
		setOver(-1);
	}

private:
	void resizeToLayout() {
		resize(ButtonsWidth(_layout.controls.size()), kButton);
		update();
	}

	[[nodiscard]] QRect buttonRect(int index) const {
		return QRect(index * (kButton + kSpacing), 0, kButton, kButton);
	}

	[[nodiscard]] int buttonAt(QPoint point) const {
		for (auto i = 0; i != int(_layout.controls.size()); ++i) {
			if (buttonRect(i).contains(point)) {
				return i;
			}
		}
		return -1;
	}

	void setOver(int index) {
		if (_over != index) {
			_over = index;
			update();
		}
	}

	void activate(Control control) {
		const auto w = window();
		switch (control) {
		case Control::Minimize:
			w->setWindowState(w->windowState() | Qt::WindowMinimized);
			break;
		case Control::Maximize:
			if (w->isMaximized()) {
				w->showNormal();
			} else {
				w->showMaximized();
			}
			break;
		case Control::Close:
			// The window's own close: to the tray, if that is set.
			w->close();
			break;
		}
	}

	Layout _layout;
	QFileSystemWatcher _watcher;
	rpl::event_stream<> _relayout;
	int _over = -1;
	int _down = -1;

};

// The widget the shelves are laid on (it holds the field's overlay).
rpl::event_stream<> LaidOut;
bool LaidOutScheduled = false;

[[nodiscard]] QWidget *ShelvesHost(const QWidget *from) {
	for (auto w = from; w; w = w->parentWidget()) {
		if (w->findChild<QWidget*>(
				u"aetherCutouts"_q,
				Qt::FindDirectChildrenOnly)) {
			return const_cast<QWidget*>(w);
		}
	}
	return nullptr;
}

} // namespace

bool NativeFrame() {
	static const auto result = (qgetenv("AETHERGRAM_NATIVE_FRAME") == "1");
	return result;
}

const style::WindowTitle &FlatTitle() {
	static const auto result = [] {
		auto copy = std::make_unique<style::WindowTitle>(st::defaultWindowTitle);
		copy->height = 0;
		copy->shadow = false;
		return copy;
	}();
	return *result;
}

void SetupFieldDrag(not_null<Ui::RpWindow*> window) {
	const auto body = window->body();
	window->setBodyTitleArea([=](QPoint point) {
		using Flag = Ui::WindowTitleHitTestFlag;
		const auto host = static_cast<QWidget*>(
			body->findChild<QWidget*>(u"aetherCutouts"_q));
		const auto shelves = host ? host->parentWidget() : nullptr;
		if (!shelves
			|| !OnField(shelves, shelves->mapFrom(body.get(), point))) {
			return Ui::WindowTitleHitTestFlags(Flag::None);
		}
		return Flag::Move | Flag::Maximize | Flag::Menu;
	});
}

void PlaceWindowButtons(
		not_null<QWidget*> host,
		const std::vector<Shelf> &shelves) {
	auto buttons = static_cast<WindowButtons*>(host->findChild<QWidget*>(
		QString::fromLatin1(kButtonsName),
		Qt::FindDirectChildrenOnly));
	if (NativeFrame() || shelves.empty()) {
		if (buttons) {
			buttons->hide();
		}
		return;
	}
	if (!buttons) {
		buttons = new WindowButtons(host);
		buttons->relayoutRequests() | rpl::on_next([=] {
			// The side changed: let every header re-measure its room.
			host->resize(host->width() + 1, host->height());
			host->resize(host->width() - 1, host->height());
		}, buttons->lifetime());
	}
	const auto left = buttons->layout().left;
	const auto outermost = left
		? ranges::min_element(shelves, std::less<>(), [](const Shelf &s) {
			return s.rect.left();
		})
		: ranges::max_element(shelves, std::less<>(), [](const Shelf &s) {
			return s.rect.x() + s.rect.width();
		});
	const auto &shelf = *outermost;
	const auto x = left
		? (shelf.rect.x() + shelf.headerInset + kEdge)
		: (shelf.rect.x()
			+ shelf.rect.width()
			- shelf.headerInset
			- kEdge
			- buttons->width());
	const auto y = shelf.rect.y() + shelf.header - kButton / 2;
	buttons->move(x, y);
	buttons->show();
	buttons->raise();
	if (!LaidOutScheduled) {
		LaidOutScheduled = true;
		crl::on_main(buttons, [] {
			LaidOutScheduled = false;
			LaidOut.fire({});
		});
	}
}

rpl::producer<> ShelvesLaidOut() {
	return LaidOut.events();
}

QMargins ButtonsReserve(not_null<const QWidget*> widget) {
	if (NativeFrame()) {
		return {};
	}
	const auto host = ShelvesHost(widget);
	if (!host) {
		return {};
	}
	const auto buttons = static_cast<WindowButtons*>(
		host->findChild<QWidget*>(
			QString::fromLatin1(kButtonsName),
			Qt::FindDirectChildrenOnly));
	const auto layout = buttons ? buttons->layout() : ReadLayout();
	const auto r = QRect(widget->mapTo(host, QPoint()), widget->size());
	// Only a header row along the top of the outermost shelf (or inset in
	// it by a gap, like the chat's header pill) is under the buttons; its
	// own end is where the buttons line up from.
	if (r.y() > 3 * kGap) {
		return {};
	}
	const auto room = kEdge + ButtonsWidth(layout.controls.size()) + 8;
	if (layout.left) {
		return (r.x() <= 2 * kGap + 1) ? QMargins(room, 0, 0, 0) : QMargins();
	}
	const auto right = host->width() - (r.x() + r.width());
	return (right <= 2 * kGap + 1) ? QMargins(0, 0, room, 0) : QMargins();
}

} // namespace Aether
