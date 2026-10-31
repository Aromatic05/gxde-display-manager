/*
 * Copyright (C) CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "greeter-neo/src/greeter_backend.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

#include "common/Messages.h"
#include "common/Session.h"

namespace gxde::greeter {
namespace {

class GreeterBackendTest : public QObject {
  Q_OBJECT

 private slots:
  void Transition_data();
  void Transition();
  void Authentication();
  void AuthenticationDisconnect();
  void FingerprintLogin();
  void RestoresLastSession_data();
  void RestoresLastSession();
  void Standalone() {
    GreeterBackend backend{QString()};
    QSignalSpy errors(&backend, &GreeterBackend::RequestFailed);
    QVERIFY(backend.AvailableActions().isEmpty());
    backend.Request("shutdown");
    QCOMPARE(errors.size(), 1);
  }

  void Protocol() {
    QLocalServer server;
    QVERIFY(server.listen("gxde-power-test-" + QUuid::createUuid().toString()));
    GreeterBackend backend(server.fullServerName());
    QSignalSpy errors(&backend, &GreeterBackend::RequestFailed);
    QTRY_VERIFY(server.hasPendingConnections());
    auto* peer = server.nextPendingConnection();
    QTRY_COMPARE(peer->bytesAvailable(), 4);
    QDataStream input(peer);
    quint32 message;
    input >> message;
    QCOMPARE(message, static_cast<quint32>(SDDM::GreeterMessages::Connect));
    QVERIFY(backend.AvailableActions().isEmpty());

    QByteArray data;
    QDataStream output(&data, QIODevice::WriteOnly);
    output << static_cast<quint32>(SDDM::DaemonMessages::HostName)
           << QString("test-host")
           << static_cast<quint32>(SDDM::DaemonMessages::LastSession)
           << QString("test.desktop")
           << static_cast<quint32>(SDDM::DaemonMessages::Capabilities)
           << static_cast<quint32>(SDDM::Capability::PowerOff |
                                   SDDM::Capability::Reboot);
    for (int i = 0; i < data.size(); ++i) {
      QCOMPARE(peer->write(data.mid(i, 1)), 1);
      peer->flush();
      QTest::qWait(1);
      if (i + 1 < data.size()) {
        QVERIFY(backend.AvailableActions().isEmpty());
      }
    }
    QTRY_COMPARE(backend.AvailableActions(),
                 QStringList({"reboot", "shutdown"}));
    backend.Request("suspend");
    backend.Request("invalid");
    QCOMPARE(errors.size(), 2);
    QCOMPARE(peer->bytesAvailable(), 0);
    backend.Request("reboot");
    backend.Request("shutdown");
    QTRY_COMPARE(peer->bytesAvailable(), 8);
    input >> message;
    QCOMPARE(message, static_cast<quint32>(SDDM::GreeterMessages::Reboot));
    input >> message;
    QCOMPARE(message, static_cast<quint32>(SDDM::GreeterMessages::PowerOff));

    data.clear();
    QDataStream update(&data, QIODevice::WriteOnly);
    update << static_cast<quint32>(SDDM::DaemonMessages::Capabilities)
           << static_cast<quint32>(SDDM::Capability::Suspend);
    peer->write(data);
    peer->flush();
    QTRY_COMPARE(backend.AvailableActions(), QStringList({"suspend"}));
    backend.Request("suspend");
    QTRY_COMPARE(peer->bytesAvailable(), 4);
    input >> message;
    QCOMPARE(message, static_cast<quint32>(SDDM::GreeterMessages::Suspend));

    peer->disconnectFromServer();
    QTRY_VERIFY(backend.AvailableActions().isEmpty());
    QTRY_COMPARE(errors.size(), 3);
    backend.Request("reboot");
    QCOMPARE(errors.size(), 4);
  }

  void UnknownMessage() {
    QLocalServer server;
    QVERIFY(server.listen("gxde-power-test-" + QUuid::createUuid().toString()));
    GreeterBackend backend(server.fullServerName());
    QSignalSpy errors(&backend, &GreeterBackend::RequestFailed);
    QTRY_VERIFY(server.hasPendingConnections());
    auto* peer = server.nextPendingConnection();
    QByteArray data;
    QDataStream output(&data, QIODevice::WriteOnly);
    output << static_cast<quint32>(SDDM::DaemonMessages::Capabilities)
           << static_cast<quint32>(7) << static_cast<quint32>(999);
    peer->write(data);
    peer->flush();
    QTRY_COMPARE(errors.size(), 1);
    QVERIFY(backend.AvailableActions().isEmpty());
  }
};

void GreeterBackendTest::Transition_data() {
  QTest::addColumn<bool>("preserve");
  QTest::newRow("shared-x11") << true;
  QTest::newRow("cross-server") << false;
}

void GreeterBackendTest::Transition() {
  QFETCH(bool, preserve);
  QLocalServer server;
  QVERIFY(server.listen("gxde-transition-" + QUuid::createUuid().toString()));
  GreeterBackend backend(server.fullServerName(),
      {QVariantMap{{"id", "x11/test.desktop"}, {"type", "x11"},
                   {"file", "test.desktop"}}});

  backend.EnableTransitions();
  QSignalSpy prepare(&backend, &GreeterBackend::PrepareTransition);
  QSignalSpy finished(&backend, &GreeterBackend::TransitionFinished);
  QSignalSpy failed(&backend, &GreeterBackend::LoginFailed);
  QTRY_VERIFY(server.hasPendingConnections());
  auto* peer = server.nextPendingConnection();
  QTRY_COMPARE(peer->bytesAvailable(), 8);
  QDataStream input(peer);
  quint32 message;
  input >> message;
  QCOMPARE(message, quint32(SDDM::GreeterMessages::Connect));
  input >> message;
  QCOMPARE(message, quint32(SDDM::GreeterMessages::EnableTransitions));
  backend.Login("user", "password", "x11/test.desktop");
  QTRY_VERIFY(peer->bytesAvailable() > 0);
  peer->readAll();
  QByteArray reply;
  QDataStream output(&reply, QIODevice::WriteOnly);
  output << quint32(preserve ? SDDM::DaemonMessages::PreserveBackground
                            : SDDM::DaemonMessages::FadeToBlack);

  peer->write(reply.left(2));
  peer->flush();
  QTest::qWait(10);
  QCOMPARE(prepare.size(), 0);
  peer->write(reply.mid(2));
  peer->flush();
  QTRY_COMPARE(prepare.size(), 1);
  QCOMPARE(prepare.first().first().toBool(), preserve);
  QVERIFY(backend.IsAuthenticating());
  QCOMPARE(peer->bytesAvailable(), 0);
  backend.FinishTransition();
  QCOMPARE(finished.size(), 1);
  QCOMPARE(peer->bytesAvailable(), 0); // Still waiting for the rendered frame.
  backend.TransitionReady();
  QTRY_COMPARE(peer->bytesAvailable(), 4);
  input >> message;
  QCOMPARE(message, quint32(SDDM::GreeterMessages::TransitionReady));
  backend.TransitionReady();
  QTest::qWait(10);
  QCOMPARE(peer->bytesAvailable(), 0);

  peer->write(reply); // A failure during another transition cancels its reply.
  peer->flush();
  QTRY_COMPARE(prepare.size(), 2);
  reply.clear();
  QDataStream failure(&reply, QIODevice::WriteOnly);
  failure << quint32(SDDM::DaemonMessages::LoginFailed);
  peer->write(reply);
  peer->flush();
  QTRY_COMPARE(failed.size(), 1);
  backend.FinishTransition();
  backend.TransitionReady();
  QCOMPARE(finished.size(), 1);
  QVERIFY(!backend.IsAuthenticating());
  QTest::qWait(10);
  QCOMPARE(peer->bytesAvailable(), 0);
}

void GreeterBackendTest::Authentication() {
  QLocalServer server;
  QVERIFY(server.listen("gxde-login-test-" + QUuid::createUuid().toString()));
  const QVariantList sessions = {QVariantMap{{"id", "x11/test.desktop"},
                                             {"type", "x11"},
                                             {"file", "test.desktop"}},
                                 QVariantMap{{"id", "wayland/test.desktop"},
                                             {"type", "wayland"},
                                             {"file", "test.desktop"}}};
  GreeterBackend backend(server.fullServerName(), sessions);
  QSignalSpy failed(&backend, &GreeterBackend::LoginFailed);
  QSignalSpy succeeded(&backend, &GreeterBackend::LoginSucceeded);
  QSignalSpy information(&backend, &GreeterBackend::InformationMessage);
  QTRY_VERIFY(server.hasPendingConnections());
  auto* peer = server.nextPendingConnection();
  QTRY_COMPARE(peer->bytesAvailable(), 4);
  peer->readAll();
  backend.Login("user", "test-password", "invalid");
  QCOMPARE(failed.size(), 1);
  QVERIFY(!backend.IsAuthenticating());

  for (int session_index = 0; session_index < 2; ++session_index) {
    backend.Login("user", "test-password",
                  sessions.at(session_index).toMap().value("id").toString());
    QVERIFY(backend.IsAuthenticating());
    backend.Login("duplicate", "ignored", "x11/test.desktop");
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    QDataStream request(peer);
    quint32 message = 0;
    quint32 session_type = 0;
    QString username, password, file;
    request >> message >> username >> password >> session_type >> file;
    QCOMPARE(request.status(), QDataStream::Ok);
    QCOMPARE(message, static_cast<quint32>(SDDM::GreeterMessages::Login));
    QCOMPARE(username, QString("user"));
    QCOMPARE(password, QString("test-password"));
    QCOMPARE(session_type, static_cast<quint32>(session_index + 1));
    QCOMPARE(file, QString("test.desktop"));
    QCOMPARE(peer->bytesAvailable(), 0);
    QByteArray reply;
    QDataStream response(&reply, QIODevice::WriteOnly);
    response << static_cast<quint32>(SDDM::DaemonMessages::LastSession)
             << QString("test.desktop")
             << static_cast<quint32>(SDDM::DaemonMessages::InformationMessage)
             << QString("Checking credentials")
             << static_cast<quint32>(
                    session_index == 0 ? SDDM::DaemonMessages::LoginFailed
                                       : SDDM::DaemonMessages::LoginSucceeded);
    for (char byte : reply) {
      peer->write(&byte, 1);
      peer->flush();
      QTest::qWait(1);
    }
    QTRY_VERIFY(!backend.IsAuthenticating());
    QCOMPARE(backend.LastSession(), QString("x11/test.desktop"));
  }
  QCOMPARE(failed.size(), 2);
  QCOMPARE(succeeded.size(), 1);
  QCOMPARE(information.size(), 2);
  peer->disconnectFromServer();
  QTRY_VERIFY(!backend.IsConnected());
  QCOMPARE(failed.size(), 2);
}

namespace {

// Reads one greeter message from the fake daemon's side.
struct GreeterMessage {
  quint32 opcode = 0;
  QString user;
  quint32 session_type = 0;
  QString file;
};

GreeterMessage ReadGreeterMessage(QLocalSocket* peer) {
  GreeterMessage message;
  QDataStream input(peer);
  input >> message.opcode;
  switch (static_cast<SDDM::GreeterMessages>(message.opcode)) {
    case SDDM::GreeterMessages::FingerprintProbe:
      input >> message.user;
      break;
    case SDDM::GreeterMessages::FingerprintLogin:
      input >> message.user >> message.session_type >> message.file;
      break;
    case SDDM::GreeterMessages::Login: {
      QString password;
      input >> message.user >> password >> message.session_type >>
          message.file;
      break;
    }
    default:
      break;
  }
  return message;
}

void Reply(QLocalSocket* peer, const QByteArray& data) {
  peer->write(data);
  peer->flush();
}

}  // namespace

void GreeterBackendTest::FingerprintLogin() {
  QLocalServer server;
  QVERIFY(server.listen("gxde-finger-test-" + QUuid::createUuid().toString()));
  const QVariantList sessions = {QVariantMap{{"id", "x11/test.desktop"},
                                             {"type", "x11"},
                                             {"file", "test.desktop"}}};
  GreeterBackend backend(server.fullServerName(), sessions);
  QSignalSpy rejected(&backend, &GreeterBackend::FingerprintRejected);
  QSignalSpy succeeded(&backend, &GreeterBackend::LoginSucceeded);
  QSignalSpy prepare(&backend, &GreeterBackend::PrepareTransition);
  QSignalSpy failures(&backend, &GreeterBackend::RequestFailed);
  QTRY_VERIFY(server.hasPendingConnections());
  auto* peer = server.nextPendingConnection();
  QTRY_COMPARE(peer->bytesAvailable(), 4);
  peer->readAll();  // Connect
  backend.EnableTransitions();
  QTRY_COMPARE(peer->bytesAvailable(), 4);
  peer->readAll();  // EnableTransitions

  auto expect = [&](SDDM::GreeterMessages opcode) {
    // Keep the event loop running: the backend lives in this thread.
    for (int waited = 0; peer->bytesAvailable() == 0 && waited < 2000;
         waited += 10) {
      QTest::qWait(10);
    }
    if (peer->bytesAvailable() == 0) return GreeterMessage{};
    GreeterMessage message = ReadGreeterMessage(peer);
    if (message.opcode != static_cast<quint32>(opcode)) {
      qWarning() << "expected" << static_cast<quint32>(opcode) << "got"
                 << message.opcode;
    }
    return message;
  };
  auto available = [&](const QString& user, bool yes) {
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << static_cast<quint32>(SDDM::DaemonMessages::FingerprintAvailable)
        << user << static_cast<quint32>(yes ? 1 : 0);
    Reply(peer, data);
  };
  auto send = [&](SDDM::DaemonMessages opcode, const QString& text = {}) {
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << static_cast<quint32>(opcode);
    if (opcode == SDDM::DaemonMessages::FingerprintRejected) out << text;
    Reply(peer, data);
  };

  // No reader: probed, nothing started.
  backend.StartFingerprint("user", "x11/test.desktop");
  QCOMPARE(expect(SDDM::GreeterMessages::FingerprintProbe).user,
           QString("user"));
  available("user", false);
  QTRY_VERIFY(!backend.FingerprintListening());
  QTest::qWait(50);
  QCOMPARE(peer->bytesAvailable(), 0);

  // Probe, run, rejection, then pam_fprintd gives up right away.
  backend.StartFingerprint("user", "x11/test.desktop");
  expect(SDDM::GreeterMessages::FingerprintProbe);
  available("user", true);
  GreeterMessage run = expect(SDDM::GreeterMessages::FingerprintLogin);
  QCOMPARE(run.user, QString("user"));
  QCOMPARE(run.session_type, static_cast<quint32>(SDDM::Session::X11Session));
  QCOMPARE(run.file, QString("test.desktop"));
  QVERIFY(backend.FingerprintListening());
  send(SDDM::DaemonMessages::FingerprintRejected, "指纹匹配失败");
  QTRY_COMPARE(rejected.size(), 1);
  QCOMPARE(rejected.at(0).at(0).toString(), QString("指纹匹配失败"));
  QVERIFY(backend.FingerprintListening());
  send(SDDM::DaemonMessages::FingerprintFinished);
  QTRY_VERIFY(backend.FingerprintGaveUp());
  QTest::qWait(50);
  QCOMPARE(peer->bytesAvailable(), 0);  // no cancel for a finished run

  // A typed password cancels the run before logging in.
  backend.StartFingerprint("user", "x11/test.desktop");
  expect(SDDM::GreeterMessages::FingerprintProbe);
  available("user", true);
  expect(SDDM::GreeterMessages::FingerprintLogin);
  backend.Login("user", "secret", "x11/test.desktop");
  expect(SDDM::GreeterMessages::FingerprintCancel);
  QCOMPARE(expect(SDDM::GreeterMessages::Login).user, QString("user"));
  QVERIFY(!backend.FingerprintListening());
  send(SDDM::DaemonMessages::LoginFailed);
  QTRY_VERIFY(!backend.IsAuthenticating());

  // A match: the daemon fades the greeter and starts the session.
  backend.StartFingerprint("user", "x11/test.desktop");
  expect(SDDM::GreeterMessages::FingerprintProbe);
  available("user", true);
  expect(SDDM::GreeterMessages::FingerprintLogin);
  send(SDDM::DaemonMessages::FadeToBlack);
  QTRY_COMPARE(prepare.size(), 1);
  backend.StopFingerprint();  // QML stops the reader once transitioning
  send(SDDM::DaemonMessages::LoginSucceeded);
  QTRY_COMPARE(succeeded.size(), 1);
  QVERIFY(!backend.FingerprintListening());
  QTest::qWait(50);
  QCOMPARE(peer->bytesAvailable(), 0);  // nothing to cancel after a match
  QCOMPARE(failures.size(), 0);
}

void GreeterBackendTest::RestoresLastSession_data() {
  QTest::addColumn<QString>("remembered");
  QTest::addColumn<QString>("expected");
  QTest::newRow("file") << "gxde.desktop" << "x11/gxde.desktop";
  QTest::newRow("name in other case") << "GXDE" << "x11/gxde.desktop";
  QTest::newRow("gone") << "missing.desktop" << "";
}

void GreeterBackendTest::RestoresLastSession() {
  QFETCH(QString, remembered);
  QFETCH(QString, expected);
  QLocalServer server;
  QVERIFY(server.listen("gxde-session-test-" + QUuid::createUuid().toString()));
  const QVariantList sessions = {QVariantMap{{"id", "x11/gxde.desktop"},
                                             {"type", "x11"},
                                             {"file", "gxde.desktop"}},
                                 QVariantMap{{"id", "wayland/other.desktop"},
                                             {"type", "wayland"},
                                             {"file", "other.desktop"}}};
  GreeterBackend backend(server.fullServerName(), sessions);
  QSignalSpy changed(&backend, &GreeterBackend::LastSessionChanged);
  QTRY_VERIFY(server.hasPendingConnections());
  auto* peer = server.nextPendingConnection();
  QByteArray data;
  QDataStream output(&data, QIODevice::WriteOnly);
  output << static_cast<quint32>(SDDM::DaemonMessages::LastSession)
         << remembered;
  peer->write(data);
  peer->flush();
  QTRY_COMPARE(changed.size(), 1);
  QCOMPARE(backend.LastSession(), expected);
}

void GreeterBackendTest::AuthenticationDisconnect() {
  QLocalServer server;
  QVERIFY(server.listen("gxde-login-test-" + QUuid::createUuid().toString()));
  const QVariantList sessions = {QVariantMap{
      {"id", "x11/test.desktop"}, {"type", "x11"}, {"file", "test.desktop"}}};
  GreeterBackend backend(server.fullServerName(), sessions);
  QSignalSpy failed(&backend, &GreeterBackend::LoginFailed);
  QTRY_VERIFY(server.hasPendingConnections());
  auto* peer = server.nextPendingConnection();
  QTRY_VERIFY(backend.IsConnected());
  backend.Login("user", "test-password", "x11/test.desktop");
  QVERIFY(backend.IsAuthenticating());
  peer->abort();
  QTRY_COMPARE(failed.size(), 1);
  QVERIFY(!backend.IsAuthenticating());
  QVERIFY(!backend.IsConnected());
}

}
}

QTEST_GUILESS_MAIN(gxde::greeter::GreeterBackendTest)
#include "greeter_backend_test.moc"
