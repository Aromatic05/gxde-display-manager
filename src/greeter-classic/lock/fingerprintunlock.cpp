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
#include "FingerprintProbe.h"

#include <QDebug>

namespace gxde {
namespace lock {

FingerprintUnlock::FingerprintUnlock(QObject* parent)
    : FingerprintSession(parent) {}

FingerprintUnlock::~FingerprintUnlock() {
  // Not Stop(): virtual calls are not dispatched from a destructor.
  if (auth_) {
    auth_->stop();
    delete auth_;
  }
}

void FingerprintUnlock::Probe(const QString& user) {
  ProbeEnrolledFingers(user, this, [this, user](bool available) {
    ProbeFinished(user, available);
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
    RunUnusable();
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
