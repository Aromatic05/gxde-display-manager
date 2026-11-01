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

#include "lockbackend.h"

#include "lockworker.h"
#include "mediacontroller.h"
#include "notificationcounter.h"
#include "systemvolume.h"
#include "sessionbasemodel.h"
#include "userinfo.h"
#include "userlistmodel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QUrl>

#include <unistd.h>

namespace gxde {
namespace lock {
namespace {

QString imageUri(const QString& path) {
  if (path.isEmpty()) return QString();
  if (path.startsWith(QLatin1String(":/"))) {
    return QStringLiteral("qrc") + path;
  }
  if (path.startsWith(QLatin1String("qrc:/")) ||
      path.startsWith(QLatin1String("file:"))) {
    return path;
  }
  return QUrl::fromLocalFile(path).toString();
}

}  // namespace

LockBackend::LockBackend(SessionBaseModel* model, LockWorker* worker,
                         QObject* parent)
    : QObject(parent),
      model_(model),
      worker_(worker),
      users_(new UserListModel(model, this)),
      media_(new MediaController(this)),
      audio_(new SystemVolume(this)),
      notifications_(new NotificationCounter(this)) {
  if (model_) {
    connect(model_, &SessionBaseModel::authFinished, this,
            &LockBackend::onAuthFinished);
    connect(model_, &SessionBaseModel::authFaildMessage, this,
            &LockBackend::onAuthFailedMessage);
    connect(model_, &SessionBaseModel::authFaildTipsMessage, this,
            &LockBackend::onAuthFailedTipsMessage);
    connect(model_, &SessionBaseModel::currentUserChanged, this,
            &LockBackend::onCurrentUserChanged);
    connect(model_, &SessionBaseModel::onPowerActionChanged, this,
            &LockBackend::onPowerActionChanged);
    connect(model_, &SessionBaseModel::canSleepChanged, this,
            &LockBackend::onCanSleepChanged);
    connect(model_, &SessionBaseModel::onHasSwapChanged, this,
            &LockBackend::onHasSwapChanged);
    connect(model_, &SessionBaseModel::hasVirtualKBChanged, this,
            &LockBackend::onHasVirtualKBChanged);
    connect(model_, &SessionBaseModel::allowShowUserSwitchButtonChanged, this,
            &LockBackend::onAllowShowUserSwitchButtonChanged);
    connect(model_, &SessionBaseModel::onUserListChanged, this,
            &LockBackend::onUserListChanged);
  }
  if (worker_) {
    connect(worker_, &LockWorker::powerActionRequested, this,
            &LockBackend::onPowerActionRequested);
    connect(worker_->fingerprint(), &FingerprintUnlock::StateChanged, this,
            &LockBackend::FingerprintChanged);
  }

  onCurrentUserChanged(model_ ? model_->currentUser()
                              : std::shared_ptr<User>());
  updateAvailableActions();
  updateVirtualKeyboardAvailability();
  updateUserSwitchAvailability();
}

LockBackend::~LockBackend() {
  for (const QMetaObject::Connection& connection : current_user_connections_) {
    QObject::disconnect(connection);
  }
}

QAbstractItemModel* LockBackend::Users() const {
  return users_;
}

MediaController* LockBackend::Media() const {
  return media_;
}

SystemVolume* LockBackend::Audio() const {
  return audio_;
}

NotificationCounter* LockBackend::Notifications() const {
  return notifications_;
}

int LockBackend::CurrentUserIndex() const {
  return users_ ? users_->currentUserIndex() : -1;
}

QString LockBackend::CurrentUserName() const {
  const std::shared_ptr<User> user = model_ ? model_->currentUser() : nullptr;
  return user ? user->name() : QString();
}

QString LockBackend::CurrentUserDisplayName() const {
  const std::shared_ptr<User> user = model_ ? model_->currentUser() : nullptr;
  return user ? user->displayName() : QString();
}

QString LockBackend::CurrentUserAvatar() const {
  const std::shared_ptr<User> user = model_ ? model_->currentUser() : nullptr;
  return user ? normalizeImageUri(user->avatarPath()) : QString();
}

bool LockBackend::Authenticating() const {
  return authenticating_;
}

bool LockBackend::LockedOut() const {
  return lockedOut_;
}

int LockBackend::LockMinutes() const {
  return lockMinutes_;
}

QString LockBackend::ErrorMessage() const {
  return error_message_;
}

QString LockBackend::BackgroundUri() const {
  return background_uri_;
}

QStringList LockBackend::AvailableActions() const {
  return available_actions_;
}

QString LockBackend::PendingPowerAction() const {
  return pending_power_action_;
}

bool LockBackend::VirtualKeyboardAvailable() const {
  return virtual_keyboard_available_;
}

bool LockBackend::UserSwitchAvailable() const {
  return user_switch_available_;
}

QStringList LockBackend::KbLayouts() const {
  return kb_layouts_;
}

QString LockBackend::CurrentKbLayout() const {
  return current_kb_layout_;
}

bool LockBackend::FingerprintListening() const {
  return worker_ && worker_->fingerprint()->Listening();
}

bool LockBackend::FingerprintGaveUp() const {
  return worker_ && worker_->fingerprint()->GetState() ==
                        FingerprintUnlock::State::kGaveUp;
}

void LockBackend::StartFingerprint() {
  if (worker_) worker_->startFingerprint();
}

void LockBackend::Authenticate(const QString& password) {
  if (!worker_ || !model_ || !model_->currentUser() || authenticating_ ||
      worker_->IsAuthenticating()) {
    return;
  }
  if (lockedOut_) return;

  clearErrorMessage();
  authenticating_ = true;
  emit AuthenticatingChanged();
  worker_->authUser(password);
}

void LockBackend::SelectUser(int index) {
  if (!model_ || !users_) return;

  const std::shared_ptr<User> user = users_->userAt(index);
  if (!user) return;

  // The in-session lock screen only authenticates the session's own user.
  // Other users are handled by SwitchUser().
  if (user->uid() != static_cast<uint>(getuid())) return;
  model_->setCurrentUser(user);
}

void LockBackend::SwitchUser(int index) {
  if (!worker_ || !users_) return;

  const std::shared_ptr<User> user = users_->userAt(index);
  if (!user) return;

  if (user->uid() == static_cast<uint>(getuid())) {
    SelectUser(index);
    return;
  }
  worker_->switchToUser(user);
}

void LockBackend::RequestPower(const QString& action) {
  if (!model_ || authenticating_) return;

  const SessionBaseModel::PowerAction mapped = powerActionFromString(action);
  if (mapped == SessionBaseModel::RequireNormal) return;

  clearErrorMessage();
  model_->setPowerAction(mapped);
}

void LockBackend::CancelPowerAction() {
  if (!model_) return;
  model_->setPowerAction(SessionBaseModel::RequireNormal);
  clearErrorMessage();
}

void LockBackend::SetKbLayout(const QString& layout) {
  if (!worker_ || !model_) return;

  const std::shared_ptr<User> user = model_->currentUser();
  if (!user) return;

  worker_->setLayout(user, layout);
  if (current_kb_layout_ != layout) {
    current_kb_layout_ = layout;
    emit KbLayoutsChanged();
  }
}

void LockBackend::ShowUserList() {
  if (model_) model_->setIsShow(true);
  emit ShowUserListRequested();
}

void LockBackend::NotifyUnlockFadeFinished() {
  emit UnlockFadeFinished();
}

void LockBackend::onAuthFinished(bool success) {
  authenticating_ = false;
  emit AuthenticatingChanged();

  if (success) {
    clearErrorMessage();
    emit Authenticated();
  } else {
    if (error_message_.isEmpty()) {
      // Same source text and context as LockWorker so the gxde-session-ui
      // translation covers both.
      setErrorMessage(QCoreApplication::translate("LockWorker",
                                                  "Wrong Password"));
    }
    emit AuthenticationFailed(error_message_);
  }

  updateLockState();
}

void LockBackend::onAuthFailedMessage(const QString& message) {
  setErrorMessage(message);
}

void LockBackend::onAuthFailedTipsMessage(const QString& message) {
  setErrorMessage(message);
}

void LockBackend::onCurrentUserChanged(const std::shared_ptr<User>& user) {
  for (const QMetaObject::Connection& connection : current_user_connections_) {
    QObject::disconnect(connection);
  }
  current_user_connections_.clear();

  if (user) {
    current_user_connections_
        << connect(user.get(), &User::lockChanged, this,
                   &LockBackend::onLockChanged);
    current_user_connections_
        << connect(user.get(), &User::lockBackgroundPathChanged, this,
                   &LockBackend::onLockBackgroundPathChanged);
    current_user_connections_
        << connect(user.get(), &User::kbLayoutListChanged, this,
                   &LockBackend::updateKbLayouts);
    current_user_connections_
        << connect(user.get(), &User::currentKBLayoutChanged, this,
                   &LockBackend::updateKbLayouts);
    current_user_connections_
        << connect(user.get(), &User::avatarChanged, this,
                   [this](const QString&) { emit CurrentUserChanged(); });
    current_user_connections_
        << connect(user.get(), &User::displayNameChanged, this,
                   [this](const QString&) { emit CurrentUserChanged(); });
  }

  updateBackground();
  updateLockState();
  updateKbLayouts();
  emit CurrentUserChanged();
}

void LockBackend::onPowerActionChanged(
    SessionBaseModel::PowerAction action) {
  const QString action_name = powerActionToString(action);
  if (pending_power_action_ == action_name) return;

  pending_power_action_ = action_name;
  emit PendingPowerActionChanged();
}

void LockBackend::onPowerActionRequested(const QString& action) {
  authenticating_ = false;
  emit AuthenticatingChanged();

  pending_power_action_ = action;
  emit PendingPowerActionChanged();
  emit PowerActionStarted(action);
}

void LockBackend::onCanSleepChanged(bool canSleep) {
  Q_UNUSED(canSleep)
  updateAvailableActions();
}

void LockBackend::onHasSwapChanged(bool hasSwap) {
  Q_UNUSED(hasSwap)
  updateAvailableActions();
}

void LockBackend::onHasVirtualKBChanged(bool hasVirtualKB) {
  Q_UNUSED(hasVirtualKB)
  updateVirtualKeyboardAvailability();
}

void LockBackend::onAllowShowUserSwitchButtonChanged(bool allow) {
  Q_UNUSED(allow)
  updateUserSwitchAvailability();
}

void LockBackend::onUserListChanged(
    const QList<std::shared_ptr<User>>& users) {
  Q_UNUSED(users)
  updateUserSwitchAvailability();
  emit UsersChanged();
}

void LockBackend::onLockChanged(bool locked) {
  Q_UNUSED(locked)
  updateLockState();
}

void LockBackend::onLockBackgroundPathChanged(const QString& path) {
  Q_UNUSED(path)
  updateBackground();
}

void LockBackend::updateAvailableActions() {
  if (!model_) return;

  QStringList actions;
  actions << QStringLiteral("shutdown") << QStringLiteral("reboot");
  if (model_->canSleep()) actions << QStringLiteral("suspend");
  if (model_->hasSwap()) actions << QStringLiteral("hibernate");

  if (actions == available_actions_) return;
  available_actions_ = actions;
  emit AvailableActionsChanged();
}

void LockBackend::updateBackground() {
  const std::shared_ptr<User> user = model_ ? model_->currentUser() : nullptr;
  const QString uri =
      user ? VersionedImageUri(user->lockBackgroundPath()) : QString();
  if (uri == background_uri_) return;

  background_uri_ = uri;
  emit BackgroundChanged();
}

void LockBackend::updateLockState() {
  const std::shared_ptr<User> user = model_ ? model_->currentUser() : nullptr;
  const bool locked = user && user->isLock();
  const int minutes = user ? static_cast<int>(user->lockNum()) : 0;

  if (locked == lockedOut_ && minutes == lockMinutes_) return;
  lockedOut_ = locked;
  lockMinutes_ = minutes;
  emit LockedOutChanged();
}

void LockBackend::updateUserSwitchAvailability() {
  if (!model_) return;

  const bool available =
      model_->alwaysShowUserSwitchButton() ||
      (model_->allowShowUserSwitchButton() && model_->userList().size() > 1);
  if (available == user_switch_available_) return;

  user_switch_available_ = available;
  emit UserSwitchAvailableChanged();
}

void LockBackend::updateVirtualKeyboardAvailability() {
  if (!model_) return;

  const bool available = model_->hasVirtualKB();
  if (available == virtual_keyboard_available_) return;

  virtual_keyboard_available_ = available;
  emit VirtualKeyboardAvailableChanged();
}

void LockBackend::updateKbLayouts() {
  const std::shared_ptr<User> user = model_ ? model_->currentUser() : nullptr;
  const QStringList layouts = user ? user->kbLayoutList() : QStringList();
  const QString current = user ? user->currentKBLayout() : QString();

  if (layouts == kb_layouts_ && current == current_kb_layout_) return;
  kb_layouts_ = layouts;
  current_kb_layout_ = current;
  emit KbLayoutsChanged();
}

void LockBackend::setErrorMessage(const QString& message) {
  if (error_message_ == message) return;
  error_message_ = message;
  emit ErrorMessageChanged();
}

void LockBackend::clearErrorMessage() {
  setErrorMessage(QString());
}

QString LockBackend::normalizeImageUri(const QString& path) {
  return imageUri(path);
}

QString LockBackend::VersionedImageUri(const QString& path) {
  const QString uri = imageUri(path);
  const QUrl url(uri);
  if (!url.isLocalFile()) return uri;
  const QFileInfo info(url.toLocalFile());
  if (!info.exists()) return uri;
  QUrl versioned = url;
  versioned.setQuery(QStringLiteral("v=%1-%2")
                         .arg(info.lastModified().toMSecsSinceEpoch())
                         .arg(info.size()));
  return versioned.toString();
}

QString LockBackend::powerActionToString(
    SessionBaseModel::PowerAction action) {
  switch (action) {
    case SessionBaseModel::RequireShutdown:
      return QStringLiteral("shutdown");
    case SessionBaseModel::RequireRestart:
      return QStringLiteral("reboot");
    case SessionBaseModel::RequireSuspend:
      return QStringLiteral("suspend");
    case SessionBaseModel::RequireHibernate:
      return QStringLiteral("hibernate");
    case SessionBaseModel::RequireNormal:
    default:
      return QString();
  }
}

SessionBaseModel::PowerAction LockBackend::powerActionFromString(
    const QString& action) {
  const QString normalized = action.trimmed().toLower();
  if (normalized == QLatin1String("shutdown") ||
      normalized == QLatin1String("poweroff")) {
    return SessionBaseModel::RequireShutdown;
  }
  if (normalized == QLatin1String("reboot") ||
      normalized == QLatin1String("restart")) {
    return SessionBaseModel::RequireRestart;
  }
  if (normalized == QLatin1String("suspend")) {
    return SessionBaseModel::RequireSuspend;
  }
  if (normalized == QLatin1String("hibernate")) {
    return SessionBaseModel::RequireHibernate;
  }
  return SessionBaseModel::RequireNormal;
}

}  // namespace lock
}  // namespace gxde
