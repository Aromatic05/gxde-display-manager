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

#include "mediacontroller.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QUrl>

#include <algorithm>

namespace gxde {
namespace lock {
namespace {

const QString kMprisPrefix = QStringLiteral("org.mpris.MediaPlayer2.");
const QString kMprisPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayerInterface =
    QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kPropertiesInterface =
    QStringLiteral("org.freedesktop.DBus.Properties");

constexpr qint64 kMaxLengthInSeconds = 36000;

QVariant unwrap(const QVariant& value) {
  if (value.canConvert<QDBusVariant>() &&
      value.userType() == qMetaTypeId<QDBusVariant>()) {
    return unwrap(value.value<QDBusVariant>().variant());
  }
  if (value.userType() == qMetaTypeId<QDBusArgument>()) {
    const QDBusArgument argument = value.value<QDBusArgument>();
    switch (argument.currentType()) {
      case QDBusArgument::MapType:
        return qdbus_cast<QVariantMap>(argument);
      case QDBusArgument::ArrayType:
        return qdbus_cast<QStringList>(argument);
      default:
        break;
    }
  }
  return value;
}

QVariantMap unwrapMap(const QVariant& value) {
  const QVariant unwrapped = unwrap(value);
  QVariantMap map = unwrapped.toMap();
  for (auto it = map.begin(); it != map.end(); ++it) it.value() = unwrap(*it);
  return map;
}

QString artUrl(const QString& url) {
  const QUrl parsed(url);
  const QString scheme = parsed.scheme();
  if (parsed.isLocalFile() || scheme == QLatin1String("http") ||
      scheme == QLatin1String("https")) {
    return parsed.toString();
  }
  return QString();
}

QString objectPath(const QVariant& value) {
  if (value.userType() == qMetaTypeId<QDBusObjectPath>()) {
    return value.value<QDBusObjectPath>().path();
  }
  return value.toString();
}

}  // namespace


MprisPlayer::MprisPlayer(const QString& service, const QDBusConnection& bus,
                         QObject* parent)
    : QObject(parent), service_(service), bus_(bus) {
  bus_.connect(service_, kMprisPath, kPropertiesInterface,
               QStringLiteral("PropertiesChanged"), this,
               SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
  bus_.connect(service_, kMprisPath, kPlayerInterface,
               QStringLiteral("Seeked"), this, SLOT(onSeeked(qlonglong)));
  Refresh();
}

void MprisPlayer::RefreshPosition() {
  QDBusMessage message = QDBusMessage::createMethodCall(
      service_, kMprisPath, kPropertiesInterface, QStringLiteral("Get"));
  message << kPlayerInterface << QStringLiteral("Position");
  auto* watcher =
      new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this](QDBusPendingCallWatcher* call) {
            const QDBusPendingReply<QDBusVariant> reply = *call;
            call->deleteLater();
            if (reply.isError()) return;
            onSeeked(reply.value().variant().toLongLong());
          });
}

void MprisPlayer::onSeeked(qlonglong position) {
  const qint64 position_us = legacy_units_ ? position * 1000 : position;
  if (position_us == position_us_) return;
  position_us_ = position_us;
  emit PositionChanged();
}

void MprisPlayer::SetPosition(qint64 position_us) {
  if (track_id_.isEmpty()) return;
  QDBusMessage message = QDBusMessage::createMethodCall(
      service_, kMprisPath, kPlayerInterface, QStringLiteral("SetPosition"));
  const qint64 clamped = std::clamp<qint64>(position_us, 0, length_us_);
  message << QVariant::fromValue(QDBusObjectPath(track_id_))
          << static_cast<qlonglong>(legacy_units_ ? clamped / 1000 : clamped);
  bus_.asyncCall(message);
  // Show the new position right away; the player confirms with Seeked.
  if (clamped != position_us_) {
    position_us_ = clamped;
    emit PositionChanged();
  }
}

void MprisPlayer::Refresh() {
  QDBusMessage message = QDBusMessage::createMethodCall(
      service_, kMprisPath, kPropertiesInterface, QStringLiteral("GetAll"));
  message << kPlayerInterface;
  auto* watcher =
      new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this](QDBusPendingCallWatcher* call) {
            const QDBusPendingReply<QVariantMap> reply = *call;
            call->deleteLater();
            if (reply.isError()) return;
            ApplyProperties(reply.value());
          });
}

void MprisPlayer::Call(const QString& method) {
  bus_.asyncCall(QDBusMessage::createMethodCall(service_, kMprisPath,
                                                kPlayerInterface, method));
}

void MprisPlayer::SetVolume(double volume) {
  QDBusMessage message = QDBusMessage::createMethodCall(
      service_, kMprisPath, kPropertiesInterface, QStringLiteral("Set"));
  message << kPlayerInterface << QStringLiteral("Volume")
          << QVariant::fromValue(QDBusVariant(std::clamp(volume, 0.0, 1.0)));
  bus_.asyncCall(message);
}

