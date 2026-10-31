/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QLocale>
#include <QLockFile>
#include <QMargins>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>
#include <QVariantMap>
#include <QWindow>

#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>
#include <QtGui/qguiapplication_platform.h>
#include <wayland-client.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <unistd.h>

#include "GreeterCursor.h"
#include "GreeterScale.h"
#include "dbuslockagent.h"
#include "dbuslockfrontservice.h"
#include "freedesktopscreensaver.h"
#include "lockbackend.h"
#include "lockshortcutmanager.h"
#include "lockstyle.h"
#include "lockstylemanager.h"
#include "lockworker.h"
#include "sessionbasemodel.h"
#include "userinfo.h"
#include "waylandsessionlock.h"

// Keep Xlib last: its None/True macros collide with Qt headers.
#include <X11/Xlib.h>

namespace {

const QString kGxdeDisplayManagerService =
    QStringLiteral("top.gxde.DisplayManager");
const QString kGxdeDisplayManagerPath =
    QStringLiteral("/top/gxde/DisplayManager");
const QString kGxdeDisplayManagerInterface =
    QStringLiteral("top.gxde.DisplayManager");
const QString kDeepinLockService =
    QStringLiteral("com.deepin.dde.lockFront");
const QString kDeepinLockPath =
    QStringLiteral("/com/deepin/dde/lockFront");
const QString kDeepinLockInterface =
    QStringLiteral("com.deepin.dde.lockFront");

bool hasArgument(int argc, char* argv[], const char* value) {
  for (int i = 1; i < argc; ++i) {
    if (QByteArray(argv[i]) == value) return true;
  }
  return false;
}

bool runsOnX11(int argc, char* argv[]) {
  QByteArray platform = qgetenv("QT_QPA_PLATFORM");
  for (int i = 1; platform.isEmpty() && i + 1 < argc; ++i) {
    if (QByteArray(argv[i]) == "-platform") platform = argv[i + 1];
  }
  return platform.startsWith("xcb") ||
         (platform.isEmpty() && !qEnvironmentVariableIsSet("WAYLAND_DISPLAY"));
}

void roundtripWaylandDisplay() {
  auto* native =
      qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
  if (!native || !native->display()) return;
  wl_display_roundtrip(native->display());
}

void configureLayerShell(QQuickWindow* window) {
  if (!window) return;
  if (!window->handle()) window->create();

  LayerShellQt::Window* layerWindow = LayerShellQt::Window::get(window);
  if (!layerWindow) return;

  layerWindow->setScreenConfiguration(
      LayerShellQt::Window::ScreenFromQWindow);
  layerWindow->setScope(QStringLiteral("gxdm-lock-neo"));
  layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
  LayerShellQt::Window::Anchors anchors;
  anchors.setFlag(LayerShellQt::Window::AnchorTop);
  anchors.setFlag(LayerShellQt::Window::AnchorBottom);
  anchors.setFlag(LayerShellQt::Window::AnchorLeft);
  anchors.setFlag(LayerShellQt::Window::AnchorRight);
  layerWindow->setAnchors(anchors);
  layerWindow->setExclusiveZone(-1);
  layerWindow->setMargins(QMargins());
  layerWindow->setKeyboardInteractivity(
      LayerShellQt::Window::KeyboardInteractivityExclusive);
  layerWindow->setCloseOnDismissed(false);
}

bool qmlModuleAvailable(const QQmlEngine& engine, const QString& module) {
  const QString relative =
      QString(module).replace(QLatin1Char('.'), QLatin1Char('/'));
  for (const QString& path : engine.importPathList()) {
    if (QFileInfo::exists(QDir(path).filePath(relative +
                                              QStringLiteral("/qmldir")))) {
      return true;
    }
  }
  return false;
}

// Preview worker used by --test-mode. It never touches PAM and accepts any
// password, so the QML frontend can be exercised without locking the session.
class DemoLockWorker : public LockWorker {
 public:
  explicit DemoLockWorker(SessionBaseModel* model) : LockWorker(model) {}

