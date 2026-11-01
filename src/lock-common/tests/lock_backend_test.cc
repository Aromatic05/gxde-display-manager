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

#include <QtTest>

#include <QPainter>
#include <QSignalSpy>

#include <unistd.h>

#include "lockbackend.h"
#include "lockworker.h"
#include "sessionbasemodel.h"
#include "userinfo.h"
#include "userlistmodel.h"

namespace {

class FakeLockWorker : public LockWorker {
 public:
  explicit FakeLockWorker(SessionBaseModel* model) : LockWorker(model) {}

  void authUser(const QString& password) override {
    last_password = password;
    ++auth_calls;
    authenticating = true;
  }

  void switchToUser(std::shared_ptr<User> user) override {
    switched_user = user;
  }

  void setLayout(std::shared_ptr<User> user, const QString& layout) override {
    Q_UNUSED(user)
    last_layout = layout;
  }

  bool IsAuthenticating() const override { return authenticating; }

  void emitPowerActionRequested(const QString& action) {
    emit powerActionRequested(action);
  }

  QString last_password;
  int auth_calls = 0;
  bool authenticating = false;
  std::shared_ptr<User> switched_user;
  QString last_layout;
};

}  // namespace

class LockBackendTest : public QObject {
  Q_OBJECT

 private slots:
  void userModelMirrorsSessionModel();
  void authenticateTracksStateAndErrors();
  void powerActionsMapAndCancel();
  void availableActionsFollowPowerCapabilities();
  void powerActionStartedIsForwarded();
  void setKbLayoutAndShowUserList();
  void switchUserUsesWorker();
  void lockedOutStateFollowsUser();
  void rewrittenWallpaperGetsNewUri();
  void measuresBrightnessBehindTheClock();
};

void LockBackendTest::userModelMirrorsSessionModel() {
  SessionBaseModel model(SessionBaseModel::LockType);
  FakeLockWorker worker(&model);
  gxde::lock::LockBackend backend(&model, &worker);

  QAbstractItemModel* users = backend.Users();
  QVERIFY(users);
  QCOMPARE(users->rowCount(), model.userList().size());

  const int current = backend.CurrentUserIndex();
  QVERIFY(current >= 0);
  QCOMPARE(backend.CurrentUserName(), model.currentUser()->name());
  QCOMPARE(backend.CurrentUserDisplayName(),
           model.currentUser()->displayName());

  const QModelIndex index = users->index(current, 0);
  QCOMPARE(users->data(index, gxde::lock::UserListModel::UserNameRole)
               .toString(),
           backend.CurrentUserName());
  QVERIFY(users->data(index, gxde::lock::UserListModel::IsCurrentRole)
              .toBool());

  const QString avatar =
      users->data(index, gxde::lock::UserListModel::AvatarRole).toString();
  QVERIFY(avatar.isEmpty() || avatar.startsWith(QLatin1String("qrc:/")) ||
          avatar.startsWith(QLatin1String("file:")));
  QVERIFY(backend.BackgroundUri().isEmpty() ||
          backend.BackgroundUri().startsWith(QLatin1String("qrc:/")) ||
          backend.BackgroundUri().startsWith(QLatin1String("file:")));
}

void LockBackendTest::authenticateTracksStateAndErrors() {
  SessionBaseModel model(SessionBaseModel::LockType);
  FakeLockWorker worker(&model);
  gxde::lock::LockBackend backend(&model, &worker);

  QSignalSpy authenticated(&backend,
                           &gxde::lock::LockBackend::Authenticated);
  QSignalSpy failed(&backend,
                    &gxde::lock::LockBackend::AuthenticationFailed);

  backend.Authenticate(QStringLiteral("secret"));
  QCOMPARE(worker.auth_calls, 1);
  QCOMPARE(worker.last_password, QStringLiteral("secret"));
  QVERIFY(backend.Authenticating());

  worker.authenticating = false;
  emit model.authFinished(true);
  QVERIFY(!backend.Authenticating());
  QCOMPARE(authenticated.count(), 1);
  QCOMPARE(backend.ErrorMessage(), QString());

  backend.Authenticate(QStringLiteral("wrong"));
  QCOMPARE(worker.auth_calls, 2);
  worker.authenticating = false;
  emit model.authFinished(false);
  QVERIFY(!backend.Authenticating());
  QCOMPARE(failed.count(), 1);
  QCOMPARE(backend.ErrorMessage(), QStringLiteral("Wrong Password"));

  backend.Authenticate(QStringLiteral("secret"));
  QCOMPARE(backend.ErrorMessage(), QString());
}

