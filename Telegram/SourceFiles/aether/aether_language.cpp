/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stdafx.h"

// Aethergram: follow the Aether desktop language + right-to-left layout.

#include "aether/aether_language.h"

#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "settings.h"
#include "ui/style/style_core_direction.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>

namespace Aether {
namespace {

// The Aether desktop writes its config as a KDE-style ini file here.
constexpr auto kRcFileName = "aetherislandrc";

// We remember the last Aether desktop language we followed here, so a
// language picked inside Aethergram is respected until Aether changes.
constexpr auto kStateFileName = "tdata/aether_language";

// The rc file is replaced atomically on every change, so the watcher is
// dropped; give the new file a moment to appear before re-adding it.
constexpr auto kWatchDebounceMs = 500;

[[nodiscard]] QString RcFilePath() {
	return QStandardPaths::writableLocation(
		QStandardPaths::GenericConfigLocation) + '/' + kRcFileName;
}

[[nodiscard]] QString StateFilePath() {
	return cWorkingDir() + kStateFileName;
}

// "he-IL" -> "he", "en_US" -> "en".
[[nodiscard]] QString BaseOfLanguage(const QString &id) {
	auto result = id.trimmed().toLower();
	auto end = result.size();
	for (const auto ch : { '-', '_' }) {
		if (const auto at = result.indexOf(ch); at >= 0 && at < end) {
			end = at;
		}
	}
	return result.left(end);
}

[[nodiscard]] QString ReadStateFile() {
	QFile file(StateFilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	return QString::fromUtf8(file.readAll()).trimmed();
}

// Write the state file first: if switching the language fails we must not
// retry on every start (that would loop restarts when logged in).
[[nodiscard]] bool WriteStateFile(const QString &value) {
	const auto path = StateFilePath();
	QDir().mkpath(path.left(path.lastIndexOf('/')));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		LOG(("Aethergram: could not write language state file %1"
			).arg(path));
		return false;
	}
	file.write(value.toUtf8());
	return true;
}

// Cloud pack ids of the languages we map to a supported pack ourselves.
[[nodiscard]] QString TargetLanguage(const QString &id) {
	const auto base = BaseOfLanguage(id);
	// "iw" is the legacy Qt/Java code for Hebrew, Telegram uses "he".
	if (base == u"iw"_q) {
		return u"he"_q;
	}
	return id.toLower();
}

void EnsureLayoutFollowsLanguage() {
	// A live language switch (only possible before login, afterwards
	// upstream restarts the app) must also update the layout direction.
	static auto lifetime = rpl::lifetime();
	static auto subscribed = false;
	if (subscribed) {
		return;
	}
	subscribed = true;
	Lang::GetInstance().idChanges(
	) | rpl::on_next([] {
		ApplyLayoutDirection();
	}, lifetime);
}

void EnsureWatcher() {
	static auto watcher = (QFileSystemWatcher*)nullptr;
	const auto path = RcFilePath();
	if (!watcher) {
		watcher = new QFileSystemWatcher(QCoreApplication::instance());
		QObject::connect(
			watcher,
			&QFileSystemWatcher::fileChanged,
			watcher,
			[] {
				static auto pending = false;
				if (pending) {
					return;
				}
				pending = true;
				QTimer::singleShot(kWatchDebounceMs, [] {
					pending = false;
					// FollowDesktopLanguage() ends in EnsureWatcher(),
					// which re-adds the path after an atomic replace.
					FollowDesktopLanguage();
				});
			});
	}
	if (QFile::exists(path) && !watcher->files().contains(path)) {
		watcher->addPath(path);
	}
}

} // namespace

bool IsRightToLeftLanguage(const QString &id) {
	const auto base = BaseOfLanguage(id);
	return base == u"he"_q
		|| base == u"iw"_q
		|| base == u"ar"_q
		|| base == u"fa"_q
		|| base == u"ur"_q
		|| base == u"yi"_q;
}

void ApplyLayoutDirection() {
	const auto forced = qEnvironmentVariable("AETHERGRAM_LAYOUT")
		.trimmed().toLower();
	auto rtl = IsRightToLeftLanguage(Lang::GetInstance().id());
	if (forced == u"ltr"_q) {
		rtl = false;
	} else if (forced == u"rtl"_q) {
		rtl = true;
	}
	style::SetRightToLeft(rtl);
	QGuiApplication::setLayoutDirection(
		rtl ? Qt::RightToLeft : Qt::LeftToRight);
	LOG(("Aethergram: layout direction %1 for language '%2' (override '%3')"
		).arg(rtl ? u"rtl"_q : u"ltr"_q, Lang::GetInstance().id(), forced));
}

QString DesktopLanguage() {
	const auto path = RcFilePath();
	if (!QFile::exists(path)) {
		return QString();
	}
	// Read-only: we only ever call value(), never setValue()/sync().
	QSettings settings(path, QSettings::IniFormat);
	settings.beginGroup(u"Interface"_q);
	const auto value = settings.value(u"Language"_q).toString();
	settings.endGroup();
	return value.trimmed().toLower();
}

void FollowDesktopLanguage() {
	const auto desktop = DesktopLanguage();
	if (desktop.isEmpty()) {
		// Aether is not running (or has no language set): nothing to follow.
		EnsureWatcher();
		return;
	}
	if (desktop == ReadStateFile()) {
		// Already following this language, so whatever the user picked in
		// Aethergram's Settings stays theirs.
		EnsureWatcher();
		return;
	}
	if (!WriteStateFile(desktop)) {
		EnsureWatcher();
		return;
	}
	const auto target = TargetLanguage(desktop);
	// The built-in English has an empty pack id.
	const auto current = Lang::GetInstance().id();
	const auto currentBase = current.isEmpty()
		? u"en"_q
		: BaseOfLanguage(current);
	if (currentBase == BaseOfLanguage(target)) {
		LOG(("Aethergram: Aether language '%1' already active"
			).arg(desktop));
		EnsureWatcher();
		return;
	}
	LOG(("Aethergram: following Aether language '%1', switching to '%2'"
		).arg(desktop, target));
	EnsureLayoutFollowsLanguage();
	// Downloads and applies a cloud pack; when logged in this restarts the
	// app, otherwise it is applied live.
	Lang::CurrentCloudManager().switchToLanguage(target);
	EnsureWatcher();
}

} // namespace Aether
