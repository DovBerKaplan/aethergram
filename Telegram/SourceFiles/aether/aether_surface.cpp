/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stdafx.h"

#include "aether/aether_surface.h"
#include "aether/aether_blur.h"

#include "ui/rp_widget.h"
#include "ui/painter.h"
#include "ui/style/style_core.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainterPath>

#include <QtCore/QStandardPaths>

#include <algorithm>
#include <random>

namespace Aether {
namespace {

// The grain picture the island and the console use (192 px, grey noise with
// a mean of 128 and a spread of about 40), laid at 4%. It is installed with
// the shared QML; without it, noise of the same kind is made here.
const QPixmap &GrainTile() {
	static const auto tile = [] {
		const auto path = QStandardPaths::locate(
			QStandardPaths::GenericDataLocation,
			u"aether/shared/AetherShared/assets/grain.png"_q);
		auto image = path.isEmpty() ? QImage() : QImage(path);
		if (image.isNull()) {
			constexpr auto size = 192;
			image = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
			auto engine = std::mt19937(0xAE7);
			auto normal = std::normal_distribution<double>(128., 40.);
			for (auto y = 0; y != size; ++y) {
				const auto line = reinterpret_cast<uint32*>(image.scanLine(y));
				for (auto x = 0; x != size; ++x) {
					const auto v = uint32(std::clamp(normal(engine), 0., 255.));
					line[x] = 0xFF000000 | (v << 16) | (v << 8) | v;
				}
			}
		}
		return QPixmap::fromImage(std::move(image));
	}();
	return tile;
}

constexpr auto kGrainOpacity = 0.04;   // Tokens.material.grainOpacity
constexpr auto kGrainOpacityLight = 0.025; // the same noise reads louder on light glass

[[nodiscard]] bool Dark() {
	return st::windowBg->c.lightnessF() < 0.5;
}

// The rim, as Theme.qml makes it for a chosen colour: that hue, light and
// faint on dark glass, a little stronger on the light one.
QColor RimColor() {
	const auto accent = st::windowBgActive->c;
	const auto hue = accent.hslHueF();
	return Dark()
		? QColor::fromHslF(std::max(0.f, float(hue)), 0.18, 0.67, 0.26)
		: QColor::fromHslF(std::max(0.f, float(hue)), 0.62, 0.80, 0.34);
}

// Paints the field over everything below it except the shelves' insides, so
// a shelf's four corners come out rounded and antialiased, whatever the
// child widgets paint (a wallpaper, a list, a plain fill).
class Cutouts final : public Ui::RpWidget {
public:
	explicit Cutouts(not_null<QWidget*> parent) : RpWidget(parent) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground);
		style::PaletteChanged() | rpl::on_next([=] {
			update();
		}, lifetime());
	}

	void setShelves(std::vector<Shelf> shelves) {
		if (sameShelves(shelves)) {
			return;
		}
		_shelves = std::move(shelves);
		update();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		if (_shelves.empty()) {
			return;
		}
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		auto outside = QPainterPath();
		outside.addRect(rect());
		for (const auto &shelf : _shelves) {
			auto inside = QPainterPath();
			inside.addRoundedRect(QRectF(shelf.rect), kRadius, kRadius);
			outside = outside.subtracted(inside);
		}
		p.setPen(Qt::NoPen);
		// Replaces what is there with the translucent field, so the
		// blurred desktop shows through between the shelves.
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillPath(outside, FieldColor());
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		for (const auto &shelf : _shelves) {
			const auto path = RoundedPath(
				QRectF(shelf.rect).adjusted(0.5, 0.5, -0.5, -0.5),
				kRadius,
				kRadius,
				kRadius,
				kRadius);
			PaintSheen(p, shelf.rect, path);
			if (shelf.grain) {
				PaintGrain(p, shelf.rect, path);
			}
			PaintRim(p, path);
		}
	}

private:
	[[nodiscard]] bool sameShelves(const std::vector<Shelf> &other) const {
		if (_shelves.size() != other.size()) {
			return false;
		}
		for (auto i = size_t(); i != other.size(); ++i) {
			if (_shelves[i].rect != other[i].rect
				|| _shelves[i].grain != other[i].grain) {
				return false;
			}
		}
		return true;
	}

	std::vector<Shelf> _shelves;

};

} // namespace

