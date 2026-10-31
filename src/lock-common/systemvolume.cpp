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

#include "systemvolume.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

#include <algorithm>

namespace gxde {
namespace lock {
namespace {

const QString kService = QStringLiteral("com.deepin.daemon.Audio");
const QString kAudioPath = QStringLiteral("/com/deepin/daemon/Audio");
const QString kAudioInterface = QStringLiteral("com.deepin.daemon.Audio");
const QString kSinkInterface = QStringLiteral("com.deepin.daemon.Audio.Sink");
const QString kPropertiesInterface =
    QStringLiteral("org.freedesktop.DBus.Properties");

QVariant unwrap(const QVariant& value) {
  if (value.userType() == qMetaTypeId<QDBusVariant>()) {
    return value.value<QDBusVariant>().variant();
  }
  return value;
}

QString objectPath(const QVariant& value) {
  const QVariant unwrapped = unwrap(value);
  if (unwrapped.userType() == qMetaTypeId<QDBusObjectPath>()) {
    return unwrapped.value<QDBusObjectPath>().path();
  }
  return unwrapped.toString();
}

}  // namespace

SystemVolume::SystemVolume(QObject* parent)
    : SystemVolume(QDBusConnection::sessionBus(), parent) {}

SystemVolume::SystemVolume(const QDBusConnection& bus, QObject* parent)
    : QObject(parent), bus_(bus) {
  bus_.connect(QStringLiteral("org.freedesktop.DBus"),
               QStringLiteral("/org/freedesktop/DBus"),
               QStringLiteral("org.freedesktop.DBus"),
               QStringLiteral("NameOwnerChanged"), this,
               SLOT(onNameOwnerChanged(QString, QString, QString)));
  connectService();
}

double SystemVolume::MaxVolume() const {
  return volume_ > 1.0 ? std::max(max_ui_volume_, volume_) : 1.0;
}

void SystemVolume::onNameOwnerChanged(const QString& name,
                                      const QString& oldOwner,
                                      const QString& newOwner) {
  Q_UNUSED(oldOwner)
  if (name != kService) return;
  if (newOwner.isEmpty()) {
    disconnectService();
  } else {
    connectService();
  }
}

void SystemVolume::connectService() {
  if (!audio_connected_) {
    audio_connected_ = bus_.connect(
        kService, kAudioPath, kPropertiesInterface,
        QStringLiteral("PropertiesChanged"), this,
        SLOT(onAudioPropertiesChanged(QString, QVariantMap, QStringList)));
  }
  QDBusMessage message = QDBusMessage::createMethodCall(
      kService, kAudioPath, kPropertiesInterface, QStringLiteral("GetAll"));
  message << kAudioInterface;
  message.setAutoStartService(false);
  auto* watcher = new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this](QDBusPendingCallWatcher* call) {
            const QDBusPendingReply<QVariantMap> reply = *call;
            call->deleteLater();
            // No dde-daemon: stay unavailable. The lock screen does not
            // auto-start it.
            if (reply.isError()) return;
            onAudioPropertiesChanged(kAudioInterface, reply.value(), {});
          });
}

void SystemVolume::disconnectService() {
  setSink(QString());
}

void SystemVolume::onAudioPropertiesChanged(const QString& interface,
                                            const QVariantMap& changed,
                                            const QStringList& invalidated) {
  Q_UNUSED(invalidated)
  if (interface != kAudioInterface) return;
  if (changed.contains(QStringLiteral("MaxUIVolume"))) {
    max_ui_volume_ =
        unwrap(changed.value(QStringLiteral("MaxUIVolume"))).toDouble();
    emit Changed();
  }
  if (changed.contains(QStringLiteral("DefaultSink"))) {
    setSink(objectPath(changed.value(QStringLiteral("DefaultSink"))));
  }
}

void SystemVolume::setSink(const QString& path) {
  // dde-daemon reports "/" while no output device exists.
  const QString sink = path == QLatin1String("/") ? QString() : path;
  if (sink == sink_) return;
  if (!sink_.isEmpty()) {
    bus_.disconnect(
        kService, sink_, kPropertiesInterface,
        QStringLiteral("PropertiesChanged"), this,
        SLOT(onSinkPropertiesChanged(QString, QVariantMap, QStringList)));
  }
  sink_ = sink;
  if (sink_.isEmpty()) {
    emit Changed();
    return;
  }
  bus_.connect(kService, sink_, kPropertiesInterface,
               QStringLiteral("PropertiesChanged"), this,
               SLOT(onSinkPropertiesChanged(QString, QVariantMap, QStringList)));
  QDBusMessage message = QDBusMessage::createMethodCall(
      kService, sink_, kPropertiesInterface, QStringLiteral("GetAll"));
  message << kSinkInterface;
  const QString requested = sink_;
  auto* watcher = new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, requested](QDBusPendingCallWatcher* call) {
            const QDBusPendingReply<QVariantMap> reply = *call;
            call->deleteLater();
            if (reply.isError() || requested != sink_) return;
            applySinkProperties(reply.value());
          });
  emit Changed();
}

void SystemVolume::onSinkPropertiesChanged(const QString& interface,
                                           const QVariantMap& changed,
                                           const QStringList& invalidated) {
  Q_UNUSED(invalidated)
  if (interface != kSinkInterface) return;
  applySinkProperties(changed);
}

void SystemVolume::applySinkProperties(const QVariantMap& properties) {
  bool changed = false;
  if (properties.contains(QStringLiteral("Volume"))) {
    const double volume =
        unwrap(properties.value(QStringLiteral("Volume"))).toDouble();
    changed |= volume != volume_;
    volume_ = volume;
  }
  if (properties.contains(QStringLiteral("Mute"))) {
    const bool muted =
        unwrap(properties.value(QStringLiteral("Mute"))).toBool();
    changed |= muted != muted_;
    muted_ = muted;
  }
  if (changed) emit Changed();
}

void SystemVolume::SetVolume(double volume) {
  if (sink_.isEmpty()) return;
  const double clamped = std::clamp(volume, 0.0, MaxVolume());
  QDBusMessage message = QDBusMessage::createMethodCall(
      kService, sink_, kSinkInterface, QStringLiteral("SetVolume"));
  // Second argument: play dde-daemon's feedback sound. Silent on the lock
  // screen.
  message << clamped << false;
  bus_.asyncCall(message);
  const bool unmute = muted_ && clamped > 0.0;
  // Show the new value right away; dde-daemon confirms with
  // PropertiesChanged. Otherwise a released slider snaps back to the old
  // value until the confirmation arrives.
  if (clamped != volume_) {
    volume_ = clamped;
    emit Changed();
  }
  // Raising the volume also unmutes, as the dock's volume slider does.
  if (unmute) SetMuted(false);
}

void SystemVolume::SetMuted(bool muted) {
  if (sink_.isEmpty()) return;
  QDBusMessage message = QDBusMessage::createMethodCall(
      kService, sink_, kSinkInterface, QStringLiteral("SetMute"));
  message << muted;
  bus_.asyncCall(message);
  if (muted != muted_) {
    muted_ = muted;
    emit Changed();
  }
}

}  // namespace lock
}  // namespace gxde
