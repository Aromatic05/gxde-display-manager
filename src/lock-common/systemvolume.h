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

#ifndef SRC_LOCK_COMMON_SYSTEMVOLUME_H_
#define SRC_LOCK_COMMON_SYSTEMVOLUME_H_

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace gxde {
namespace lock {

class SystemVolume : public QObject {
  Q_OBJECT

  Q_PROPERTY(bool available READ Available NOTIFY Changed)
  Q_PROPERTY(double volume READ Volume NOTIFY Changed)
  Q_PROPERTY(double maxVolume READ MaxVolume NOTIFY Changed)
  Q_PROPERTY(bool muted READ Muted NOTIFY Changed)

 public:
  explicit SystemVolume(QObject* parent = nullptr);
  explicit SystemVolume(const QDBusConnection& bus, QObject* parent = nullptr);

  bool Available() const { return !sink_.isEmpty(); }
  double Volume() const { return volume_; }
  double MaxVolume() const;
  bool Muted() const { return muted_; }

  Q_INVOKABLE void SetVolume(double volume);
  Q_INVOKABLE void SetMuted(bool muted);
  Q_INVOKABLE void ToggleMute() { SetMuted(!muted_); }

 signals:
  void Changed();

 private slots:
  void onNameOwnerChanged(const QString& name, const QString& oldOwner,
                          const QString& newOwner);
  void onAudioPropertiesChanged(const QString& interface,
                                const QVariantMap& changed,
                                const QStringList& invalidated);
  void onSinkPropertiesChanged(const QString& interface,
                               const QVariantMap& changed,
                               const QStringList& invalidated);

 private:
  void connectService();
  void disconnectService();
  void setSink(const QString& path);
  void applySinkProperties(const QVariantMap& properties);

  QDBusConnection bus_;
  QString sink_;
  double volume_ = 0.0;
  double max_ui_volume_ = 1.0;
  bool muted_ = false;
  bool audio_connected_ = false;
};

}  // namespace lock
}  // namespace gxde

#endif  // SRC_LOCK_COMMON_SYSTEMVOLUME_H_