void MprisPlayer::ApplyProperties(const QVariantMap& raw) {
  const QVariantMap properties = unwrapMap(raw);
  bool changed = false;
  auto set = [&changed](auto& field, const auto& value) {
    if (field == value) return;
    field = value;
    changed = true;
  };

  if (properties.contains(QStringLiteral("Metadata"))) {
    const QVariantMap metadata =
        unwrapMap(properties.value(QStringLiteral("Metadata")));
    set(title_, metadata.value(QStringLiteral("xesam:title")).toString());
    set(artist_, unwrap(metadata.value(QStringLiteral("xesam:artist")))
                     .toStringList()
                     .join(QStringLiteral(", ")));
    set(album_, metadata.value(QStringLiteral("xesam:album")).toString());
    set(art_url_,
        artUrl(metadata.value(QStringLiteral("mpris:artUrl")).toString()));
    const QString track_id =
        objectPath(metadata.value(QStringLiteral("mpris:trackid")));
    // A new track starts elsewhere; polling only runs while playing.
    if (track_id != track_id_) RefreshPosition();
    set(track_id_, track_id);
    qint64 length = metadata.value(QStringLiteral("mpris:length")).toLongLong();
    const bool legacy = length > 0 && length < kMaxLengthInSeconds;
    // The cached position was read in the other units.
    if (legacy != legacy_units_) RefreshPosition();
    set(legacy_units_, legacy);
    if (legacy_units_) length *= 1000000;
    set(length_us_, length);
  }
  if (properties.contains(QStringLiteral("Position"))) {
    onSeeked(properties.value(QStringLiteral("Position")).toLongLong());
  }
  if (properties.contains(QStringLiteral("CanSeek"))) {
    set(can_seek_, properties.value(QStringLiteral("CanSeek")).toBool());
  }
  if (properties.contains(QStringLiteral("PlaybackStatus"))) {
    set(playback_status_,
        properties.value(QStringLiteral("PlaybackStatus")).toString());
  }
  if (properties.contains(QStringLiteral("CanGoNext"))) {
    set(can_go_next_, properties.value(QStringLiteral("CanGoNext")).toBool());
  }
  if (properties.contains(QStringLiteral("CanGoPrevious"))) {
    set(can_go_previous_,
        properties.value(QStringLiteral("CanGoPrevious")).toBool());
  }
  if (properties.contains(QStringLiteral("CanPlay"))) {
    set(can_play_, properties.value(QStringLiteral("CanPlay")).toBool());
  }
  if (properties.contains(QStringLiteral("CanPause"))) {
    set(can_pause_, properties.value(QStringLiteral("CanPause")).toBool());
  }
  if (properties.contains(QStringLiteral("Volume"))) {
    set(has_volume_, true);
    set(volume_, properties.value(QStringLiteral("Volume")).toDouble());
  }

  if (changed) emit Changed();
}

void MprisPlayer::onPropertiesChanged(const QString& interface,
                                      const QVariantMap& changed,
                                      const QStringList& invalidated) {
  if (interface != kPlayerInterface) return;
  ApplyProperties(changed);
  // Some players only invalidate; fetch the new values.
  if (!invalidated.isEmpty()) Refresh();
}

MediaController::MediaController(QObject* parent)
    : MediaController(QDBusConnection::sessionBus(), true, parent) {}

MediaController::MediaController(const QDBusConnection& bus, bool scan,
                                 QObject* parent)
    : QObject(parent), bus_(bus) {
  position_timer_.setInterval(1000);
  connect(&position_timer_, &QTimer::timeout, this, [this] {
    if (MprisPlayer* player = active()) player->RefreshPosition();
  });
  bus_.connect(QStringLiteral("org.freedesktop.DBus"),
               QStringLiteral("/org/freedesktop/DBus"),
               QStringLiteral("org.freedesktop.DBus"),
               QStringLiteral("NameOwnerChanged"), this,
               SLOT(onNameOwnerChanged(QString, QString, QString)));
  if (scan) scanExistingPlayers();
}

void MediaController::scanExistingPlayers() {
  QDBusMessage message = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.DBus"),
      QStringLiteral("/org/freedesktop/DBus"),
      QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListNames"));
  auto* watcher =
      new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this](QDBusPendingCallWatcher* call) {
            const QDBusPendingReply<QStringList> reply = *call;
            call->deleteLater();
            if (reply.isError()) return;
            for (const QString& name : reply.value()) {
              if (name.startsWith(kMprisPrefix)) AddPlayer(name);
            }
          });
}

void MediaController::onNameOwnerChanged(const QString& name,
                                         const QString& oldOwner,
                                         const QString& newOwner) {
  Q_UNUSED(oldOwner)
  if (!name.startsWith(kMprisPrefix)) return;
  if (newOwner.isEmpty()) {
    RemovePlayer(name);
  } else {
    AddPlayer(name);
  }
}

