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

#include <QtTest>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QVariantMap>

#include <memory>

#include "mediacontroller.h"

// Minimal org.mpris.MediaPlayer2.Player on its own bus connection.
class FakePlayer : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
  Q_PROPERTY(QVariantMap Metadata READ metadata)
  Q_PROPERTY(bool CanGoNext READ yes)
  Q_PROPERTY(bool CanGoPrevious READ yes)
  Q_PROPERTY(bool CanPlay READ yes)
  Q_PROPERTY(bool CanPause READ yes)
  Q_PROPERTY(bool CanSeek READ yes)
  Q_PROPERTY(qlonglong Position READ position)
  Q_PROPERTY(double Volume READ volume WRITE setVolume)

 public:
  FakePlayer(const QString& name, const QString& title)
      : name_(name),
        bus_(QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                           QStringLiteral("fake-") + name)) {
    metadata_.insert(QStringLiteral("xesam:title"), title);
    metadata_.insert(QStringLiteral("xesam:artist"),
                     QStringList{QStringLiteral("Artist ") + name});
    metadata_.insert(QStringLiteral("xesam:album"),
                     QStringLiteral("Album ") + name);
    metadata_.insert(QStringLiteral("mpris:length"),
                     QVariant::fromValue<qlonglong>(240000000));
    metadata_.insert(
        QStringLiteral("mpris:trackid"),
        QVariant::fromValue(QDBusObjectPath(QStringLiteral("/track/1"))));
    bus_.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this,
                        QDBusConnection::ExportAllContents);
  }
  ~FakePlayer() override { Stop(); }

  bool Start() {
    return bus_.registerService(QStringLiteral("org.mpris.MediaPlayer2.") +
                                name_);
  }
  void Stop() {
    if (!bus_.isConnected()) return;
    bus_.unregisterService(QStringLiteral("org.mpris.MediaPlayer2.") + name_);
    QDBusConnection::disconnectFromBus(QStringLiteral("fake-") + name_);
  }

  QString service() const {
    return QStringLiteral("org.mpris.MediaPlayer2.") + name_;
  }
  QString playbackStatus() const { return status_; }
  QVariantMap metadata() const { return metadata_; }
  bool yes() const { return true; }
  double volume() const { return volume_; }
  void setVolume(double volume) { volume_ = volume; }
  qlonglong position() const { return position_; }
  void setPositionQuietly(qlonglong position) { position_ = position; }

  void Change(const QVariantMap& properties) {
    if (properties.contains(QStringLiteral("PlaybackStatus"))) {
      status_ = properties.value(QStringLiteral("PlaybackStatus")).toString();
    }
    if (properties.contains(QStringLiteral("Metadata"))) {
      metadata_ = properties.value(QStringLiteral("Metadata")).toMap();
    }
    QDBusMessage signal = QDBusMessage::createSignal(
        QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    signal << QStringLiteral("org.mpris.MediaPlayer2.Player") << properties
           << QStringList();
    bus_.send(signal);
  }

  QStringList calls;

 public slots:
  void PlayPause() { calls << QStringLiteral("PlayPause"); }
  void Next() { calls << QStringLiteral("Next"); }
  void Previous() { calls << QStringLiteral("Previous"); }
  void SetPosition(const QDBusObjectPath& track, qlonglong position) {
    calls << QStringLiteral("SetPosition %1 %2").arg(track.path()).arg(position);
    position_ = position;
  }

 private:
  QString name_;
  QDBusConnection bus_;
  QString status_ = QStringLiteral("Paused");
  QVariantMap metadata_;
  double volume_ = 0.8;
  qlonglong position_ = 0;
};

class MediaControllerTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void followsPlayerAndSendsCommands();
  void playingPlayerTakesOverAndFallsBack();
  void tracksPositionAndSeeks();
  void convertsDeepinMusicUnits();
  void filtersArtworkSchemes();
};

