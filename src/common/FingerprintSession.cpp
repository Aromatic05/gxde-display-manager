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

#include "FingerprintSession.h"

#include <QDebug>

namespace gxde {

FingerprintSession::FingerprintSession(QObject* parent) : QObject(parent) {}

void FingerprintSession::Start(const QString& user) {
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

void FingerprintSession::Stop() {
  if (state_ == State::kListening) StopRun();
  if (state_ == State::kProbing || state_ == State::kListening) {
    SetState(State::kIdle);
  }
}

void FingerprintSession::ProbeFinished(const QString& user, bool available) {
  if (state_ != State::kProbing || user != user_) return;
  if (!available) {
    SetState(State::kUnavailable);
    return;
  }
  BeginRun();
}

void FingerprintSession::BeginRun() {
  rejections_ = 0;
  since_event_.start();
  SetState(State::kListening);
  StartRun(user_);
}

void FingerprintSession::RunRejected(const QString& message) {
  if (state_ != State::kListening) return;
  ++rejections_;
  since_event_.start();
  emit Rejected(message);
}

void FingerprintSession::RunFinished(bool matched) {
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
  qWarning() << "(Fingerprint) authentication failed without a scan;"
                " not listening for" << user_;
  SetState(State::kUnavailable);
}

void FingerprintSession::RunUnusable() {
  if (state_ != State::kListening) return;
  StopRun();
  SetState(State::kUnavailable);
}

void FingerprintSession::SetState(State state) {
  if (state == state_) return;
  state_ = state;
  emit StateChanged();
}

}  // namespace gxde
