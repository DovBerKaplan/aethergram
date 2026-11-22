/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stdafx.h"

#include "aether/aether_blur.h"

#include "base/platform/base_platform_info.h"

#include <QtCore/QObject>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <QtWidgets/QWidget>

#include <dlfcn.h>

#include <algorithm>
#include <cstring>
#include <memory>

extern "C" {

struct wl_proxy;
struct wl_display;
struct wl_registry;
struct wl_interface;

struct wl_message {
	const char *name;
	const char *signature;
	const struct wl_interface **types;
};

struct wl_interface {
	const char *name;
	int version;
	int method_count;
	const struct wl_message *methods;
	int event_count;
	const struct wl_message *events;
};

struct wl_registry_listener {
	void (*global)(
		void *data,
		wl_registry *registry,
		uint32_t name,
		const char *interface,
		uint32_t version);
	void (*global_remove)(void *data, wl_registry *registry, uint32_t name);
};

} // extern "C"

namespace Aether {
namespace {

constexpr auto kMarshalFlagDestroy = uint32_t(1);
constexpr auto kProtocolVersion = uint32_t(1);

struct Wayland {
	using Marshal = wl_proxy*(*)(
		wl_proxy*,
		uint32_t,
		const wl_interface*,
		uint32_t,
		uint32_t,
		...);
	Marshal marshal = nullptr;
	int (*addListener)(wl_proxy*, void(**)(void), void*) = nullptr;
	uint32_t (*version)(wl_proxy*) = nullptr;
	int (*roundtrip)(wl_display*) = nullptr;
	const wl_interface *registryInterface = nullptr;
	const wl_interface *surfaceInterface = nullptr;
	const wl_interface *regionInterface = nullptr;

	// ext-background-effect-v1 (staging, what KWin 6.8 offers in place of
	// org_kde_kwin_blur), spelled out: the tables its XML would generate.
	// They refer to wl_surface and wl_region, so they are made once the
	// library is loaded.
	wl_interface effectInterface = {};
	wl_interface managerInterface = {};
	const wl_interface *getTypes[2] = {};
	const wl_interface *regionTypes[1] = {};
	wl_message effectMethods[2] = {};
	wl_message managerMethods[2] = {};
	wl_message managerEvents[1] = {};

	[[nodiscard]] bool ok() const {
		return marshal
			&& addListener
			&& version
			&& roundtrip
			&& registryInterface
			&& surfaceInterface
			&& regionInterface;
	}
};

[[nodiscard]] Wayland &Symbols() {
	static auto result = [] {
		auto wayland = std::make_unique<Wayland>();
		if (const auto lib = dlopen("libwayland-client.so.0", RTLD_LAZY | RTLD_NODELETE)) {
			const auto load = [&](auto &where, const char *name) {
				where = reinterpret_cast<std::remove_reference_t<decltype(where)>>(dlsym(lib, name));
			};
			load(wayland->marshal, "wl_proxy_marshal_flags");
			load(wayland->addListener, "wl_proxy_add_listener");
			load(wayland->version, "wl_proxy_get_version");
			load(wayland->roundtrip, "wl_display_roundtrip");
			load(wayland->registryInterface, "wl_registry_interface");
			load(wayland->surfaceInterface, "wl_surface_interface");
			load(wayland->regionInterface, "wl_region_interface");
		}
		if (wayland->ok()) {
			auto &w = *wayland;
			w.getTypes[0] = &w.effectInterface;
			w.getTypes[1] = w.surfaceInterface;
			w.regionTypes[0] = w.regionInterface;
			w.effectMethods[0] = { "destroy", "", nullptr };
			w.effectMethods[1] = { "set_blur_region", "?o", w.regionTypes };
			w.effectInterface = {
				"ext_background_effect_surface_v1",
				1,
				2,
				w.effectMethods,
				0,
				nullptr };
			w.managerMethods[0] = { "destroy", "", nullptr };
			w.managerMethods[1] = {
				"get_background_effect", "no", w.getTypes };
			w.managerEvents[0] = { "capabilities", "u", nullptr };
			w.managerInterface = {
				"ext_background_effect_manager_v1",
				1,
				2,
				w.managerMethods,
				1,
				w.managerEvents };
		}
		return wayland;
	}();
	return *result;
}

struct Registry {
	uint32_t name = 0;
	uint32_t version = 0;
};

void OnGlobal(
		void *data,
		wl_registry *,
		uint32_t name,
		const char *interface,
		uint32_t version) {
	if (std::strcmp(interface, "ext_background_effect_manager_v1") == 0) {
		*static_cast<Registry*>(data) = { name, version };
	}
}

void OnGlobalRemove(void *, wl_registry *, uint32_t) {
}

constexpr wl_registry_listener kRegistryListener = {
	OnGlobal,
	OnGlobalRemove,
};

// The manager global, bound once for the display; null when there is none.
[[nodiscard]] wl_proxy *Manager(wl_display *display) {
	static auto bound = false;
	static wl_proxy *manager = nullptr;
	if (bound) {
		return manager;
	}
	bound = true;
	auto &w = Symbols();
	if (!w.ok() || !display) {
		return nullptr;
	}
	const auto registry = w.marshal(
		reinterpret_cast<wl_proxy*>(display),
		1, // wl_display.get_registry
		w.registryInterface,
		w.version(reinterpret_cast<wl_proxy*>(display)),
		0,
		nullptr);
	if (!registry) {
		return nullptr;
	}
	auto found = Registry();
	w.addListener(
		registry,
		reinterpret_cast<void(**)(void)>(
			const_cast<wl_registry_listener*>(&kRegistryListener)),
		&found);
	w.roundtrip(display);
	if (found.name) {
		const auto version = std::min(found.version, kProtocolVersion);
		manager = w.marshal(
			registry,
			0, // wl_registry.bind
			&w.managerInterface,
			version,
			0,
			found.name,
			w.managerInterface.name,
			version,
			nullptr);
	}
	return manager;
}

// Keeps one blur object on the window's surface, made again whenever the
// surface is (hiding and showing a window recreates it).
class WindowBlur final : public QObject {
public:
	explicit WindowBlur(not_null<QWidget*> widget)
	: QObject(widget)
	, _widget(widget) {
		widget->installEventFilter(this);
		apply();
	}