QPainterPath RoundedPath(
		const QRectF &rect,
		double tl,
		double tr,
		double br,
		double bl) {
	auto path = QPainterPath();
	path.moveTo(rect.left() + tl, rect.top());
	path.lineTo(rect.right() - tr, rect.top());
	if (tr > 0.) {
		path.arcTo(rect.right() - 2 * tr, rect.top(), 2 * tr, 2 * tr, 90, -90);
	}
	path.lineTo(rect.right(), rect.bottom() - br);
	if (br > 0.) {
		path.arcTo(rect.right() - 2 * br, rect.bottom() - 2 * br, 2 * br, 2 * br, 0, -90);
	}
	path.lineTo(rect.left() + bl, rect.bottom());
	if (bl > 0.) {
		path.arcTo(rect.left(), rect.bottom() - 2 * bl, 2 * bl, 2 * bl, 270, -90);
	}
	path.lineTo(rect.left(), rect.top() + tl);
	if (tl > 0.) {
		path.arcTo(rect.left(), rect.top(), 2 * tl, 2 * tl, 180, -90);
	}
	path.closeSubpath();
	return path;
}

void PaintSheen(QPainter &p, const QRect &rect, const QPainterPath &path) {
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setClipPath(path, Qt::IntersectClip);
	// Flat frosted glass: one even milky veil over the body, no gradient
	// (a top-to-bottom sheen reads as the card darkening towards its foot).
	p.fillRect(rect, QColor(255, 255, 255, Dark() ? 10 : 46));
	p.restore();
}

void PaintGrain(QPainter &p, const QRect &rect, const QPainterPath &path) {
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setClipPath(path, Qt::IntersectClip);
	p.setOpacity(p.opacity()
		* (Dark() ? kGrainOpacity : kGrainOpacityLight));
	p.drawTiledPixmap(rect, GrainTile());
	p.restore();
}

void PaintRim(QPainter &p, const QPainterPath &path) {
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(RimColor(), 1.));
	p.drawPath(path);
	p.restore();
}

void PaintMaterial(QPainter &p, const QRect &rect, int radius) {
	const auto path = RoundedPath(
		QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5),
		radius,
		radius,
		radius,
		radius);
	PaintSheen(p, rect, path);
	PaintGrain(p, rect, path);
	PaintRim(p, path);
}

QColor GlassColor() {
	// The island's own body (Theme.qml, a chosen colour on dark glass:
	// hsla(hue, .11, .21-.25)), flat, at the middle of its two stops. The
	// island has KWin's blur behind it and lets a third through; inside the
	// chat there is only the quiet backdrop, so the body is denser to read
	// as the same frosted, milky glass.
	const auto hue = std::max(0.f, float(st::windowBgActive->c.hslHueF()));
	return Dark()
		? QColor::fromHslF(hue, 0.11f, 0.23f, 0.66f)
		: QColor::fromHslF(hue, 0.25f, 0.96f, 0.80f);
}

void PaintGlass(QPainter &p, const QRect &rect, int radius) {
	const auto path = RoundedPath(
		QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5),
		radius,
		radius,
		radius,
		radius);
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.fillPath(path, GlassColor());
	p.restore();
	PaintGrain(p, rect, path);
	PaintRim(p, path);
}

const style::FlatButton &GlassButton(const style::FlatButton &base) {
	static auto copies = base::flat_map<
		const style::FlatButton*,
		std::unique_ptr<style::FlatButton>>();
	auto &copy = copies[&base];
	if (!copy) {
		copy = std::make_unique<style::FlatButton>(base);
		copy->bgColor = st::transparent;
		copy->overBgColor = st::transparent;
	}
	return *copy;
}

QColor FieldColor() {
	// The same glass as the title bar and the island: a dark body at a third
	// of opacity or so, over what KWin blurs behind the window.
	const auto base = st::windowBg->c;
	const auto hue = std::max(0.f, float(base.hslHueF()));
	auto result = (base.lightnessF() < 0.5)
		? QColor::fromHslF(hue, 0.06f, 0.20f)
		: QColor::fromHslF(hue, 0.10f, 0.93f);
	result.setAlphaF((base.lightnessF() < 0.5) ? 0.58f : 0.62f);
	return result;
}

void RaiseShelves(not_null<QWidget*> host) {
	if (const auto cutouts = host->findChild<QWidget*>(
			u"aetherCutouts"_q,
			Qt::FindDirectChildrenOnly)) {
		cutouts->raise();
	}
}

void SetShelves(not_null<QWidget*> host, std::vector<Shelf> shelves) {
	// (Not findChild<Cutouts*>: that wants Q_OBJECT, which these widgets
	// do not use; the name is ours alone, so the cast is safe.)
	auto cutouts = static_cast<Cutouts*>(host->findChild<QWidget*>(
		u"aetherCutouts"_q,
		Qt::FindDirectChildrenOnly));
	if (!cutouts) {
		cutouts = new Cutouts(host);
		cutouts->setObjectName(u"aetherCutouts"_q);
		SetupWindowBlur(host->window());
	}
	cutouts->setGeometry(host->rect());
	cutouts->setShelves(std::move(shelves));
	cutouts->raise();
	cutouts->show();
}

} // namespace Aether