void MediaControllerTest::initTestCase() {
  if (!QDBusConnection::sessionBus().isConnected()) {
    QSKIP("No session bus; run under dbus-run-session.");
  }
}

void MediaControllerTest::followsPlayerAndSendsCommands() {
  FakePlayer player(QStringLiteral("alpha"), QStringLiteral("First Song"));
  QVERIFY(player.Start());

  // Existing players are found by the initial ListNames scan.
  gxde::lock::MediaController media;
  QTRY_VERIFY(media.Available());
  QCOMPARE(media.ActiveService(), player.service());
  QCOMPARE(media.Title(), QStringLiteral("First Song"));
  QCOMPARE(media.Artist(), QStringLiteral("Artist alpha"));
  QCOMPARE(media.Album(), QStringLiteral("Album alpha"));
  QCOMPARE(media.Length(), 240000.0);
  QVERIFY(!media.Playing());
  QVERIFY(media.CanGoNext());
  QVERIFY(media.HasVolume());
  QCOMPARE(media.Volume(), 0.8);

  media.PlayPause();
  media.Next();
  media.Previous();
  QTRY_COMPARE(player.calls, (QStringList{QStringLiteral("PlayPause"),
                                          QStringLiteral("Next"),
                                          QStringLiteral("Previous")}));

  media.SetVolume(0.3);
  QTRY_COMPARE(player.volume(), 0.3);

  // PropertiesChanged updates are followed.
  QVariantMap metadata;
  metadata.insert(QStringLiteral("xesam:title"), QStringLiteral("Second Song"));
  player.Change({{QStringLiteral("PlaybackStatus"), QStringLiteral("Playing")},
                 {QStringLiteral("Metadata"), metadata},
                 {QStringLiteral("Volume"), 0.3}});
  QTRY_COMPARE(media.Title(), QStringLiteral("Second Song"));
  QTRY_VERIFY(media.Playing());

  // Mute and restore.
  media.ToggleMute();
  QTRY_COMPARE(player.volume(), 0.0);
  player.Change({{QStringLiteral("Volume"), 0.0}});
  QTRY_COMPARE(media.Volume(), 0.0);
  media.ToggleMute();
  QTRY_COMPARE(player.volume(), 0.3);
}

void MediaControllerTest::playingPlayerTakesOverAndFallsBack() {
  gxde::lock::MediaController media;
  QVERIFY(!media.Available());

  FakePlayer first(QStringLiteral("first"), QStringLiteral("Paused Song"));
  QVERIFY(first.Start());
  QTRY_COMPARE(media.ActiveService(), first.service());

  auto second = std::make_unique<FakePlayer>(QStringLiteral("second"),
                                             QStringLiteral("Playing Song"));
  QVERIFY(second->Start());
  // A paused newcomer does not steal the indicator...
  QTest::qWait(200);
  QCOMPARE(media.ActiveService(), first.service());

  // ...but one that starts playing does.
  second->Change({{QStringLiteral("PlaybackStatus"), QStringLiteral("Playing")}});
  QTRY_COMPARE(media.ActiveService(), second->service());
  QCOMPARE(media.Title(), QStringLiteral("Playing Song"));

  // When it goes away, the remaining player is shown again.
  second.reset();
  QTRY_COMPARE(media.ActiveService(), first.service());
  QCOMPARE(media.Title(), QStringLiteral("Paused Song"));

  first.Stop();
  QTRY_VERIFY(!media.Available());
  QCOMPARE(media.ActiveService(), QString());
}

