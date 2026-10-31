/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "notificationcounter.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace gxde {
namespace lock {
namespace {

const QString kService = QStringLiteral("com.deepin.dde.Notification");
const QString kPath = QStringLiteral("/com/deepin/dde/Notification");
const QString kInterface = QStringLiteral("com.deepin.dde.Notification");

}  // namespace

NotificationCounter::NotificationCounter(QObject* parent)
    : NotificationCounter(QDBusConnection::sessionBus(), parent) {}

NotificationCounter::NotificationCounter(const QDBusConnection& bus,
                                         QObject* parent)
    : QObject(parent), bus_(bus) {
  bus_.connect(QStringLiteral("org.freedesktop.DBus"),
               QStringLiteral("/org/freedesktop/DBus"),
               QStringLiteral("org.freedesktop.DBus"),
               QStringLiteral("NameOwnerChanged"), this,
               SLOT(onNameOwnerChanged(QString, QString, QString)));
  // Subscribed by name, so the match survives dde-osd restarts.
  bus_.connect(kService, kPath, kInterface, QStringLiteral("RecordAdded"),
               this, SLOT(onRecordAdded(QString)));

  QDBusMessage message = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.DBus"),
      QStringLiteral("/org/freedesktop/DBus"),
      QStringLiteral("org.freedesktop.DBus"),
      QStringLiteral("NameHasOwner"));
  message << kService;
  auto* watcher = new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this](QDBusPendingCallWatcher* call) {
            const QDBusPendingReply<bool> reply = *call;
            call->deleteLater();
            if (!reply.isError() && reply.value()) setAvailable(true);
          });
}

void NotificationCounter::Reset() {
  if (count_ == 0) return;
  count_ = 0;
  emit Changed();
}

void NotificationCounter::onNameOwnerChanged(const QString& name,
                                             const QString& oldOwner,
                                             const QString& newOwner) {
  Q_UNUSED(oldOwner)
  if (name == kService) setAvailable(!newOwner.isEmpty());
}

void NotificationCounter::onRecordAdded(const QString& record) {
  Q_UNUSED(record)  // content is deliberately not kept
  if (!available_) setAvailable(true);
  ++count_;
  emit Changed();
}

void NotificationCounter::setAvailable(bool available) {
  if (available == available_) return;
  available_ = available;
  emit Changed();
}

}  // namespace lock
}  // namespace gxde
