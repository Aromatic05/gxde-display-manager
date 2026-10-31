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

#ifndef SRC_LOCK_COMMON_LOCKBACKEND_H_
#define SRC_LOCK_COMMON_LOCKBACKEND_H_

#include <QAbstractItemModel>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QStringList>
#include <memory>

#include "sessionbasemodel.h"

class LockWorker;
class User;

namespace gxde {
namespace lock {

class MediaController;
class NotificationCounter;
class SystemVolume;
class UserListModel;

// QML-safe facade over SessionBaseModel and LockWorker. The QML lock screen
// never touches the legacy widget model directly.
class LockBackend : public QObject {
  Q_OBJECT
  Q_MOC_INCLUDE("mediacontroller.h")
  Q_MOC_INCLUDE("notificationcounter.h")
  Q_MOC_INCLUDE("systemvolume.h")

  Q_PROPERTY(QAbstractItemModel* users READ Users CONSTANT)
  Q_PROPERTY(gxde::lock::MediaController* media READ Media CONSTANT)
  Q_PROPERTY(gxde::lock::SystemVolume* audio READ Audio CONSTANT)
  Q_PROPERTY(gxde::lock::NotificationCounter* notifications READ Notifications
                 CONSTANT)
  Q_PROPERTY(int currentUserIndex READ CurrentUserIndex NOTIFY CurrentUserChanged)
  Q_PROPERTY(QString currentUserName READ CurrentUserName NOTIFY CurrentUserChanged)
  Q_PROPERTY(QString currentUserDisplayName READ CurrentUserDisplayName NOTIFY CurrentUserChanged)
  Q_PROPERTY(QString currentUserAvatar READ CurrentUserAvatar NOTIFY CurrentUserChanged)
  Q_PROPERTY(bool authenticating READ Authenticating NOTIFY AuthenticatingChanged)
  Q_PROPERTY(bool lockedOut READ LockedOut NOTIFY LockedOutChanged)
  Q_PROPERTY(int lockMinutes READ LockMinutes NOTIFY LockedOutChanged)
  Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY ErrorMessageChanged)
  Q_PROPERTY(QString backgroundUri READ BackgroundUri NOTIFY BackgroundChanged)
  Q_PROPERTY(QStringList availableActions READ AvailableActions NOTIFY AvailableActionsChanged)
  Q_PROPERTY(QString pendingPowerAction READ PendingPowerAction NOTIFY PendingPowerActionChanged)
  Q_PROPERTY(bool virtualKeyboardAvailable READ VirtualKeyboardAvailable NOTIFY VirtualKeyboardAvailableChanged)
  Q_PROPERTY(bool userSwitchAvailable READ UserSwitchAvailable NOTIFY UserSwitchAvailableChanged)
  Q_PROPERTY(QStringList kbLayouts READ KbLayouts NOTIFY KbLayoutsChanged)
  Q_PROPERTY(QString currentKbLayout READ CurrentKbLayout NOTIFY KbLayoutsChanged)
  // A finger on the reader unlocks (runs alongside the password).
  Q_PROPERTY(bool fingerprintListening READ FingerprintListening NOTIFY FingerprintChanged)
  // pam_fprintd's tries are used up; StartFingerprint() re-arms.
  Q_PROPERTY(bool fingerprintGaveUp READ FingerprintGaveUp NOTIFY FingerprintChanged)

 public:
  explicit LockBackend(SessionBaseModel* model, LockWorker* worker,
                       QObject* parent = nullptr);
  ~LockBackend() override;

  QAbstractItemModel* Users() const;
  MediaController* Media() const;
  SystemVolume* Audio() const;
  NotificationCounter* Notifications() const;
  int CurrentUserIndex() const;
  QString CurrentUserName() const;
  QString CurrentUserDisplayName() const;
  QString CurrentUserAvatar() const;
  bool Authenticating() const;
  bool LockedOut() const;
  int LockMinutes() const;
  QString ErrorMessage() const;
  QString BackgroundUri() const;
  QStringList AvailableActions() const;
  QString PendingPowerAction() const;
  bool VirtualKeyboardAvailable() const;
  bool UserSwitchAvailable() const;
  QStringList KbLayouts() const;
  QString CurrentKbLayout() const;
  bool FingerprintListening() const;
  bool FingerprintGaveUp() const;

  Q_INVOKABLE void Authenticate(const QString& password);
  Q_INVOKABLE void SelectUser(int index);
  Q_INVOKABLE void SwitchUser(int index);
  Q_INVOKABLE void RequestPower(const QString& action);
  Q_INVOKABLE void CancelPowerAction();
  Q_INVOKABLE void SetKbLayout(const QString& layout);
  Q_INVOKABLE void ShowUserList();
  Q_INVOKABLE void NotifyUnlockFadeFinished();
  // Listens for a finger again if it is not already (no-op otherwise).
  Q_INVOKABLE void StartFingerprint();

 signals:
  void Authenticated();
  void AuthenticationFailed(const QString& message);
  void UsersChanged();
  void CurrentUserChanged();
  void AuthenticatingChanged();
  void LockedOutChanged();
  void ErrorMessageChanged();
  void BackgroundChanged();
  void AvailableActionsChanged();
  void PendingPowerActionChanged();
  void PowerActionStarted(const QString& action);
  void VirtualKeyboardAvailableChanged();
  void UserSwitchAvailableChanged();
  void KbLayoutsChanged();
  void ShowUserListRequested();
  void UnlockFadeFinished();
  void FingerprintChanged();

 private slots:
  void onAuthFinished(bool success);
  void onAuthFailedMessage(const QString& message);
  void onAuthFailedTipsMessage(const QString& message);
  void onCurrentUserChanged(const std::shared_ptr<User>& user);
  void onPowerActionChanged(SessionBaseModel::PowerAction action);
  void onPowerActionRequested(const QString& action);
  void onCanSleepChanged(bool canSleep);
  void onHasSwapChanged(bool hasSwap);
  void onHasVirtualKBChanged(bool hasVirtualKB);
  void onAllowShowUserSwitchButtonChanged(bool allow);
  void onUserListChanged(const QList<std::shared_ptr<User>>& users);
  void onLockChanged(bool locked);
  void onLockBackgroundPathChanged(const QString& path);

 private:
  void updateAvailableActions();
  void updateBackground();
  void updateLockState();
  void updateUserSwitchAvailability();
  void updateVirtualKeyboardAvailability();
  void updateKbLayouts();
  void setErrorMessage(const QString& message);
  void clearErrorMessage();
  static QString normalizeImageUri(const QString& path);
  static QString powerActionToString(SessionBaseModel::PowerAction action);
  static SessionBaseModel::PowerAction powerActionFromString(
      const QString& action);

  SessionBaseModel* model_;
  LockWorker* worker_;
  UserListModel* users_;
  MediaController* media_;
  SystemVolume* audio_;
  NotificationCounter* notifications_;
  QList<QMetaObject::Connection> current_user_connections_;
  bool authenticating_ = false;
  bool lockedOut_ = false;
  int lockMinutes_ = 0;
  QString error_message_;
  QString background_uri_;
  QStringList available_actions_;
  QString pending_power_action_;
  bool virtual_keyboard_available_ = false;
  bool user_switch_available_ = false;
  QStringList kb_layouts_;
  QString current_kb_layout_;
};

}  // namespace lock
}  // namespace gxde

#endif  // SRC_LOCK_COMMON_LOCKBACKEND_H_
