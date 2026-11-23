/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QObject>
#include <QtCore/QVariantMap>

#include "base/basic_types.h"
#include <rpl/lifetime.h>

namespace Aether {

// Aethergram says what it is doing on the session bus, so the island and the
// dock can show it without polling (docs/THEME-BUS.md, "Client side"):
// org.aether.Aethergram, /Aether, org.aether.Client1.
class ClientBus final : public QObject {
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.aether.Client1")
	Q_PROPERTY(int Unread READ unread)
	Q_PROPERTY(int UnreadMuted READ unreadMuted)

public:
	ClientBus();

	[[nodiscard]] int unread() const;
	[[nodiscard]] int unreadMuted() const;

public Q_SLOTS:
	void Show();
	void Hide();

Q_SIGNALS:
	void StateChanged(const QVariantMap &state);

private:
	void publish();
	[[nodiscard]] QVariantMap state() const;

	QVariantMap _last;
	rpl::lifetime _lifetime;

};

// Claims the bus name and exports the object; does nothing off the session
// bus. Called once, after the application has its domain.
void StartClientBus();

} // namespace Aether