void LockBackendTest::powerActionsMapAndCancel() {
  SessionBaseModel model(SessionBaseModel::LockType);
  FakeLockWorker worker(&model);
  gxde::lock::LockBackend backend(&model, &worker);

  QSignalSpy pending(&backend,
                     &gxde::lock::LockBackend::PendingPowerActionChanged);

  backend.RequestPower(QStringLiteral("shutdown"));
  QCOMPARE(model.powerAction(), SessionBaseModel::RequireShutdown);
  QCOMPARE(backend.PendingPowerAction(), QStringLiteral("shutdown"));
  QCOMPARE(pending.count(), 1);

  backend.RequestPower(QStringLiteral("reboot"));
  QCOMPARE(model.powerAction(), SessionBaseModel::RequireRestart);
  QCOMPARE(backend.PendingPowerAction(), QStringLiteral("reboot"));

  backend.CancelPowerAction();
  QCOMPARE(model.powerAction(), SessionBaseModel::RequireNormal);
  QCOMPARE(backend.PendingPowerAction(), QString());
}

void LockBackendTest::availableActionsFollowPowerCapabilities() {
  SessionBaseModel model(SessionBaseModel::LockType);
  FakeLockWorker worker(&model);
  gxde::lock::LockBackend backend(&model, &worker);

  // LockWorker initializes canSleep from the current user's settings, so
  // start from a known capability state.
  model.setCanSleep(false);
  model.setHasSwap(false);

  QStringList actions = backend.AvailableActions();
  QVERIFY(actions.contains(QStringLiteral("shutdown")));
  QVERIFY(actions.contains(QStringLiteral("reboot")));
  QVERIFY(!actions.contains(QStringLiteral("suspend")));

  model.setCanSleep(true);
  model.setHasSwap(true);

  actions = backend.AvailableActions();
  QVERIFY(actions.contains(QStringLiteral("suspend")));
  QVERIFY(actions.contains(QStringLiteral("hibernate")));
}

void LockBackendTest::powerActionStartedIsForwarded() {
  SessionBaseModel model(SessionBaseModel::LockType);
  FakeLockWorker worker(&model);
  gxde::lock::LockBackend backend(&model, &worker);

  QSignalSpy started(&backend,
                     &gxde::lock::LockBackend::PowerActionStarted);

  worker.emitPowerActionRequested(QStringLiteral("hibernate"));
  QCOMPARE(started.count(), 1);
  QCOMPARE(backend.PendingPowerAction(), QStringLiteral("hibernate"));
  QVERIFY(!backend.Authenticating());
}

void LockBackendTest::setKbLayoutAndShowUserList() {
  SessionBaseModel model(SessionBaseModel::LockType);
  FakeLockWorker worker(&model);
  gxde::lock::LockBackend backend(&model, &worker);

  QSignalSpy show(&backend,
                  &gxde::lock::LockBackend::ShowUserListRequested);

  backend.SetKbLayout(QStringLiteral("us"));
  QCOMPARE(worker.last_layout, QStringLiteral("us"));
  QCOMPARE(backend.CurrentKbLayout(), QStringLiteral("us"));

  backend.ShowUserList();
  QVERIFY(model.isShow());
  QCOMPARE(show.count(), 1);
}

