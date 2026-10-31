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

#include "FingerprintProbe.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QTimer>

namespace gxde {
namespace {

const QString kFprintService = QStringLiteral("net.reactivated.Fprint");
const QString kManagerPath = QStringLiteral("/net/reactivated/Fprint/Manager");
const QString kManagerInterface =
    QStringLiteral("net.reactivated.Fprint.Manager");
const QString kDeviceInterface =
    QStringLiteral("net.reactivated.Fprint.Device");

// Where PAM reads service files (Linux-PAM, plus its vendor directory, and
// OpenPAM on FreeBSD).
bool FingerprintServiceInstalled() {
  for (const char* dir : {"/etc/pam.d/", "/usr/lib/pam.d/",
                          "/usr/local/etc/pam.d/"}) {
    if (QFile::exists(QLatin1String(dir) +
                      QLatin1String("gxdm-fingerprint"))) {
      return true;
    }
  }
  return false;
}

}  // namespace

void ProbeEnrolledFingers(const QString& user, QObject* context,
                          std::function<void(bool available)> done) {
  if (!FingerprintServiceInstalled()) {
    // Runs would fail on PAM's "other" service; don't offer the finger.
    qDebug() << "(Fingerprint) no gxdm-fingerprint PAM service";
    QTimer::singleShot(0, context, [done] { done(false); });
    return;
  }
  QDBusConnection bus = QDBusConnection::systemBus();
  if (!bus.isConnected()) {
    // Calls on a dead connection never report back.
    qDebug() << "(Fingerprint) no system bus";
    QTimer::singleShot(0, context, [done] { done(false); });
    return;
  }
  // Manager.GetDefaultDevice, then Device.ListEnrolledFingers(user). Either
  // fails (NoSuchDevice, NoEnrolledPrints, no fprintd) when there is nothing
  // to listen with.
  const QDBusMessage get_device = QDBusMessage::createMethodCall(
      kFprintService, kManagerPath, kManagerInterface,
      QStringLiteral("GetDefaultDevice"));
  auto* watcher =
      new QDBusPendingCallWatcher(bus.asyncCall(get_device), context);
  QObject::connect(
      watcher, &QDBusPendingCallWatcher::finished, context,
      [user, context, done, bus](QDBusPendingCallWatcher* call) mutable {
        const QDBusPendingReply<QDBusObjectPath> device = *call;
        call->deleteLater();
        if (device.isError()) {
          qDebug() << "(Fingerprint) no fingerprint reader:"
                   << device.error().name();
          done(false);
          return;
        }
        QDBusMessage list = QDBusMessage::createMethodCall(
            kFprintService, device.value().path(), kDeviceInterface,
            QStringLiteral("ListEnrolledFingers"));
        list << user;
        auto* next = new QDBusPendingCallWatcher(bus.asyncCall(list), context);
        QObject::connect(next, &QDBusPendingCallWatcher::finished, context,
                         [done](QDBusPendingCallWatcher* reply_call) {
                           const QDBusPendingReply<QStringList> fingers =
                               *reply_call;
                           reply_call->deleteLater();
                           if (fingers.isError()) {
                             qDebug() << "(Fingerprint) no enrolled fingers:"
                                      << fingers.error().name();
                           }
                           done(!fingers.isError() &&
                                !fingers.value().isEmpty());
                         });
      });
}

}  // namespace gxde
