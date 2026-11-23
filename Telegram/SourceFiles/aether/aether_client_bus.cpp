/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stdafx.h"

#include "aether/aether_client_bus.h"

#include "core/application.h"
#include "window/window_controller.h"
#include "mainwindow.h"

#include <QtDBus/QDBusConnection>
#include <QtCore/QTimer>

namespace Aether {

ClientBus::ClientBus() {
	Core::App().unreadBadgeChanges(
	) | rpl::on_next([=] {
		publish();
	}, _lifetime);
}

int ClientBus::unread() const {
	return Core::App().unreadBadgeMuted() ? 0 : Core::App().unreadBadge();
}

int ClientBus::unreadMuted() const {
	return Core::App().unreadBadgeMuted() ? Core::App().unreadBadge() : 0;
}

QVariantMap ClientBus::state() const {
	return {
		{ u"Unread"_q, unread() },
		{ u"UnreadMuted"_q, unreadMuted() },
	};
}

void ClientBus::publish() {
	const auto now = state();
	if (now != _last) {
		_last = now;
		Q_EMIT StateChanged(now);
	}
}

void ClientBus::Show() {
	Core::App().activate();
}

void ClientBus::Hide() {
	if (const auto window = Core::App().activePrimaryWindow()) {
		window->widget()->hide();
	}
}

void StartClientBus() {
	auto bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()
		|| !bus.registerService(
			u"org.aether.Aethergram"_q)) {
		return;
	}
	const auto client = new ClientBus();
	bus.registerObject(
		u"/Aether"_q,
		client,
		QDBusConnection::ExportAllProperties
			| QDBusConnection::ExportAllSlots
			| QDBusConnection::ExportAllSignals);
}

} // namespace Aether