	~WindowBlur() {
		release();
	}

protected:
	bool eventFilter(QObject *watched, QEvent *event) override {
		switch (event->type()) {
		case QEvent::Show:
		case QEvent::WinIdChange:
		case QEvent::PlatformSurface:
		case QEvent::Resize:
			apply();
			break;
		default: break;
		}
		return QObject::eventFilter(watched, event);
	}

private:
	void apply() {
		const auto window = _widget->windowHandle();
		const auto native = qApp->nativeInterface<
			QNativeInterface::QWaylandApplication>();
		if (!window || !native || !_widget->isVisible()) {
			return;
		}
		const auto surface = reinterpret_cast<wl_proxy*>(_widget->winId());
		if (!surface) {
			return;
		}
		auto &w = Symbols();
		const auto manager = Manager(native->display());
		if (!manager) {
			return;
		}
		if (_surface != surface) {
			release();
			_surface = surface;
			_blur = w.marshal(
				manager,
				1, // ext_background_effect_manager_v1.get_background_effect
				&w.effectInterface,
				w.version(manager),
				0,
				nullptr,
				surface);
		}
		if (_blur) {
			// A region over the whole window (an empty one would mean no
			// effect); the shelves paint over it, the gaps let it through.
			const auto compositor = reinterpret_cast<wl_proxy*>(
				native->compositor());
			if (!compositor) {
				return;
			}
			const auto region = w.marshal(
				compositor,
				1, // wl_compositor.create_region
				w.regionInterface,
				w.version(compositor),
				0,
				nullptr);
			if (!region) {
				return;
			}
			w.marshal(
				region,
				1, // wl_region.add
				nullptr,
				w.version(region),
				0,
				0,
				0,
				_widget->width(),
				_widget->height());
			w.marshal(
				_blur,
				1, // ext_background_effect_surface_v1.set_blur_region
				nullptr,
				w.version(_blur),
				0,
				region);
			w.marshal(
				region,
				0, // wl_region.destroy
				nullptr,
				w.version(region),
				kMarshalFlagDestroy);
			_widget->update();
		}
	}

	void release() {
		if (_blur) {
			auto &w = Symbols();
			w.marshal(
				_blur,
				0, // ext_background_effect_surface_v1.destroy
				nullptr,
				w.version(_blur),
				kMarshalFlagDestroy);
			_blur = nullptr;
		}
		_surface = nullptr;
	}

	const not_null<QWidget*> _widget;
	wl_proxy *_surface = nullptr;
	wl_proxy *_blur = nullptr;

};

} // namespace

void SetupWindowBlur(not_null<QWidget*> window) {
	if (!::Platform::IsWayland() || !Symbols().ok()) {
		return;
	}
	if (window->findChild<QObject*>(
			u"aetherBlur"_q,
			Qt::FindDirectChildrenOnly)) {
		return;
	}
	(new WindowBlur(window))->setObjectName(u"aetherBlur"_q);
}

} // namespace Aether
