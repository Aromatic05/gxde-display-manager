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

#ifndef SRC_LOCK_COMMON_MEDIACONTROLLER_H_
#define SRC_LOCK_COMMON_MEDIACONTROLLER_H_

#include <QDBusConnection>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace gxde {
namespace lock {

class MprisPlayer : public QObject {
  Q_OBJECT

 public:
  MprisPlayer(const QString& service, const QDBusConnection& bus,
              QObject* parent = nullptr);

  QString Service() const { return service_; }
  QString Title() const { return title_; }
  QString Artist() const { return artist_; }
  QString Album() const { return album_; }
  QString ArtUrl() const { return art_url_; }
  QString TrackId() const { return track_id_; }
  qint64 LengthUs() const { return length_us_; }
  qint64 PositionUs() const { return position_us_; }
  bool CanSeek() const { return can_seek_; }
  QString PlaybackStatus() const { return playback_status_; }
  bool CanGoNext() const { return can_go_next_; }
  bool CanGoPrevious() const { return can_go_previous_; }
  bool CanPlay() const { return can_play_; }
  bool CanPause() const { return can_pause_; }
  bool HasVolume() const { return has_volume_; }
  double Volume() const { return volume_; }

  void Call(const QString& method);
  void SetVolume(double volume);
  void SetPosition(qint64 position_us);
  void Refresh();
  void RefreshPosition();

  void ApplyProperties(const QVariantMap& properties);

 signals:
  void Changed();
  void PositionChanged();

 private slots:
  void onPropertiesChanged(const QString& interface,
                           const QVariantMap& changed,
                           const QStringList& invalidated);
  void onSeeked(qlonglong position);

 private:
  QString service_;
  QDBusConnection bus_;
  QString title_;
  QString artist_;
  QString album_;
  QString art_url_;
  QString track_id_;
  qint64 length_us_ = 0;
  qint64 position_us_ = 0;
  bool can_seek_ = false;
  bool legacy_units_ = false;
  QString playback_status_;
  bool can_go_next_ = false;
  bool can_go_previous_ = false;
  bool can_play_ = false;
  bool can_pause_ = false;
  bool has_volume_ = false;
  double volume_ = 1.0;
};

class MediaController : public QObject {
  Q_OBJECT

  Q_PROPERTY(bool available READ Available NOTIFY Changed)
  Q_PROPERTY(QString title READ Title NOTIFY Changed)
  Q_PROPERTY(QString artist READ Artist NOTIFY Changed)
  Q_PROPERTY(QString album READ Album NOTIFY Changed)
  Q_PROPERTY(QString artUrl READ ArtUrl NOTIFY Changed)
  Q_PROPERTY(double length READ Length NOTIFY Changed)
  Q_PROPERTY(double position READ Position NOTIFY PositionChanged)
  Q_PROPERTY(bool canSeek READ CanSeek NOTIFY Changed)
  Q_PROPERTY(bool playing READ Playing NOTIFY Changed)
  Q_PROPERTY(bool canGoNext READ CanGoNext NOTIFY Changed)
  Q_PROPERTY(bool canGoPrevious READ CanGoPrevious NOTIFY Changed)
  Q_PROPERTY(bool canPlayPause READ CanPlayPause NOTIFY Changed)
  Q_PROPERTY(bool hasVolume READ HasVolume NOTIFY Changed)
  Q_PROPERTY(double volume READ Volume NOTIFY Changed)

 public:
  explicit MediaController(QObject* parent = nullptr);
  MediaController(const QDBusConnection& bus, bool scan,
                  QObject* parent = nullptr);

  bool Available() const;
  QString Title() const;
  QString Artist() const;
  QString Album() const;
  QString ArtUrl() const;
  double Length() const;
  double Position() const;
  bool CanSeek() const;
  bool Playing() const;
  bool CanGoNext() const;
  bool CanGoPrevious() const;
  bool CanPlayPause() const;
  bool HasVolume() const;
  double Volume() const;
  QString ActiveService() const;

  Q_INVOKABLE void PlayPause();
  Q_INVOKABLE void Next();
  Q_INVOKABLE void Previous();
  Q_INVOKABLE void SetVolume(double volume);
  Q_INVOKABLE void ToggleMute();
  Q_INVOKABLE void Seek(double position_ms);

  void AddPlayer(const QString& service);
  void RemovePlayer(const QString& service);
  MprisPlayer* Player(const QString& service) const;

 signals:
  void Changed();
  void PositionChanged();

 private slots:
  void onNameOwnerChanged(const QString& name, const QString& oldOwner,
                          const QString& newOwner);
  void onPlayerChanged();
  void onPlayerPositionChanged();

 private:
  void scanExistingPlayers();
  void selectActive();
  void updatePositionPolling();
  MprisPlayer* active() const;

  QDBusConnection bus_;
  QHash<QString, MprisPlayer*> players_;
  QStringList order_;  // appearance order
  QString active_;
  double last_volume_ = 1.0;
  QTimer position_timer_;
};

}  // namespace lock
}  // namespace gxde

#endif  // SRC_LOCK_COMMON_MEDIACONTROLLER_H_