void LockBackendTest::switchUserUsesWorker() {
  SessionBaseModel model(SessionBaseModel::LockType);
  FakeLockWorker worker(&model);
  gxde::lock::LockBackend backend(&model, &worker);

  QAbstractItemModel* users = backend.Users();
  QVERIFY(users);

  int other = -1;
  for (int i = 0; i < users->rowCount(); ++i) {
    const uint uid = users->data(users->index(i, 0),
                                 gxde::lock::UserListModel::UidRole)
                         .toUInt();
    if (uid != static_cast<uint>(getuid())) {
      other = i;
      break;
    }
  }
  if (other < 0) QSKIP("No other user account available");

  backend.SwitchUser(other);
  QVERIFY(worker.switched_user);
  QCOMPARE(worker.switched_user->uid(),
           users->data(users->index(other, 0),
                       gxde::lock::UserListModel::UidRole)
               .toUInt());
}

void LockBackendTest::lockedOutStateFollowsUser() {
  SessionBaseModel model(SessionBaseModel::LockType);
  FakeLockWorker worker(&model);
  gxde::lock::LockBackend backend(&model, &worker);

  QVERIFY(!backend.LockedOut());
  QVERIFY(model.currentUser());
  model.currentUser()->startLock();
  QVERIFY(backend.LockedOut());
  QVERIFY(backend.LockMinutes() > 0);
}

void LockBackendTest::rewrittenWallpaperGetsNewUri() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("lock-wallpaper-override");
  auto write = [&](const QByteArray& data, qint64 mtime) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(data);
    file.setFileTime(QDateTime::fromMSecsSinceEpoch(mtime),
                     QFileDevice::FileModificationTime);
  };
  write("first", 1700000000000);
  const QString first = gxde::lock::LockBackend::VersionedImageUri(path);
  QCOMPARE(QUrl(first).toLocalFile(), path);
  QCOMPARE(gxde::lock::LockBackend::VersionedImageUri(path), first);  // stable

  // The override is rewritten in place with another picture.
  write("second picture", 1700000005000);
  const QString second = gxde::lock::LockBackend::VersionedImageUri(path);
  QVERIFY(second != first);
  QCOMPARE(QUrl(second).toLocalFile(), path);

  // Non-local and missing files are passed through.
  QCOMPARE(gxde::lock::LockBackend::VersionedImageUri(":/img/bg.jpg"),
           QString("qrc:/img/bg.jpg"));
  QCOMPARE(gxde::lock::LockBackend::VersionedImageUri(dir.filePath("missing")),
           QUrl::fromLocalFile(dir.filePath("missing")).toString());
}

void LockBackendTest::measuresBrightnessBehindTheClock() {
  using gxde::lock::LockBackend;
  QImage white(1920, 1080, QImage::Format_RGB32);
  white.fill(Qt::white);
  QVERIFY(LockBackend::MeasureLuminance(white) > 0.99);

  QImage black(1920, 1080, QImage::Format_RGB32);
  black.fill(Qt::black);
  QVERIFY(LockBackend::MeasureLuminance(black) < 0.01);

  // Dark edges do not hide a bright centre, where the clock sits...
  QImage centre = black;
  QPainter(&centre).fillRect(QRect(480, 300, 960, 480), Qt::white);
  QVERIFY(LockBackend::MeasureLuminance(centre) > 0.99);

  // ...and bright edges outside it do not count.
  QImage edges = white;
  QPainter(&edges).fillRect(QRect(384, 270, 1152, 540), Qt::black);
  QVERIFY(LockBackend::MeasureLuminance(edges) < 0.01);

  // Rec. 709: green is the brightest primary, blue the darkest.
  QImage green(160, 90, QImage::Format_RGB32);
  green.fill(QColor(0, 255, 0));
  QImage blue(160, 90, QImage::Format_RGB32);
  blue.fill(QColor(0, 0, 255));
  QVERIFY(LockBackend::MeasureLuminance(green) >
          LockBackend::MeasureLuminance(blue));

  QCOMPARE(LockBackend::MeasureLuminance(QImage()), 0.0);
}

QTEST_GUILESS_MAIN(LockBackendTest)
#include "lock_backend_test.moc"
