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

#include <QtQuickTest/quicktest.h>

#include <QByteArray>
#include <QtQml/qqml.h>

class TransitionBackend : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList availableActions READ availableActions CONSTANT)
  Q_PROPERTY(QString lastSession READ lastSession CONSTANT)
  Q_PROPERTY(bool authenticating READ authenticating CONSTANT)
  Q_PROPERTY(int completed MEMBER completed NOTIFY CompletedChanged)
  Q_PROPERTY(bool fingerprintListening MEMBER fingerprintListening NOTIFY
                 FingerprintChanged)
  Q_PROPERTY(bool fingerprintGaveUp MEMBER fingerprintGaveUp NOTIFY
                 FingerprintChanged)
  // "user|session" per StartFingerprint(), "stop" per StopFingerprint().
  Q_PROPERTY(QStringList fingerprintCalls MEMBER fingerprintCalls NOTIFY
                 FingerprintCallsChanged)

 public:
  using QObject::QObject;

  QStringList availableActions() const { return {}; }
  QString lastSession() const { return {}; }
  bool authenticating() const { return true; }
  int completed = 0;
  bool fingerprintListening = false;
  bool fingerprintGaveUp = false;
  QStringList fingerprintCalls;

  Q_INVOKABLE void StartFingerprint(const QString& user,
                                    const QString& session) {
    fingerprintCalls << user + "|" + session;
    emit FingerprintCallsChanged();
  }
  Q_INVOKABLE void StopFingerprint() {
    fingerprintCalls << "stop";
    emit FingerprintCallsChanged();
  }

  Q_INVOKABLE void FinishTransition() {
    ++completed;
    emit CompletedChanged();
  }

 signals:
  void PrepareTransition(bool preserve);
  void RequestFailed(QString message);
  void LoginFailed(QString message);
  void InformationMessage(QString message);
  void LoginSucceeded();
  void LastSessionChanged();
  void CompletedChanged();
  void FingerprintChanged();
  void FingerprintRejected(QString message);
  void FingerprintCallsChanged();
};

class TransitionScreenManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(QObject* currentWindow MEMBER currentWindow NOTIFY CurrentWindowChanged)

 public:
  using QObject::QObject;

  QObject* currentWindow = nullptr;

 signals:
  void CurrentWindowChanged();
  void CancelForwarded();
};

int main(int argc, char* argv[]) {
  qmlRegisterType<TransitionBackend>("TransitionTest", 1, 0, "TransitionBackend");
  qmlRegisterType<TransitionScreenManager>(
      "TransitionTest", 1, 0, "TransitionScreenManager");

  qputenv("QT_IM_MODULE", QByteArrayLiteral("qtvirtualkeyboard"));
  qputenv("QT_VIRTUALKEYBOARD_STYLE", QByteArrayLiteral("gxde"));
  return quick_test_main(argc, argv, "GreeterQml", QUICK_TEST_SOURCE_DIR);
}

#include "qml_test.moc"
