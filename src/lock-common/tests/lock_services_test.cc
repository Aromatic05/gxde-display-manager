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

// SystemVolume and NotificationCounter against fake dde-daemon / dde-osd
// services on a private session bus.

#include <QtTest>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QVariantMap>

#include <memory>

#include "notificationcounter.h"
#include "systemvolume.h"

namespace {

void emitPropertiesChanged(QDBusConnection& bus, const QString& path,
                           const QString& interface,
                           const QVariantMap& changed) {
  QDBusMessage signal = QDBusMessage::createSignal(
      path, QStringLiteral("org.freedesktop.DBus.Properties"),
      QStringLiteral("PropertiesChanged"));
  signal << interface << changed << QStringList();
  bus.send(signal);
}

}  // namespace

class FakeSink : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "com.deepin.daemon.Audio.Sink")
  Q_PROPERTY(double Volume READ volume)
  Q_PROPERTY(bool Mute READ mute)

 public:
  double volume() const { return volume_; }
  bool mute() const { return mute_; }

  double volume_ = 0.4;
  bool mute_ = false;
  QStringList calls;

 public slots:
  void SetVolume(double volume, bool play) {
    calls << QStringLiteral("SetVolume %1 %2").arg(volume).arg(play);
  }
  void SetMute(bool mute) { calls << QStringLiteral("SetMute %1").arg(mute); }
};

class FakeAudio : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "com.deepin.daemon.Audio")
  Q_PROPERTY(QDBusObjectPath DefaultSink READ defaultSink)
  Q_PROPERTY(double MaxUIVolume READ maxUiVolume)

 public:
  FakeAudio()
      : bus_(QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                           QStringLiteral("fake-audio"))) {
    bus_.registerObject(QStringLiteral("/com/deepin/daemon/Audio"), this,
                        QDBusConnection::ExportAllProperties);
    bus_.registerObject(QStringLiteral("/com/deepin/daemon/Audio/Sink1"),
                        &first, QDBusConnection::ExportAllContents);
    bus_.registerObject(QStringLiteral("/com/deepin/daemon/Audio/Sink2"),
                        &second, QDBusConnection::ExportAllContents);
    second.volume_ = 1.2;
  }
  ~FakeAudio() override {
    bus_.unregisterService(QStringLiteral("com.deepin.daemon.Audio"));
    QDBusConnection::disconnectFromBus(QStringLiteral("fake-audio"));
  }

  bool Start() {
    return bus_.registerService(QStringLiteral("com.deepin.daemon.Audio"));
  }
  void Stop() {
    bus_.unregisterService(QStringLiteral("com.deepin.daemon.Audio"));
  }

  QDBusObjectPath defaultSink() const { return QDBusObjectPath(sink_); }
  double maxUiVolume() const { return 1.5; }

  void SwitchSink(const QString& path) {
    sink_ = path;
    emitPropertiesChanged(
        bus_, QStringLiteral("/com/deepin/daemon/Audio"),
        QStringLiteral("com.deepin.daemon.Audio"),
        {{QStringLiteral("DefaultSink"),
          QVariant::fromValue(QDBusObjectPath(path))}});
  }
  void ChangeSink(FakeSink& sink, const QString& path,
                  const QVariantMap& changed) {
    if (changed.contains(QStringLiteral("Volume"))) {
      sink.volume_ = changed.value(QStringLiteral("Volume")).toDouble();
    }
    if (changed.contains(QStringLiteral("Mute"))) {
      sink.mute_ = changed.value(QStringLiteral("Mute")).toBool();
    }
    emitPropertiesChanged(bus_, path,
                          QStringLiteral("com.deepin.daemon.Audio.Sink"),
                          changed);
  }

  FakeSink first;
  FakeSink second;

 private:
  QDBusConnection bus_;
  QString sink_ = QStringLiteral("/com/deepin/daemon/Audio/Sink1");
};

class FakeNotificationCenter : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "com.deepin.dde.Notification")

 public:
  FakeNotificationCenter()
      : bus_(QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                           QStringLiteral("fake-osd"))) {}
  ~FakeNotificationCenter() override {
    bus_.unregisterService(QStringLiteral("com.deepin.dde.Notification"));
    QDBusConnection::disconnectFromBus(QStringLiteral("fake-osd"));
  }

  bool Start() {
    return bus_.registerService(QStringLiteral("com.deepin.dde.Notification"));
  }
  void Stop() {
    bus_.unregisterService(QStringLiteral("com.deepin.dde.Notification"));
  }
  void Notify() {
    QDBusMessage signal = QDBusMessage::createSignal(
        QStringLiteral("/com/deepin/dde/Notification"),
        QStringLiteral("com.deepin.dde.Notification"),
        QStringLiteral("RecordAdded"));
    signal << QStringLiteral("{\"summary\":\"secret\"}");
    bus_.send(signal);
  }

 private:
  QDBusConnection bus_;
};

class LockServicesTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void volumeFollowsDefaultSink();
  void volumeUnavailableWithoutDaemon();
  void countsNotificationsWhileLocked();
  void notificationsUnavailableOutsideGxde();
};

void LockServicesTest::initTestCase() {
  if (!QDBusConnection::sessionBus().isConnected()) {
    QSKIP("No session bus; run under dbus-run-session.");
  }
}

void LockServicesTest::volumeFollowsDefaultSink() {
  FakeAudio audio;
  QVERIFY(audio.Start());

  gxde::lock::SystemVolume volume;
  QTRY_VERIFY(volume.Available());
  QTRY_COMPARE(volume.Volume(), 0.4);
  QVERIFY(!volume.Muted());
  QCOMPARE(volume.MaxVolume(), 1.0);

  // Commands go to the default sink, silently; raising also unmutes.
  volume.SetVolume(0.7);
  QCOMPARE(volume.Volume(), 0.7);  // shown before dde-daemon confirms
  QTRY_COMPARE(audio.first.calls.value(0), QStringLiteral("SetVolume 0.7 0"));
  volume.ToggleMute();
  QTRY_COMPARE(audio.first.calls.value(1), QStringLiteral("SetMute 1"));
  audio.ChangeSink(audio.first, QStringLiteral("/com/deepin/daemon/Audio/Sink1"),
                   {{QStringLiteral("Mute"), true},
                    {QStringLiteral("Volume"), 0.7}});
  QTRY_VERIFY(volume.Muted());
  QCOMPARE(volume.Volume(), 0.7);
  audio.first.calls.clear();
  volume.SetVolume(0.5);
  QTRY_COMPARE(audio.first.calls,
               (QStringList{QStringLiteral("SetVolume 0.5 0"),
                            QStringLiteral("SetMute 0")}));

  // Volume above 1.0 is clamped unless the sink is already amplified.
  audio.first.calls.clear();
  volume.SetVolume(1.4);
  QTRY_COMPARE(audio.first.calls.value(0), QStringLiteral("SetVolume 1 0"));

  // Switching the default sink follows the new one.
  audio.SwitchSink(QStringLiteral("/com/deepin/daemon/Audio/Sink2"));
  QTRY_COMPARE(volume.Volume(), 1.2);
  QCOMPARE(volume.MaxVolume(), 1.5);
  audio.ChangeSink(audio.first, QStringLiteral("/com/deepin/daemon/Audio/Sink1"),
                   {{QStringLiteral("Volume"), 0.1}});
  QTest::qWait(200);
  QCOMPARE(volume.Volume(), 1.2);  // the old sink is no longer followed

  // dde-daemon going away hides the control.
  audio.Stop();
  QTRY_VERIFY(!volume.Available());
}

void LockServicesTest::volumeUnavailableWithoutDaemon() {
  gxde::lock::SystemVolume volume;
  QTest::qWait(300);
  QVERIFY(!volume.Available());
  volume.SetVolume(0.5);  // no crash, no call

  // dde-daemon starting later is picked up.
  FakeAudio audio;
  QVERIFY(audio.Start());
  QTRY_VERIFY(volume.Available());
}

void LockServicesTest::countsNotificationsWhileLocked() {
  FakeNotificationCenter center;
  QVERIFY(center.Start());

  gxde::lock::NotificationCounter counter;
  QTRY_VERIFY(counter.Available());
  QCOMPARE(counter.Count(), 0);

  center.Notify();
  center.Notify();
  center.Notify();
  QTRY_COMPARE(counter.Count(), 3);

  counter.Reset();
  QCOMPARE(counter.Count(), 0);
  center.Notify();
  QTRY_COMPARE(counter.Count(), 1);

  center.Stop();
  QTRY_VERIFY(!counter.Available());
}

void LockServicesTest::notificationsUnavailableOutsideGxde() {
  gxde::lock::NotificationCounter counter;
  QTest::qWait(300);
  QVERIFY(!counter.Available());

  FakeNotificationCenter center;
  QVERIFY(center.Start());
  QTRY_VERIFY(counter.Available());
}

QTEST_GUILESS_MAIN(LockServicesTest)
#include "lock_services_test.moc"
