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

#ifndef SRC_LOCK_COMMON_NOTIFICATIONCOUNTER_H_
#define SRC_LOCK_COMMON_NOTIFICATIONCOUNTER_H_

#include <QDBusConnection>
#include <QObject>
#include <QString>

namespace gxde {
namespace lock {

class NotificationCounter : public QObject {
  Q_OBJECT

  Q_PROPERTY(bool available READ Available NOTIFY Changed)
  Q_PROPERTY(int count READ Count NOTIFY Changed)

 public:
  explicit NotificationCounter(QObject* parent = nullptr);
  explicit NotificationCounter(const QDBusConnection& bus,
                               QObject* parent = nullptr);

  bool Available() const { return available_; }
  int Count() const { return count_; }

  // Starts counting from zero; called whenever the lock screen is shown.
  Q_INVOKABLE void Reset();

 signals:
  void Changed();

 private slots:
  void onNameOwnerChanged(const QString& name, const QString& oldOwner,
                          const QString& newOwner);
  void onRecordAdded(const QString& record);

 private:
  void setAvailable(bool available);

  QDBusConnection bus_;
  bool available_ = false;
  int count_ = 0;
};

}  // namespace lock
}  // namespace gxde

#endif  // SRC_LOCK_COMMON_NOTIFICATIONCOUNTER_H_
