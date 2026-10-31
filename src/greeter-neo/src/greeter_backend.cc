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
#include <QDebug>
#include <QFileInfo>
#include <QIODevice>
#include <QLocalSocket>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtGlobal>

#include "common/FingerprintSession.h"
#include "common/Messages.h"
#include "common/Session.h"
#include "common/SystemDefaults.h"

namespace gxde::greeter {

// Fingerprint runs through the daemon: the probe is FingerprintProbe and a
// run is a FingerprintLogin, whose match starts the session (LoginSucceeded).
class DaemonFingerprint : public FingerprintSession {
 public:
  explicit DaemonFingerprint(GreeterBackend* backend)
      : FingerprintSession(backend), backend_(backend) {}

  void Available(const QString& user, bool available) {
    ProbeFinished(user, available);
  }
  void Rejected(const QString& message) { RunRejected(message); }
  void Finished() {
    run_active_ = false;
    RunFinished(false);
  }
  // The daemon is starting the session: nothing to cancel any more.
  void Succeeded() {
    run_active_ = false;
    RunFinished(true);
  }

 protected:
  void Probe(const QString& user) override {
    QByteArray data;
    QDataStream output(&data, QIODevice::WriteOnly);
    output << static_cast<quint32>(SDDM::GreeterMessages::FingerprintProbe)
           << user;
    if (!backend_->SendRaw(data)) {
      // Not connected: nothing to listen with.
      QMetaObject::invokeMethod(
          this, [this, user] { ProbeFinished(user, false); },
          Qt::QueuedConnection);
    }
  }
  void StartRun(const QString& user) override {
    QByteArray data;
    QDataStream output(&data, QIODevice::WriteOnly);
    output << static_cast<quint32>(SDDM::GreeterMessages::FingerprintLogin)
           << user;
    if (!backend_->WriteSession(output, backend_->fingerprint_session_) ||
        !backend_->SendRaw(data)) {
      QMetaObject::invokeMethod(
          this, [this] { RunUnusable(); }, Qt::QueuedConnection);
      return;
    }
    run_active_ = true;
  }
  void StopRun() override {
    if (!run_active_) return;
    run_active_ = false;
    QByteArray data;
    QDataStream output(&data, QIODevice::WriteOnly);
    output << static_cast<quint32>(SDDM::GreeterMessages::FingerprintCancel);
    backend_->SendRaw(data);
  }