  void authUser(const QString& password) override {
    if (authenticating_) return;

    Q_UNUSED(password)
    authenticating_ = true;
    QTimer::singleShot(450, this, [this] {
      authenticating_ = false;
      emit m_model->authFinished(true);
    });
  }

  void switchToUser(std::shared_ptr<User> user) override {
    Q_UNUSED(user)
  }

  bool IsAuthenticating() const override { return authenticating_; }

 private:
  bool authenticating_ = false;
};

void requestShowFromExistingLocker() {
  QDBusInterface gxdm(kGxdeDisplayManagerService, kGxdeDisplayManagerPath,
                      kGxdeDisplayManagerInterface,
                      QDBusConnection::sessionBus());
  if (gxdm.isValid()) {
    gxdm.asyncCall(QStringLiteral("Show"));
    return;
  }

  QDBusInterface deepin(kDeepinLockService, kDeepinLockPath,
                        kDeepinLockInterface, QDBusConnection::sessionBus());
  if (deepin.isValid()) deepin.asyncCall(QStringLiteral("Show"));
}

}  // namespace

int main(int argc, char* argv[]) {
  const bool testMode = hasArgument(argc, argv, "--test-mode");
  const bool useX11 = runsOnX11(argc, argv);

  if (!testMode && !useX11) {
    LayerShellQt::Shell::useLayerShell();
  }

  qputenv("QT_IM_MODULE", QByteArrayLiteral("qtvirtualkeyboard"));
  qputenv("QT_VIRTUALKEYBOARD_STYLE", QByteArrayLiteral("gxde"));
  // The keyboard is the InputPanel embedded in LockScreen.qml. Without this,
  // focusing the password field makes the plugin open its own top-level panel
  // window, which steals window activation from the lock window; the field
  // then loses focus, the panel closes, the field refocuses... and flickers.
  qputenv("QT_VIRTUALKEYBOARD_DESKTOP_DISABLE", QByteArrayLiteral("1"));
  
  if (!qEnvironmentVariableIsSet("QSG_USE_SIMPLE_ANIMATION_DRIVER")) {
    qputenv("QSG_USE_SIMPLE_ANIMATION_DRIVER", QByteArrayLiteral("1"));
  }

  GxdmGreeterCursor::applyConfiguredTheme();
  if (useX11) {
    GxdmGreeterScale::applyX11ScaleFactors();
  } else {
    GxdmGreeterCursor::enforceWaylandCursorConfig();
    GxdmGreeterScale::applyThemeScale();
  }

  QQuickWindow::setDefaultAlphaBuffer(true);
  QGuiApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  app.setOrganizationName(QStringLiteral("deepin"));
  app.setApplicationName(QStringLiteral("gxdm-lock-neo"));
  app.setApplicationVersion(QStringLiteral("1.0"));

  // The shared LockWorker reports PAM failures with tr() strings translated
  // by the classic session UI catalogue; load it here too.
  QTranslator translator;
  if (translator.load(
          QStringLiteral("/usr/share/gxde-session-ui/translations/"
                         "gxde-session-ui_") +
          QLocale::system().name())) {
    app.installTranslator(&translator);
  }

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption daemonOption(
      QStringList{QStringLiteral("d"), QStringLiteral("daemon")},
      QStringLiteral("start in daemon mode and wait for a lock request"));
  QCommandLineOption switchOption(
      QStringList{QStringLiteral("s"), QStringLiteral("switch")},
      QStringLiteral("show the user list after locking"));
  QCommandLineOption autoEnrollOption(
      QStringLiteral("auto-enroll"),
      QStringLiteral("enroll the lock shortcut (enabled by default)"));
  QCommandLineOption testOption(
      QStringLiteral("test-mode"),
      QStringLiteral("preview the lock screen without authenticating"));
  parser.addOption(daemonOption);
  parser.addOption(switchOption);
  parser.addOption(autoEnrollOption);
  parser.addOption(testOption);
  parser.process(app);

  const bool demo = parser.isSet(testOption);
  const bool runDaemon = parser.isSet(daemonOption) && !demo;
  const bool showUserList = parser.isSet(switchOption);

  // Keep one locker per session. A second invocation just asks the running
  // instance to show itself, matching the classic locker's single-instance
  // behaviour. The preview mode deliberately skips this.
  std::unique_ptr<QLockFile> instanceLock;
  if (!demo) {
    QString runtimeDir =
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtimeDir.isEmpty()) runtimeDir = QDir::tempPath();
    instanceLock = std::make_unique<QLockFile>(
        QDir(runtimeDir).filePath(QStringLiteral("gxdm-lock-neo.lock")));
    if (!instanceLock->tryLock(0)) {
      if (!runDaemon) requestShowFromExistingLocker();
      return 0;
    }
  }

  SessionBaseModel model(SessionBaseModel::LockType);
  std::unique_ptr<LockWorker> worker;
  if (demo) {
    worker = std::make_unique<DemoLockWorker>(&model);
  } else {
    worker = std::make_unique<LockWorker>(&model);
  }
  gxde::lock::LockBackend backend(&model, worker.get());

  QObject::connect(&model, &SessionBaseModel::showUserList, &backend,
                   [&backend] { backend.ShowUserList(); });

  // Session D-Bus surface. The QML locker is a drop-in replacement for the
  // classic locker: the same service names and methods are exported so
  // desktop shortcuts, power menus and org.freedesktop.ScreenSaver callers
  // keep working.
  std::unique_ptr<DBusLockAgent> agent;
  std::unique_ptr<DBusLockFrontService> frontService;
  std::unique_ptr<FreedesktopScreenSaver> screenSaver;
  std::unique_ptr<LockShortcutManager> shortcutManager;
  std::unique_ptr<GxdeDisplayManagerService> displayManagerService;

  if (!demo) {
    QDBusConnection sessionBus = QDBusConnection::sessionBus();

    agent = std::make_unique<DBusLockAgent>();
    agent->setModel(&model);
    frontService = std::make_unique<DBusLockFrontService>(agent.get());
    screenSaver =
        std::make_unique<FreedesktopScreenSaver>(agent.get(), &model);
    shortcutManager = std::make_unique<LockShortcutManager>(&model);
    displayManagerService =
        std::make_unique<GxdeDisplayManagerService>(shortcutManager.get());

    if (!sessionBus.registerService(kGxdeDisplayManagerService) ||
        !sessionBus.registerObject(kGxdeDisplayManagerPath,
                                   shortcutManager.get(),
                                   QDBusConnection::ExportAdaptors)) {
      qWarning() << "Failed to register" << kGxdeDisplayManagerService
                 << sessionBus.lastError();
      if (!runDaemon) requestShowFromExistingLocker();
      return 1;
    }

    if (sessionBus.registerService(kDeepinLockService)) {
      sessionBus.registerObject(kDeepinLockPath, agent.get(),
                                QDBusConnection::ExportAdaptors);
    } else {
      qInfo() << kDeepinLockService << "is already owned.";
    }

    if (sessionBus.registerService(QStringLiteral("org.freedesktop.ScreenSaver"))) {
      sessionBus.registerObject(QStringLiteral("/org/freedesktop/ScreenSaver"),
                                agent.get(), QDBusConnection::ExportAdaptors);
      sessionBus.registerObject(QStringLiteral("/ScreenSaver"), agent.get(),
                                QDBusConnection::ExportAdaptors);
    }

    QTimer::singleShot(0, shortcutManager.get(),
                       [manager = shortcutManager.get()] {
                         manager->tryEnroll(true);
                       });
  }

  // Runtime style switch: the daemon broadcasts LockScreenStyleChanged after
  // SetLockScreenStyle(). Once the screen is hidden, release the instance lock
  // and our D-Bus names, then exec the configured locker in daemon mode.
  std::unique_ptr<gxde::lock::LockStyleManager> styleManager;
  if (!demo) {
    gxde::lock::LockStylePaths stylePaths =
        gxde::lock::LockStyleResolver::DefaultPaths();
    stylePaths.gxdmConfig =
        qEnvironmentVariable("GXDM_LOCK_GXDM_CONF", stylePaths.gxdmConfig);
    stylePaths.omgConfig =
        qEnvironmentVariable("GXDM_LOCK_OMG_CONF", stylePaths.omgConfig);
    styleManager = std::make_unique<gxde::lock::LockStyleManager>(
        &model, gxde::lock::LockStyle::kModern, stylePaths);
    QObject::connect(displayManagerService.get(),
                     &GxdeDisplayManagerService::LockScreenStyleChanged,
                     styleManager.get(),
                     &gxde::lock::LockStyleManager::Refresh);
    QObject::connect(
        styleManager.get(), &gxde::lock::LockStyleManager::HandoffRequested,
        &app,
        [&instanceLock](const QString& executable) {
          qInfo() << "Lock screen style changed; handing off to" << executable;
          QDBusConnection sessionBus = QDBusConnection::sessionBus();
          const QStringList names{
              kGxdeDisplayManagerService,
              kDeepinLockService,
              QStringLiteral("org.freedesktop.ScreenSaver"),
          };
          for (const QString& name : names) sessionBus.unregisterService(name);
          if (instanceLock) instanceLock->unlock();

          gxde::lock::ExecLockScreen(executable,
                                     {QByteArrayLiteral("--daemon")});

          // exec failed: take everything back and keep running.
          if (instanceLock) instanceLock->tryLock(0);
          for (const QString& name : names) sessionBus.registerService(name);
        },
        Qt::QueuedConnection);
  }

  // Wayland's ext-session-lock-v1 is preferred. If the compositor does not
  // advertise it, configureLayerShell() provides the same full-screen,
  // keyboard-exclusive fallback the classic locker uses.
  std::unique_ptr<WaylandSessionLockIntegration> sessionLockIntegration;
  if (!useX11 && !demo) {
    sessionLockIntegration =
        std::make_unique<WaylandSessionLockIntegration>(&app);
    sessionLockIntegration->initialize(nullptr);
    roundtripWaylandDisplay();

    if (sessionLockIntegration->isAvailable()) {
      qInfo() << "(Lock) Protocol: Using ext-session-lock-v1.";
      QObject::connect(sessionLockIntegration.get(),
                       &WaylandSessionLockIntegration::locked, &app, [] {
                         qInfo() << "(Lock) Status: Wayland session is locked.";
                       });
      QObject::connect(sessionLockIntegration.get(),
                       &WaylandSessionLockIntegration::finished, &model,
                       [&model] {
                         qWarning() << "(Lock) Status: Wayland session lock was"
                                       " finished by the compositor.";
                         model.setIsShow(false);
                       });
    } else {
      qWarning() << "(Lock) Warning: ext-session-lock-v1 is unavailable;"
                    " using the LayerShell fallback.";
    }
  }

  QQmlApplicationEngine engine;
  QQmlComponent component(&engine);
  component.loadFromModule(QStringLiteral("Lock"),
                           QStringLiteral("LockWindow"));
  if (component.isError()) {
    qWarning() << "Failed to load the QML lock screen:"
               << component.errors();
    return 1;
  }

  const bool virtualKeyboardAvailable =
      backend.VirtualKeyboardAvailable() &&
      qmlModuleAvailable(engine, QStringLiteral("QtQuick.VirtualKeyboard"));

  QHash<QScreen*, QQuickWindow*> windows;
  QQuickWindow* primaryWindow = nullptr;
  std::function<void(QQuickWindow*)> grabKeyboard;

  constexpr int kUnlockFadeTimeoutMs = 1000;
  bool unlockPending = false;
  qsizetype pendingUnlockFades = 0;
  QTimer unlockFadeTimer;
  unlockFadeTimer.setSingleShot(true);
  unlockFadeTimer.setInterval(kUnlockFadeTimeoutMs);

  auto destroyWindow = [&](QScreen* screen) {
    QQuickWindow* window = windows.take(screen);
    if (!window) return;
    if (window == primaryWindow) primaryWindow = nullptr;
    delete window;
  };

  auto destroyAll = [&] {
    const QList<QQuickWindow*> oldWindows = windows.values();
    windows.clear();
    primaryWindow = nullptr;
    for (QQuickWindow* window : oldWindows) delete window;
  };

  auto createWindow = [&](QScreen* screen) -> QQuickWindow* {
    if (!screen) return nullptr;
    if (QQuickWindow* existing = windows.value(screen)) return existing;

    QVariantMap properties;
    properties.insert(QStringLiteral("backend"),
                      QVariant::fromValue(&backend));
    properties.insert(QStringLiteral("primaryScreen"),
                      screen == QGuiApplication::primaryScreen());
    properties.insert(QStringLiteral("virtualKeyboardAvailable"),
                      virtualKeyboardAvailable);
    properties.insert(QStringLiteral("testMode"), demo);

    QObject* object = component.createWithInitialProperties(properties);
    auto* window = qobject_cast<QQuickWindow*>(object);
    if (!window) {
      qWarning() << "Failed to create a QML lock window:" << component.errors();
      delete object;
      return nullptr;
    }

    window->setScreen(screen);

    if (!demo && !useX11) {
      const bool usingSessionLock =
          sessionLockIntegration &&
          sessionLockIntegration->isAvailable() &&
          sessionLockIntegration->installOnWindow(window);
      if (!usingSessionLock) configureLayerShell(window);
    }

    if (!demo && useX11) {
      QObject::connect(
          window, &QWindow::visibleChanged, &app,
          [&grabKeyboard, &primaryWindow, window](bool visible) {
            if (visible && window == primaryWindow) grabKeyboard(window);
          });
    }

    QObject::connect(
        &backend, &gxde::lock::LockBackend::Authenticated, window,
        [window] { QMetaObject::invokeMethod(window, "handleAuthenticated"); });
    QObject::connect(
        &backend, &gxde::lock::LockBackend::AuthenticationFailed, window,
        [window](const QString& message) {
          QMetaObject::invokeMethod(
              window, "handleAuthenticationFailed",
              Q_ARG(QVariant, QVariant::fromValue(message)));
        });
    QObject::connect(
        &backend, &gxde::lock::LockBackend::PowerActionStarted, window,
        [window](const QString& action) {
          QMetaObject::invokeMethod(
              window, "handlePowerActionStarted",
              Q_ARG(QVariant, QVariant::fromValue(action)));
        });
    QObject::connect(
        &backend, &gxde::lock::LockBackend::ShowUserListRequested, window,
        [window] { QMetaObject::invokeMethod(window, "handleShowUserList"); });

    windows.insert(screen, window);
    if (screen == QGuiApplication::primaryScreen()) primaryWindow = window;

    window->setProperty("shown", model.isShow());
    return window;
  };

  grabKeyboard = [&](QQuickWindow* window) {
    if (!window || demo || !useX11) return;

    auto* native =
        qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!native || !native->display()) return;

    const int result = XGrabKeyboard(
        native->display(), static_cast<Window>(window->winId()), True,
        GrabModeAsync, GrabModeAsync, CurrentTime);
    if (result == 0) return;

    static int failures = 0;
    if (++failures >= 15) {
      qWarning() << "Failed to grab the keyboard for the lock screen.";
      failures = 0;
      return;
    }
    QTimer::singleShot(100, window, [&grabKeyboard, window] {
      grabKeyboard(window);
    });
  };

  auto releaseKeyboard = [&] {
    if (demo || !useX11) return;
    auto* native =
        qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (native && native->display()) {
      XUngrabKeyboard(native->display(), CurrentTime);
    }
  };

  auto createAll = [&] {
    if (demo) {
      createWindow(QGuiApplication::primaryScreen());
      return;
    }
    QList<QScreen*> screens = QGuiApplication::screens();
    std::stable_sort(screens.begin(), screens.end(),
                     [](QScreen* left, QScreen* right) {
                       const bool leftPrimary =
                           left == QGuiApplication::primaryScreen();
                       const bool rightPrimary =
                           right == QGuiApplication::primaryScreen();
                       return leftPrimary && !rightPrimary;
                     });
    for (QScreen* screen : screens) createWindow(screen);
  };

  QObject::connect(&app, &QGuiApplication::screenAdded, &app,
                   [&createWindow, demo](QScreen* screen) {
                     if (!demo) createWindow(screen);
                   });
  QObject::connect(&app, &QGuiApplication::screenRemoved, &app,
                   [&destroyWindow](QScreen* screen) { destroyWindow(screen); });
  QObject::connect(&app, &QGuiApplication::primaryScreenChanged, &app,
                   [&](QScreen* screen) {
                     primaryWindow = windows.value(screen);
                     if (model.isShow() && primaryWindow && !demo && useX11) {
                       releaseKeyboard();
                       grabKeyboard(primaryWindow);
                     }
                   });

  QObject::connect(&model, &SessionBaseModel::visibleChanged, &app,
                   [&](bool visible) {
                     for (QQuickWindow* window : windows) {
                       window->setProperty("shown", visible);
                     }
                     if (!visible) releaseKeyboard();
                     if (!demo && visible && primaryWindow) {
                       QTimer::singleShot(0, primaryWindow, [primaryWindow] {
                         primaryWindow->requestActivate();
                       });
                     }
                   });

  auto finishUnlock = [&] {
    if (!unlockPending) return;
    unlockPending = false;
    pendingUnlockFades = 0;
    unlockFadeTimer.stop();
    if (sessionLockIntegration && sessionLockIntegration->isAvailable()) {
      sessionLockIntegration->unlock();
    } else {
      model.setIsShow(false);
    }
  };

  QObject::connect(&unlockFadeTimer, &QTimer::timeout, &app, [&] {
    qWarning() << "(Lock) Unlock fade-out timed out; unlocking now.";
    finishUnlock();
  });

  QObject::connect(&backend, &gxde::lock::LockBackend::UnlockFadeFinished,
                   &app, [&] {
                     if (unlockPending && --pendingUnlockFades <= 0) {
                       finishUnlock();
                     }
                   });

  QObject::connect(&backend, &gxde::lock::LockBackend::Authenticated, &app,
                   [&] {
                     if (demo || unlockPending) return;
                     unlockPending = true;
                     pendingUnlockFades = windows.size();
                     if (pendingUnlockFades == 0) {
                       finishUnlock();
                       return;
                     }
                     unlockFadeTimer.start();
                   });

  if (sessionLockIntegration) {
    // ext-session-lock roles are tied to a wl_surface for life. Recreate the
    // hidden windows after every completed/aborted lock cycle so the next
    // request receives fresh surfaces instead of reusing dead roles.
    QObject::connect(sessionLockIntegration.get(),
                     &WaylandSessionLockIntegration::unlocked, &app,
                     [&] {
                       model.setIsShow(false);
                       destroyAll();
                       createAll();
                     },
                     Qt::QueuedConnection);
    QObject::connect(sessionLockIntegration.get(),
                     &WaylandSessionLockIntegration::finished, &app,
                     [&] {
                       model.setIsShow(false);
                       destroyAll();
                       createAll();
                     },
                     Qt::QueuedConnection);
  }

  createAll();

  if (!runDaemon) {
    model.setIsShow(true);
    if (showUserList) emit model.showUserList();
  }

  const int result = app.exec();

  destroyAll();
  return result;
}
