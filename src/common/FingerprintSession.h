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

#ifndef GXDM_COMMON_FINGERPRINTSESSION_H_
#define GXDM_COMMON_FINGERPRINTSESSION_H_

#include <QElapsedTimer>
#include <QObject>
#include <QString>

namespace gxde {

// Listens for one user's finger alongside the password: the lock screen
// (gxde::lock::FingerprintUnlock) and the greeter (through the daemon).
//
// Subclasses supply the probe and the runs. A run is one pass of the
// gxdm-fingerprint PAM service (pam_fprintd), which ends in one of three ways,
// told apart by timing:
//  - a match;
//  - its tries are used up: it fails right after the last rejection. Nothing
//    is started again until Start(), so a wrong finger cannot keep the reader
//    busy;
//  - no finger before pam_fprintd's timeout (30 s by default, never under
//    10 s): the run is restarted quietly, so the reader keeps listening.
// A run that fails quickly without any scan means there is nothing to listen
// with (no reader, no prints, no PAM service): the user is marked unavailable.
class FingerprintSession : public QObject {
  Q_OBJECT

 public:
  enum class State {
    kIdle,         // not started, or stopped
    kProbing,      // asking for a reader and enrolled fingers
    kListening,    // a run is waiting for a finger
    kGaveUp,       // pam_fprintd's tries are used up; Start() re-arms
    kUnavailable,  // no reader or no enrolled fingers for this user
  };
  Q_ENUM(State)

  explicit FingerprintSession(QObject* parent = nullptr);

  State GetState() const { return state_; }
  bool Listening() const { return state_ == State::kListening; }
  const QString& User() const { return user_; }

  // Probes, then listens. Does nothing while already probing or listening
  // for the same user.
  void Start(const QString& user);
  void Stop();

  // Below this, a failed run is "quick" (see the class comment).
  void SetQuickFailureMs(qint64 ms) { quick_failure_ms_ = ms; }

 signals:
  void StateChanged();
  void Matched();
  // One rejected scan, with pam_fprintd's (translated) message. The run goes
  // on unless its tries are used up.
  void Rejected(const QString& message);

 protected:
  // Probe() must answer with ProbeFinished(); StartRun() with RunRejected()
  // and RunFinished(). StopRun() ends the current run; its late results are
  // ignored.
  virtual void Probe(const QString& user) = 0;
  virtual void StartRun(const QString& user) = 0;
  virtual void StopRun() = 0;

  void ProbeFinished(const QString& user, bool available);
  void RunRejected(const QString& message);
  void RunFinished(bool matched);
  // The run cannot work (e.g. the PAM stack asked for input): stop for good.
  void RunUnusable();

 private:
  void BeginRun();
  void SetState(State state);

  State state_ = State::kIdle;
  QString user_;
  // pam_fprintd fails about 2 s after its last try; its timeout is >= 10 s.
  qint64 quick_failure_ms_ = 6000;
  QElapsedTimer since_event_;  // since the run started or the last rejection
  int rejections_ = 0;
};

}  // namespace gxde

#endif  // GXDM_COMMON_FINGERPRINTSESSION_H_