void MediaController::AddPlayer(const QString& service) {
  if (players_.contains(service)) return;
  auto* player = new MprisPlayer(service, bus_, this);
  connect(player, &MprisPlayer::Changed, this,
          &MediaController::onPlayerChanged);
  connect(player, &MprisPlayer::PositionChanged, this,
          &MediaController::onPlayerPositionChanged);
  players_.insert(service, player);
  order_ << service;
  selectActive();
  emit Changed();
}

void MediaController::onPlayerPositionChanged() {
  if (sender() == active()) emit PositionChanged();
}

void MediaController::updatePositionPolling() {
  MprisPlayer* player = active();
  const bool playing =
      player && player->PlaybackStatus() == QLatin1String("Playing");
  if (playing == position_timer_.isActive()) return;
  if (playing) {
    player->RefreshPosition();
    position_timer_.start();
  } else {
    position_timer_.stop();
  }
}

void MediaController::RemovePlayer(const QString& service) {
  MprisPlayer* player = players_.take(service);
  if (!player) return;
  order_.removeAll(service);
  player->deleteLater();
  if (active_ == service) active_.clear();
  selectActive();
  emit Changed();
  emit PositionChanged();
}

MprisPlayer* MediaController::Player(const QString& service) const {
  return players_.value(service);
}

void MediaController::onPlayerChanged() {
  auto* player = qobject_cast<MprisPlayer*>(sender());
  const QString before = active_;
  // A player that starts playing takes over the indicator.
  if (player && player->PlaybackStatus() == QLatin1String("Playing")) {
    active_ = player->Service();
  }
  selectActive();
  emit Changed();
  if (active_ != before) emit PositionChanged();
}

void MediaController::selectActive() {
  MprisPlayer* current = active();
  if (!current || current->PlaybackStatus() != QLatin1String("Playing")) {
    for (const QString& service : order_) {
      if (players_.value(service)->PlaybackStatus() ==
          QLatin1String("Playing")) {
        active_ = service;
        break;
      }
    }
  }
  if (!players_.contains(active_)) {
    active_ = order_.isEmpty() ? QString() : order_.first();
  }
  updatePositionPolling();
}

MprisPlayer* MediaController::active() const {
  return players_.value(active_);
}

QString MediaController::ActiveService() const { return active_; }

bool MediaController::Available() const {
  const MprisPlayer* player = active();
  // A player with nothing loaded has nothing worth showing.
  return player && (!player->Title().isEmpty() ||
                    player->PlaybackStatus() == QLatin1String("Playing"));
}

QString MediaController::Title() const {
  return active() ? active()->Title() : QString();
}

QString MediaController::Artist() const {
  return active() ? active()->Artist() : QString();
}

QString MediaController::Album() const {
  return active() ? active()->Album() : QString();
}

QString MediaController::ArtUrl() const {
  return active() ? active()->ArtUrl() : QString();
}

double MediaController::Length() const {
  return active() ? active()->LengthUs() / 1000.0 : 0.0;
}

double MediaController::Position() const {
  if (!active()) return 0.0;
  const double position = active()->PositionUs() / 1000.0;
  return Length() > 0 ? std::clamp(position, 0.0, Length()) : position;
}

bool MediaController::CanSeek() const {
  return active() && active()->CanSeek() && Length() > 0 &&
         !active()->TrackId().isEmpty();
}

void MediaController::Seek(double position_ms) {
  if (CanSeek()) {
    active()->SetPosition(static_cast<qint64>(position_ms * 1000.0));
  }
}

bool MediaController::Playing() const {
  return active() &&
         active()->PlaybackStatus() == QLatin1String("Playing");
}

bool MediaController::CanGoNext() const {
  return active() && active()->CanGoNext();
}

bool MediaController::CanGoPrevious() const {
  return active() && active()->CanGoPrevious();
}

bool MediaController::CanPlayPause() const {
  return active() && (active()->CanPlay() || active()->CanPause());
}

bool MediaController::HasVolume() const {
  return active() && active()->HasVolume();
}

double MediaController::Volume() const {
  return active() ? active()->Volume() : 0.0;
}

void MediaController::PlayPause() {
  if (CanPlayPause()) active()->Call(QStringLiteral("PlayPause"));
}

void MediaController::Next() {
  if (CanGoNext()) active()->Call(QStringLiteral("Next"));
}

void MediaController::Previous() {
  if (CanGoPrevious()) active()->Call(QStringLiteral("Previous"));
}

void MediaController::SetVolume(double volume) {
  if (HasVolume()) active()->SetVolume(volume);
}

void MediaController::ToggleMute() {
  if (!HasVolume()) return;
  const double current = active()->Volume();
  if (current > 0.0) {
    last_volume_ = current;
    active()->SetVolume(0.0);
  } else {
    active()->SetVolume(last_volume_ > 0.0 ? last_volume_ : 0.5);
  }
}

}  // namespace lock
}  // namespace gxde
