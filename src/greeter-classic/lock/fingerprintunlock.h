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

#ifndef GXDM_LOCK_FINGERPRINTUNLOCK_H_
#define GXDM_LOCK_FINGERPRINTUNLOCK_H_

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>

namespace SDDM {
class Auth;
}

namespace gxde {
namespace lock {

// Fingerprint authentication for the locked user, run alongside the password.
//
// Each run is gxdm-helper with the gxdm-fingerprint PAM service (pam_fprintd),
// unprivileged and auth-only like the password unlock. fprintd's polkit
// policy lets an active session verify its own user.
//
// pam_fprintd ends a run in three ways, told apart by timing:
//  - a match;
//  - its tries are used up: it fails right after the last mismatch. The run
//    stops until Start() is called again, so a wrong finger cannot keep the
//    reader busy;
//  - no finger before its timeout (30 s by default): the run is restarted
//    quietly, so the reader keeps listening while the screen stays locked.
// A run that fails quickly without any scan means there is nothing to listen
// with (no reader, no prints, no PAM service): the user is marked unavailable.
class FingerprintUnlock : public QObject {
  Q_OBJECT

 public:
  enum class State {
    kIdle,         // not started, or stopped
    kProbing,      // asking fprintd for a reader and enrolled fingers
    kListening,    // a run is waiting for a finger
    kGaveUp,       // pam_fprintd's tries are used up; Start() re-arms
    kUnavailable,  // no reader or no enrolled fingers for this user
  };
  Q_ENUM(State)

  explicit FingerprintUnlock(QObject* parent = nullptr);
  ~FingerprintUnlock() override;

  State GetState() const { return state_; }
  bool Listening() const { return state_ == State::kListening; }

  // Probes fprintd, then listens. Does nothing while already probing or
  // listening for the same user.
  void Start(const QString& user);
  void Stop();

  // Below this, a failed run is "quick" (see the class comment). pam_fprintd
  // accepts no timeout under 10 s.
  void SetQuickFailureMs(qint64 ms) { quick_failure_ms_ = ms; }

 signals:
  void StateChanged();
  void Matched();
  // One rejected scan, with pam_fprintd's (translated) message. The run goes
  // on unless its tries are used up.
  void Rejected(const QString& message);

 protected:
  // Seams for tests. The defaults talk to fprintd and gxdm-helper.
  // Probe() must answer with ProbeFinished(); StartRun() with RunRejected()
  // and RunFinished().
  virtual void Probe(const QString& user);
  virtual void StartRun(const QString& user);
  virtual void StopRun();

  void ProbeFinished(const QString& user, bool available);
  void RunRejected(const QString& message);
  void RunFinished(bool matched);

 private:
  void BeginRun();
  void SetState(State state);

  State state_ = State::kIdle;
  QString user_;
  qint64 quick_failure_ms_ = 6000;
  QElapsedTimer since_event_;  // since the run started or the last rejection
  int rejections_ = 0;
  QPointer<SDDM::Auth> auth_;
};

}  // namespace lock
}  // namespace gxde

#endif  // GXDM_LOCK_FINGERPRINTUNLOCK_H_
