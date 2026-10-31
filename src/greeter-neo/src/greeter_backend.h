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

#ifndef GXDE_DISPLAY_MANAGER_GREETER_NEO_SRC_GREETER_BACKEND_H_
#define GXDE_DISPLAY_MANAGER_GREETER_NEO_SRC_GREETER_BACKEND_H_

#include <QDataStream>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtGlobal>

namespace gxde::greeter {

class DaemonFingerprint;

class GreeterBackend : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList availableActions READ AvailableActions NOTIFY
                 AvailableActionsChanged)

  Q_PROPERTY(bool connected READ IsConnected NOTIFY ConnectedChanged)
  Q_PROPERTY(
      bool authenticating READ IsAuthenticating NOTIFY AuthenticatingChanged)
  Q_PROPERTY(QString lastSession READ LastSession NOTIFY LastSessionChanged)
  // A finger on the reader logs the chosen user in (alongside the password).
  Q_PROPERTY(bool fingerprintListening READ FingerprintListening NOTIFY
                 FingerprintChanged)
  // pam_fprintd's tries are used up; StartFingerprint() re-arms.
  Q_PROPERTY(bool fingerprintGaveUp READ FingerprintGaveUp NOTIFY
                 FingerprintChanged)

 public:
  explicit GreeterBackend(const QString& socket_name,
                          const QVariantList& sessions = {},
                          QObject* parent = nullptr);
  ~GreeterBackend() override;
  void EnableTransitions();
  Q_INVOKABLE void FinishTransition();
  void TransitionReady();
  bool TransitionPending() const { return transition_pending_; }
  bool IsConnected() const;
  bool IsAuthenticating() const;
  QString LastSession() const;
  Q_INVOKABLE void Login(const QString& username, const QString& password,
                         const QString& session_id);
  QStringList AvailableActions() const;
  Q_INVOKABLE void Request(const QString& action);

  bool FingerprintListening() const;
  bool FingerprintGaveUp() const;
  // Listens for `username`'s finger and logs into `session_id` on a match.
  // Restarts for another user or session; otherwise a no-op while listening.
  Q_INVOKABLE void StartFingerprint(const QString& username,
                                    const QString& session_id);
  Q_INVOKABLE void StopFingerprint();

 signals:
  void PrepareTransition(bool preserveBackground);
  void TransitionFinished(bool preserveBackground);
  void ConnectedChanged();
  void AuthenticatingChanged();
  void LastSessionChanged();
  void LoginFailed(const QString& message);
  void LoginSucceeded();
  void InformationMessage(const QString& message);
  void AvailableActionsChanged();
  void RequestFailed(const QString& message);
  void FingerprintChanged();
  // One rejected scan, with pam_fprintd's (translated) message.
  void FingerprintRejected(const QString& message);

 private:
  void SetAuthenticating(bool authenticating);
  void HandleDisconnect();
  void ReadMessages();
  void SetCapabilities(quint32 capabilities);
  QString InitialSession(const QString& remembered) const;
  bool SendMessage(quint32 message);
  // Serialized session for the Login family of messages; false if unknown.
  bool WriteSession(QDataStream& output, const QString& session_id) const;
  // Fingerprint messages: failures are not power requests, and a lost
  // connection already reports itself.
  bool SendRaw(const QByteArray& data);

  friend class DaemonFingerprint;

  QLocalSocket socket_;
  QDataStream input_;
  quint32 capabilities_ = 0;
  QVariantList sessions_;
  QString last_session_;
  bool transitions_enabled_ = false;
  bool transition_pending_ = false;
  bool preserve_background_ = false;
  bool authenticating_ = false;
  bool login_succeeded_ = false;
  DaemonFingerprint* fingerprint_ = nullptr;
  QString fingerprint_session_;  // session_id for fingerprint runs
};

}

#endif
