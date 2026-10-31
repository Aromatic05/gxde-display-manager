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

#include "fingerprintunlock.h"

#include "Auth.h"
#include "AuthPrompt.h"
#include "AuthRequest.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>

namespace gxde {
namespace lock {
namespace {

const QString kFprintService = QStringLiteral("net.reactivated.Fprint");
const QString kManagerPath = QStringLiteral("/net/reactivated/Fprint/Manager");
const QString kManagerInterface =
    QStringLiteral("net.reactivated.Fprint.Manager");
const QString kDeviceInterface =
    QStringLiteral("net.reactivated.Fprint.Device");

}  // namespace

FingerprintUnlock::FingerprintUnlock(QObject* parent) : QObject(parent) {}

FingerprintUnlock::~FingerprintUnlock() {
  // Not Stop(): virtual calls are not dispatched from a destructor.
  if (auth_) {
    auth_->stop();
    delete auth_;
  }
}

void FingerprintUnlock::Start(const QString& user) {
  if (user.isEmpty()) return;
  if (user == user_ &&
      (state_ == State::kProbing || state_ == State::kListening)) {
    return;
  }
  Stop();
  user_ = user;
  // Probe every time: a reader may have been plugged in, or a finger
  // enrolled, since the last attempt.
  SetState(State::kProbing);
  Probe(user_);
}

void FingerprintUnlock::Stop() {
  if (state_ == State::kListening) StopRun();
  if (state_ == State::kProbing || state_ == State::kListening) {
    SetState(State::kIdle);
  }
}

void FingerprintUnlock::ProbeFinished(const QString& user, bool available) {
  if (state_ != State::kProbing || user != user_) return;
  if (!available) {
    SetState(State::kUnavailable);
    return;
  }
  BeginRun();
}

void FingerprintUnlock::BeginRun() {
  rejections_ = 0;
  since_event_.start();
  SetState(State::kListening);
  StartRun(user_);
}

void FingerprintUnlock::RunRejected(const QString& message) {
  if (state_ != State::kListening) return;
  ++rejections_;
  since_event_.start();
  emit Rejected(message);
}

void FingerprintUnlock::RunFinished(bool matched) {
  if (state_ != State::kListening) return;
  StopRun();
  if (matched) {
    SetState(State::kIdle);
    emit Matched();
    return;
  }
  if (since_event_.elapsed() >= quick_failure_ms_) {
    // pam_fprintd timed out waiting for a finger: keep listening.
    BeginRun();
    return;
  }
  if (rejections_ > 0) {
    SetState(State::kGaveUp);
    return;
  }
  qWarning() << "(FingerprintUnlock) fingerprint authentication failed"
                " without a scan; not listening for" << user_;
  SetState(State::kUnavailable);
}

void FingerprintUnlock::SetState(State state) {
  if (state == state_) return;
  state_ = state;
  emit StateChanged();
}

void FingerprintUnlock::Probe(const QString& user) {
  // Manager.GetDefaultDevice, then Device.ListEnrolledFingers(user). fprintd
  // is D-Bus activated, so starting it here is intended. Either call fails
  // (NoSuchDevice, NoEnrolledPrints, no fprintd) when there is nothing to
  // listen with.
  QDBusConnection bus = QDBusConnection::systemBus();
  if (!bus.isConnected()) {
    // Calls on a dead connection never report back.
    qDebug() << "(FingerprintUnlock) no system bus";
    ProbeFinished(user, false);
    return;
  }
  const QDBusMessage get_device = QDBusMessage::createMethodCall(
      kFprintService, kManagerPath, kManagerInterface,
      QStringLiteral("GetDefaultDevice"));
  auto* watcher =
      new QDBusPendingCallWatcher(bus.asyncCall(get_device), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, user, bus](QDBusPendingCallWatcher* call) mutable {
            const QDBusPendingReply<QDBusObjectPath> device = *call;
            call->deleteLater();
            if (device.isError()) {
              qDebug() << "(FingerprintUnlock) no fingerprint reader:"
                       << device.error().name();
              ProbeFinished(user, false);
              return;
            }
            QDBusMessage list = QDBusMessage::createMethodCall(
                kFprintService, device.value().path(), kDeviceInterface,
                QStringLiteral("ListEnrolledFingers"));
            list << user;
            auto* next =
                new QDBusPendingCallWatcher(bus.asyncCall(list), this);
            connect(next, &QDBusPendingCallWatcher::finished, this,
                    [this, user](QDBusPendingCallWatcher* reply_call) {
                      const QDBusPendingReply<QStringList> fingers =
                          *reply_call;
                      reply_call->deleteLater();
                      if (fingers.isError()) {
                        qDebug() << "(FingerprintUnlock) no enrolled fingers:"
                                 << fingers.error().name();
                      }
                      ProbeFinished(user, !fingers.isError() &&
                                              !fingers.value().isEmpty());
                    });
          });
}

void FingerprintUnlock::StartRun(const QString& user) {
  auto* auth = new SDDM::Auth(this);
  auth_ = auth;
  auth->setVerbose(true);
  auth->setFingerprint(true);
  auth->setUser(user);
  // Signals from a run that has already been replaced are ignored.
  connect(auth, &SDDM::Auth::error, this,
          [this, auth](const QString& message, SDDM::Auth::Error type) {
            // Conversation errors: mismatches and bad scans.
            if (auth == auth_ && type == SDDM::Auth::ERROR_AUTHENTICATION) {
              RunRejected(message);
            }
          });
  connect(auth, &SDDM::Auth::info, this,
          [](const QString& message, SDDM::Auth::Info) {
            // "Place your finger on <device>", "Verification timed out": the
            // UI shows its own hint.
            qDebug() << "(FingerprintUnlock) pam_fprintd:" << message;
          });
  connect(auth, &SDDM::Auth::requestChanged, this, [this, auth] {
    if (auth != auth_ || !auth->request() ||
        auth->request()->prompts().isEmpty()) {
      return;
    }
    // The fingerprint stack asks nothing. A prompt means a misconfigured
    // gxdm-fingerprint service; never answer it.
    qWarning() << "(FingerprintUnlock) gxdm-fingerprint asked for input;"
                  " check /etc/pam.d/gxdm-fingerprint";
    StopRun();
    SetState(State::kUnavailable);
  });
  connect(auth, &SDDM::Auth::authentication, this,
          [this, auth](const QString&, bool success) {
            if (auth == auth_) RunFinished(success);
          });
  // The helper died without reporting a result.
  connect(auth, &SDDM::Auth::finished, this,
          [this, auth](SDDM::Auth::HelperExitStatus) {
            if (auth == auth_) RunFinished(false);
          });
  auth->start();
}

void FingerprintUnlock::StopRun() {
  if (!auth_) return;
  SDDM::Auth* auth = auth_;
  auth_ = nullptr;
  // Terminating the helper drops its fprintd connection, which releases the
  // reader.
  auth->stop();
  auth->deleteLater();
}

}  // namespace lock
}  // namespace gxde
