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

#include "FingerprintSession.h"

#include <QPointer>

namespace SDDM {
class Auth;
}

namespace gxde {
namespace lock {

// Fingerprint unlock for the locked user. Each run is gxdm-helper with the
// gxdm-fingerprint PAM service, unprivileged and auth-only like the password
// unlock; fprintd's polkit policy lets an active session verify its own user.
class FingerprintUnlock : public FingerprintSession {
  Q_OBJECT

 public:
  explicit FingerprintUnlock(QObject* parent = nullptr);
  ~FingerprintUnlock() override;

 protected:
  void Probe(const QString& user) override;
  void StartRun(const QString& user) override;
  void StopRun() override;

 private:
  QPointer<SDDM::Auth> auth_;
};

}  // namespace lock
}  // namespace gxde

#endif  // GXDM_LOCK_FINGERPRINTUNLOCK_H_
