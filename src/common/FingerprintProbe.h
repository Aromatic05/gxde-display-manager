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

#ifndef GXDM_COMMON_FINGERPRINTPROBE_H_
#define GXDM_COMMON_FINGERPRINTPROBE_H_

#include <QString>

#include <functional>

class QObject;

namespace gxde {

// Asks whether `user` can log in by fingerprint: the gxdm-fingerprint PAM
// service is installed, fprintd (system bus) has a default reader, and the
// user has enrolled fingers. `done` runs once, asynchronously, in `context`'s
// thread, unless `context` is destroyed first. fprintd is D-Bus activated;
// starting it here is intended.
void ProbeEnrolledFingers(const QString& user, QObject* context,
                          std::function<void(bool available)> done);

}  // namespace gxde

#endif  // GXDM_COMMON_FINGERPRINTPROBE_H_