 private:
  GreeterBackend* backend_;
  bool run_active_ = false;
};

namespace {

struct PowerAction {
  const char* name;
  SDDM::Capability capability;
  SDDM::GreeterMessages message;
};

constexpr PowerAction kActions[] = {
    {"reboot", SDDM::Capability::Reboot, SDDM::GreeterMessages::Reboot},
    {"shutdown", SDDM::Capability::PowerOff, SDDM::GreeterMessages::PowerOff},
    {"suspend", SDDM::Capability::Suspend, SDDM::GreeterMessages::Suspend}};

}

GreeterBackend::GreeterBackend(const QString& socket_name,
                               const QVariantList& sessions, QObject* parent)
    : QObject(parent),
      input_(&socket_),
      sessions_(sessions),
      fingerprint_(new DaemonFingerprint(this)) {
  connect(fingerprint_, &FingerprintSession::StateChanged, this,
          &GreeterBackend::FingerprintChanged);
  connect(fingerprint_, &FingerprintSession::Rejected, this,
          &GreeterBackend::FingerprintRejected);
  connect(&socket_, &QLocalSocket::connected, this, [this] {
    emit ConnectedChanged();
    SendMessage(static_cast<quint32>(SDDM::GreeterMessages::Connect));
    if (transitions_enabled_) {
      EnableTransitions();
    }
  });
  connect(&socket_, &QLocalSocket::readyRead, this,
          &GreeterBackend::ReadMessages);
  connect(&socket_, &QLocalSocket::disconnected, this,
          &GreeterBackend::HandleDisconnect);
  connect(&socket_, &QLocalSocket::errorOccurred, this, [this] {
    HandleDisconnect();
    if (login_succeeded_) return;
    emit RequestFailed(tr("电源服务连接失败：%1").arg(socket_.errorString()));
  });
  if (!socket_name.isEmpty()) {
    socket_.connectToServer(socket_name);
  }
}

GreeterBackend::~GreeterBackend() = default;

bool GreeterBackend::FingerprintListening() const {
  return fingerprint_->Listening();
}

bool GreeterBackend::FingerprintGaveUp() const {
  return fingerprint_->GetState() == FingerprintSession::State::kGaveUp;
}

void GreeterBackend::StartFingerprint(const QString& username,
                                      const QString& session_id) {
  if (authenticating_ || login_succeeded_ || username.trimmed().isEmpty()) {
    return;
  }
  // Another session means another FingerprintLogin.
  if (session_id != fingerprint_session_) fingerprint_->Stop();
  fingerprint_session_ = session_id;
  fingerprint_->Start(username);
}

void GreeterBackend::StopFingerprint() { fingerprint_->Stop(); }

bool GreeterBackend::WriteSession(QDataStream& output,
                                  const QString& session_id) const {
  QVariantMap session;
  for (const auto& candidate : sessions_) {
    if (candidate.toMap().value("id").toString() == session_id) {
      session = candidate.toMap();
      break;
    }
  }
  const QString type = session.value("type").toString();
  const QString file = session.value("file").toString();
  if ((type != "x11" && type != "wayland") || file.isEmpty()) return false;
  output << static_cast<quint32>(type == "wayland"
                                     ? SDDM::Session::WaylandSession
                                     : SDDM::Session::X11Session)
         << file;
  return true;
}

bool GreeterBackend::SendRaw(const QByteArray& data) {
  if (socket_.state() != QLocalSocket::ConnectedState ||
      socket_.write(data) != data.size()) {
    return false;
  }
  socket_.flush();
  return true;
}

void GreeterBackend::EnableTransitions() {
  transitions_enabled_ = true;

  if (IsConnected()) {
    SendMessage(static_cast<quint32>(SDDM::GreeterMessages::EnableTransitions));
  }
}

void GreeterBackend::FinishTransition() {
  if (transition_pending_) {
    emit TransitionFinished(preserve_background_);
  }
}

void GreeterBackend::TransitionReady() {
  if (!transition_pending_) {
    return;
  }

  transition_pending_ = false;
  SendMessage(static_cast<quint32>(SDDM::GreeterMessages::TransitionReady));
}

bool GreeterBackend::IsConnected() const {
  return socket_.state() == QLocalSocket::ConnectedState;
}

bool GreeterBackend::IsAuthenticating() const { return authenticating_; }

QString GreeterBackend::InitialSession(const QString& remembered) const {
  const QString desktop_file =
      GxdmSystemDefaults::initialSessionDesktopFile(remembered);
  const bool using_system_default =
      remembered.isEmpty() && !desktop_file.isEmpty();
  if (desktop_file.isEmpty()) return {};

  const QString name = QFileInfo(desktop_file).completeBaseName();
  for (const QVariant& candidate : sessions_) {
    const QVariantMap session = candidate.toMap();
    const QString file = session.value("file").toString();
    if (QFileInfo(file).completeBaseName().compare(name, Qt::CaseInsensitive) !=
        0) {
      continue;
    }
    const QString id = session.value("id").toString();
    if (using_system_default) {
      qDebug() << "(Frontend) No remembered session; Defaulting to"
               << desktop_file << id;
    } else {
      qDebug() << "(Frontend) Restoring last session:" << desktop_file << id;
    }
    return id;
  }

  if (using_system_default) {
    qWarning() << "(Frontend) GXDE default session is unavailable:"
               << desktop_file;
  } else {
    qWarning() << "(Frontend) Remembered session is no longer available:"
               << desktop_file;
  }
  return {};
}

QString GreeterBackend::LastSession() const { return last_session_; }

void GreeterBackend::SetAuthenticating(bool authenticating) {
  if (authenticating_ == authenticating) return;
  authenticating_ = authenticating;
  emit AuthenticatingChanged();
}

void GreeterBackend::HandleDisconnect() {
  transition_pending_ = false;
  fingerprint_->Stop();
  SetCapabilities(0);
  emit ConnectedChanged();
  if (authenticating_ && !login_succeeded_) {
    SetAuthenticating(false);
    emit LoginFailed(tr("登录服务连接已断开，请稍后重试。"));
  }
}

void GreeterBackend::Login(const QString& username, const QString& password,
                           const QString& session_id) {
  if (authenticating_ || login_succeeded_) return;
  if (!IsConnected()) {
    emit LoginFailed(tr("登录服务不可用。"));
    return;
  }
  if (username.trimmed().isEmpty()) {
    emit LoginFailed(tr("请输入用户名。"));
    return;
  }
  QByteArray data;
  QDataStream output(&data, QIODevice::WriteOnly);
  output << static_cast<quint32>(SDDM::GreeterMessages::Login) << username
         << password;
  if (!WriteSession(output, session_id)) {
    data.fill('\0');
    emit LoginFailed(tr("请选择可用的桌面会话。"));
    return;
  }
  // The password takes over from the reader (the daemon would cancel it
  // anyway).
  fingerprint_->Stop();
  SetAuthenticating(true);
  if (socket_.write(data) != data.size()) {
    SetAuthenticating(false);
    emit LoginFailed(tr("无法发送登录请求。"));
  }
  data.fill('\0');
  socket_.flush();
}

QStringList GreeterBackend::AvailableActions() const {
  QStringList available;
  for (const auto& action : kActions) {
    if (capabilities_ & action.capability) {
      available.append(QString::fromLatin1(action.name));
    }
  }
  return available;
}

void GreeterBackend::SetCapabilities(quint32 capabilities) {
  if (capabilities_ == capabilities) {
    return;
  }
  capabilities_ = capabilities;
  emit AvailableActionsChanged();
}

bool GreeterBackend::SendMessage(quint32 message) {
  QByteArray data;
  QDataStream output(&data, QIODevice::WriteOnly);
  output << message;
  if (socket_.state() != QLocalSocket::ConnectedState ||
      socket_.write(data) != data.size()) {
    emit RequestFailed(tr("无法发送电源请求，请稍后重试。"));
    return false;
  }
  socket_.flush();
  return true;
}

void GreeterBackend::Request(const QString& name) {
  for (const auto& action : kActions) {
    if (name != QLatin1String(action.name)) {
      continue;
    }
    if (!(capabilities_ & action.capability)) {
      emit RequestFailed(tr("当前无法执行此电源操作。"));
      return;
    }
    SendMessage(static_cast<quint32>(action.message));
    return;
  }
  emit RequestFailed(tr("未知的电源操作。"));
}

void GreeterBackend::ReadMessages() {
  while (socket_.bytesAvailable() > 0) {
    input_.startTransaction();
    quint32 message = 0;
    quint32 capabilities = 0;
    quint32 flag = 0;
    QString text;
    input_ >> message;
    if (input_.status() != QDataStream::Ok) {
      input_.rollbackTransaction();
      return;
    }
    switch (static_cast<SDDM::DaemonMessages>(message)) {
      case SDDM::DaemonMessages::Capabilities:
        input_ >> capabilities;
        break;
      case SDDM::DaemonMessages::HostName:
      case SDDM::DaemonMessages::InformationMessage:
      case SDDM::DaemonMessages::LastSession:
        input_ >> text;
        break;
      case SDDM::DaemonMessages::FingerprintAvailable:
        input_ >> text >> flag;
        break;
      case SDDM::DaemonMessages::FingerprintRejected:
        input_ >> text;
        break;
      case SDDM::DaemonMessages::FadeToBlack:
      case SDDM::DaemonMessages::PreserveBackground:
      case SDDM::DaemonMessages::LoginSucceeded:
      case SDDM::DaemonMessages::LoginFailed:
      case SDDM::DaemonMessages::FingerprintFinished:
        break;
      default:
        input_.abortTransaction();
        socket_.abort();
        SetCapabilities(0);
        emit RequestFailed(tr("电源服务返回了无法识别的消息。"));
        return;
    }
    if (!input_.commitTransaction()) {
      return;
    }
    const auto opcode = static_cast<SDDM::DaemonMessages>(message);
    if (opcode == SDDM::DaemonMessages::FadeToBlack ||
        opcode == SDDM::DaemonMessages::PreserveBackground) {
      // A matched finger starts the session like a password does.
      if ((!authenticating_ && !fingerprint_->Listening()) ||
          !transitions_enabled_) {
        continue;
      }

      transition_pending_ = true;
      preserve_background_ = opcode == SDDM::DaemonMessages::PreserveBackground;
      // The finger matched; the session is starting, nothing to cancel.
      if (!authenticating_) fingerprint_->Succeeded();
      emit PrepareTransition(preserve_background_);
    } else if (static_cast<SDDM::DaemonMessages>(message) ==
        SDDM::DaemonMessages::Capabilities) {
      SetCapabilities(capabilities);
    } else if (static_cast<SDDM::DaemonMessages>(message) ==
               SDDM::DaemonMessages::LastSession) {
      last_session_ = InitialSession(text);
      emit LastSessionChanged();
    } else if (static_cast<SDDM::DaemonMessages>(message) ==
               SDDM::DaemonMessages::LoginFailed) {
      SetAuthenticating(false);
      transition_pending_ = false;
      emit LoginFailed(tr("鉴权失败"));
    } else if (static_cast<SDDM::DaemonMessages>(message) ==
               SDDM::DaemonMessages::LoginSucceeded) {
      transition_pending_ = false;
      login_succeeded_ = true;
      SetAuthenticating(false);
      fingerprint_->Succeeded();
      emit LoginSucceeded();
    } else if (static_cast<SDDM::DaemonMessages>(message) ==
               SDDM::DaemonMessages::InformationMessage) {
      emit InformationMessage(text);
    } else if (opcode == SDDM::DaemonMessages::FingerprintAvailable) {
      fingerprint_->Available(text, flag != 0);
    } else if (opcode == SDDM::DaemonMessages::FingerprintRejected) {
      fingerprint_->Rejected(text);
    } else if (opcode == SDDM::DaemonMessages::FingerprintFinished) {
      fingerprint_->Finished();
    }
  }
}

}