void MediaControllerTest::tracksPositionAndSeeks() {
  FakePlayer player(QStringLiteral("seek"), QStringLiteral("Long Song"));
  player.setPositionQuietly(30000000);
  QVERIFY(player.Start());
  gxde::lock::MediaController media;
  QTRY_VERIFY(media.Available());
  QVERIFY(media.CanSeek());

  // Paused: position is read once (track change), not polled.
  QTRY_COMPARE(media.Position(), 30000.0);

  // Playing: position is polled about once a second.
  player.Change({{QStringLiteral("PlaybackStatus"), QStringLiteral("Playing")}});
  QTRY_VERIFY(media.Playing());
  player.setPositionQuietly(84000000);
  QTRY_COMPARE_WITH_TIMEOUT(media.Position(), 84000.0, 3000);

  media.Seek(120000);
  QTRY_COMPARE(player.calls.value(0),
               QStringLiteral("SetPosition /track/1 120000000"));
  QCOMPARE(media.Position(), 120000.0);

  // Out of range seeks are clamped to the track.
  player.calls.clear();
  media.Seek(999999);
  QTRY_COMPARE(player.calls.value(0),
               QStringLiteral("SetPosition /track/1 240000000"));
}

// GXDE/Deepin Music: mpris:length in seconds, Position in milliseconds, no
// trackid (values as reported by the real player).
void MediaControllerTest::convertsDeepinMusicUnits() {
  FakePlayer player(QStringLiteral("DeepinMusic"), QStringLiteral("吾心不移"));
  QVERIFY(player.Start());
  gxde::lock::MediaController media;
  QTRY_VERIFY(media.Available());

  QVariantMap metadata;
  metadata.insert(QStringLiteral("xesam:title"), QStringLiteral("吾心不移"));
  metadata.insert(QStringLiteral("mpris:length"),
                  QVariant::fromValue<qlonglong>(198));
  player.setPositionQuietly(29806);
  player.Change({{QStringLiteral("Metadata"), metadata}});

  QTRY_COMPARE(media.Length(), 198000.0);
  QTRY_COMPARE(media.Position(), 29806.0);
  QVERIFY(!media.CanSeek());  // no trackid, so SetPosition is impossible

  player.Change({{QStringLiteral("PlaybackStatus"), QStringLiteral("Playing")}});
  player.setPositionQuietly(50417);
  QTRY_COMPARE_WITH_TIMEOUT(media.Position(), 50417.0, 3000);

  // Back to a spec-compliant length: microseconds again.
  metadata.insert(QStringLiteral("mpris:length"),
                  QVariant::fromValue<qlonglong>(223000000));
  player.setPositionQuietly(172000000);
  player.Change({{QStringLiteral("Metadata"), metadata}});
  QTRY_COMPARE(media.Length(), 223000.0);
  QTRY_COMPARE(media.Position(), 172000.0);
}

void MediaControllerTest::filtersArtworkSchemes() {
  FakePlayer player(QStringLiteral("art"), QStringLiteral("Cover"));
  QVERIFY(player.Start());
  gxde::lock::MediaController media;
  QTRY_VERIFY(media.Available());

  QVariantMap metadata;
  metadata.insert(QStringLiteral("xesam:title"), QStringLiteral("Cover"));
  metadata.insert(QStringLiteral("mpris:artUrl"),
                  QStringLiteral("https://example.com/cover.jpg"));
  player.Change({{QStringLiteral("Metadata"), metadata}});
  QTRY_COMPARE(media.ArtUrl(), QStringLiteral("https://example.com/cover.jpg"));

  metadata.insert(QStringLiteral("mpris:artUrl"),
                  QStringLiteral("file:///tmp/cover.png"));
  player.Change({{QStringLiteral("Metadata"), metadata}});
  QTRY_COMPARE(media.ArtUrl(), QStringLiteral("file:///tmp/cover.png"));

  // Other schemes are dropped.
  metadata.insert(QStringLiteral("mpris:artUrl"),
                  QStringLiteral("ftp://example.com/cover.jpg"));
  player.Change({{QStringLiteral("Metadata"), metadata}});
  QTRY_COMPARE(media.ArtUrl(), QString());
}

QTEST_GUILESS_MAIN(MediaControllerTest)
#include "media_controller_test.moc"
