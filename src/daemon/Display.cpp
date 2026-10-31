/***************************************************************************
* Copyright (c) 2014-2015 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
* Copyright (c) 2014 Martin Bříza <mbriza@redhat.com>
* Copyright (c) 2013 Abdurrahman AVCI <abdurrahmanavci@gmail.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the
* Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
***************************************************************************/

#include "Display.h"

#include "Configuration.h"
#include "DaemonApp.h"
#include "DisplayManager.h"
#include "XorgDisplayServer.h"
#include "XorgUserDisplayServer.h"
#include "Seat.h"
#include "SocketServer.h"
#include "Greeter.h"
#include "SessionReuse.h"
#include "Utils.h"

#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>
#include <QLocalSocket>

#include <pwd.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

#include <sys/ioctl.h>
#include <fcntl.h>

#include <memory>
#include <utility>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include "FingerprintProbe.h"
#include "Login1Manager.h"
#include "VirtualTerminal.h"
#include "WaylandDisplayServer.h"
#include "config.h"

static int s_ttyFailures = 0;

namespace SDDM {
    static void killLogindSessionIfPresent(const QString &sessionId) {
        OrgFreedesktopLogin1ManagerInterface manager(
            Logind::serviceName(), Logind::managerPath(),
            QDBusConnection::systemBus());
        auto *existsWatcher = new QDBusPendingCallWatcher(
            manager.GetSession(sessionId), QCoreApplication::instance());
        QObject::connect(existsWatcher,
            &QDBusPendingCallWatcher::finished, existsWatcher,
            [sessionId](QDBusPendingCallWatcher *watcher) {
                const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
                watcher->deleteLater();
                if (reply.isError())
                    return;

                qWarning() << "Logind session" << sessionId
                           << "still exists; killing remaining processes";
                OrgFreedesktopLogin1ManagerInterface manager(
                    Logind::serviceName(), Logind::managerPath(),
                    QDBusConnection::systemBus());
                auto *killWatcher = new QDBusPendingCallWatcher(
                    manager.KillSession(
                        sessionId, QStringLiteral("all"), SIGKILL),
                    QCoreApplication::instance());
                QObject::connect(killWatcher,
                    &QDBusPendingCallWatcher::finished, killWatcher,
                    [sessionId](QDBusPendingCallWatcher *watcher) {
                        const QDBusPendingReply<> reply = *watcher;
                        watcher->deleteLater();
                        if (reply.isError()) {
                            qWarning()
                                << "Failed to kill remaining processes in logind session"
                                << sessionId << reply.error().message();
                        }
                    });
            });
    }

    static void terminateLogindSession(const QString &sessionId) {
        if (sessionId.isEmpty() || !Logind::isAvailable())
            return;

        OrgFreedesktopLogin1ManagerInterface manager(
            Logind::serviceName(), Logind::managerPath(),
            QDBusConnection::systemBus());
        qInfo() << "Terminating logind session" << sessionId;
        // Session cleanup must not hold the daemon event loop. The helper
        // already terminates its child process; this request lets logind clean
        // up the corresponding scope without delaying the next greeter.
        auto *terminateWatcher = new QDBusPendingCallWatcher(
            manager.TerminateSession(sessionId),
            QCoreApplication::instance());
        QObject::connect(terminateWatcher,
            &QDBusPendingCallWatcher::finished, terminateWatcher,
            [sessionId](QDBusPendingCallWatcher *watcher) {
                const QDBusPendingReply<> reply = *watcher;
                watcher->deleteLater();
                if (reply.isError()) {
                    qWarning() << "Failed to terminate logind session"
                               << sessionId << reply.error().message();
                    return;
                }

                QTimer::singleShot(2000, QCoreApplication::instance(),
                    [sessionId] {
                        killLogindSessionIfPresent(sessionId);
                    });
            });
    }

    static QStringList sessionDirs(Session::Type type) {
        switch (type) {
        case Session::WaylandSession:
            return mainConfig.Wayland.SessionDir.get();
        case Session::X11Session:
            return mainConfig.X11.SessionDir.get();
        default:
            return {};
        }
    }

    static bool sessionEntryExists(Session::Type type, const QString& name) {
        if (name.isEmpty()) {
            return false;
        }

        QString fileName = name;
        const QString extension = QStringLiteral(".desktop");
        if (!fileName.endsWith(extension))
            fileName += extension;

        const QFileInfo fileInfo(fileName);
        for (const auto &path : sessionDirs(type)) {
            const QDir dir(path);

            if (fileInfo.isAbsolute()) {
                if (fileInfo.absolutePath() != dir.absolutePath())
                    continue;

                return fileInfo.exists() && fileInfo.isFile();
            }

            if (dir.exists(fileName))
                return true;
        }

        return false;
    }

    static bool loadSession(Session::Type type, const QString& name, Session &session) {
        if (!sessionEntryExists(type, name)) {
            return false;
        }

        Session candidate(type, name);
        if (!candidate.isValid()) {
            return false;
        }

        session = candidate;
        return true;
    }

    static bool loadNamedSession(const QString& name, Session& session) {
        if (loadSession(Session::WaylandSession, name, session)) {
            return true;
        }

        return loadSession(Session::X11Session, name, session);
    }

    static bool loadFirstAvailableSession(Session::Type type, const QStringList &dirPaths, Session &session) {
        QStringList entries;
        for (const auto& path : dirPaths) {
            QDir dir(path);
            dir.setNameFilters({QStringLiteral("*.desktop")});
            dir.setFilter(QDir::Files);
            entries += dir.entryList();
        }

        entries.removeDuplicates();
        entries.sort(Qt::CaseInsensitive);

        for (const auto &entry : std::as_const(entries)) {
            Session candidate(type, entry);
            if (!candidate.isAvailable())
                continue;

            session = candidate;
            return true;
        }

        return false;
    }

    static bool loadDefaultSession(Session& session) {
        if (QFileInfo::exists(QStringLiteral("/dev/dri")) &&
            loadFirstAvailableSession(Session::WaylandSession, mainConfig.Wayland.SessionDir.get(), session)) {
            return true;
        }

        return loadFirstAvailableSession(Session::X11Session, mainConfig.X11.SessionDir.get(), session);
    }

    Display::DisplayServerType Display::defaultDisplayServerType()
    {
        const QString &displayServerType = mainConfig.DisplayServer.get().toLower();
        DisplayServerType ret;
        if (displayServerType == QStringLiteral("x11-user")) {
            ret = X11UserDisplayServerType;
        } else if (displayServerType == QStringLiteral("wayland")) {
            ret = WaylandDisplayServerType;
        } else {
            if (displayServerType != QLatin1String("x11")) {
                qWarning("\"%s\" is an invalid value for General.DisplayServer: fall back to \"x11\"",
                    qPrintable(displayServerType));
            }
            ret = X11DisplayServerType;
        }
        return ret;
    }

    Display::Display(Seat *parent, DisplayServerType serverType)
        : QObject(parent),
        m_displayServerType(serverType),
        m_auth(new Auth(this)),
        m_seat(parent),
        m_socketServer(new SocketServer(this)),
        m_greeter(new Greeter(this)),
        m_reuseActivationTimer(new QTimer(this))
    {
        m_reuseActivationTimer->setSingleShot(true);
        m_reuseActivationTimer->setInterval(30000);
        connect(m_reuseActivationTimer, &QTimer::timeout, this, [this] {
            if (m_reuseActivationPending) {
                finishReusableSessionActivation(false,
                    QStringLiteral("Timed out while activating the existing session"));
            }
        });

        // Create display server
        switch (m_displayServerType) {
        case X11DisplayServerType:
            m_terminalId = VirtualTerminal::setUpNewVt();
            m_displayServer = new XorgDisplayServer(this);
            break;
        case X11UserDisplayServerType:
            m_terminalId = VirtualTerminal::setUpNewVt();
            m_displayServer = new XorgUserDisplayServer(this);
            m_greeter->setDisplayServerCommand(XorgUserDisplayServer::command(this));
            break;
        case WaylandDisplayServerType:
            m_terminalId = VirtualTerminal::setUpNewVt();
            m_displayServer = new WaylandDisplayServer(this);
            {
                QString compositorCommand = mainConfig.Wayland.CompositorCommand.get();
                if (compositorCommand.trimmed().isEmpty()) {
                    // No hard dependency on a single compositor: prefer
                    // flakewm and fall back to other common ones.
                    struct CompositorCandidate {
                        const char *binary;
                        const char *args;
                    };
                    const CompositorCandidate candidates[] = {
                        { "flakewm", "" },
                        { "gxde-wlcom", "" },
                        { "labwc", "" },
                        { "sway", "" },
                        { "weston", " --shell=kiosk-shell.so" },
                    };
                    for (const auto &candidate : candidates) {
                        const QString executable =
                            QStandardPaths::findExecutable(QString::fromLatin1(candidate.binary));
                        if (!executable.isEmpty()) {
                            compositorCommand = executable + QLatin1String(candidate.args);
                            break;
                        }
                    }
                    qInfo() << "Automatically selected Wayland greeter compositor:" << compositorCommand;
                }
                m_greeter->setDisplayServerCommand(compositorCommand);
            }
            break;
        }

        qDebug("Using VT %d", m_terminalId);

        // respond to authentication requests
        m_auth->setVerbose(true);
        connect(m_auth, &Auth::requestChanged, this, &Display::slotRequestChanged);
        connect(m_auth, &Auth::authentication, this, &Display::slotAuthenticationFinished);
        connect(m_auth, &Auth::sessionStarted, this, &Display::slotSessionStarted);
        connect(m_auth, &Auth::finished, this, &Display::slotHelperFinished);
        connect(m_auth, &Auth::info, this, &Display::slotAuthInfo);
        connect(m_auth, &Auth::error, this, &Display::slotAuthError);

        // restart display after display server ended
        connect(m_displayServer, &DisplayServer::started, this, &Display::displayServerStarted);
        connect(m_displayServer, &DisplayServer::stopped, this, &Display::stop);

        // connect login signal
        connect(m_socketServer, &SocketServer::login, this, &Display::login);
        connect(m_socketServer, &SocketServer::fingerprintProbe, this, &Display::fingerprintProbe);
        connect(m_socketServer, &SocketServer::fingerprintLogin, this, &Display::fingerprintLogin);
        connect(m_socketServer, &SocketServer::fingerprintCancel, this, &Display::fingerprintCancel);

        m_transitionTimer = new QTimer(this);
        m_transitionTimer->setSingleShot(true);
        m_transitionTimer->setInterval(800);

        connect(m_transitionTimer, &QTimer::timeout, this, [this] {
            qWarning() << "Greeter transition timed out; continuing login";
            completeTransition();
        });
        connect(m_socketServer, &SocketServer::transitionReady, this, [this](QLocalSocket *socket) {
            if (socket == m_socket && m_transitionTimer->isActive()) {
                completeTransition();
            }
        });

        // connect login result signals
        connect(this, &Display::loginFailed, m_socketServer, &SocketServer::loginFailed);
        connect(this, &Display::loginSucceeded, m_socketServer, &SocketServer::loginSucceeded);

        connect(m_greeter, &Greeter::failed, this, &Display::stop);
        connect(m_greeter, &Greeter::ttyFailed, this, [this] {
            ++s_ttyFailures;
            if (s_ttyFailures > 5) {
                QCoreApplication::exit(23);
            }
            // It might be the case that we are trying a tty that has been taken over by a
            // different process. In such a case, switch back to the initial one and try again.
            VirtualTerminal::jumpToVt(SDDM_INITIAL_VT, true);
            stop();
        });
        connect(m_greeter, &Greeter::displayServerFailed, this, &Display::displayServerFailed);
    }

    Display::~Display() {
        disconnect(m_auth, &Auth::finished, this, &Display::slotHelperFinished);
        stop();
    }

    Display::DisplayServerType Display::displayServerType() const
    {
        return m_displayServerType;
    }

    DisplayServer *Display::displayServer() const
    {
        return m_displayServer;
    }

    int Display::terminalId() const {
        return m_auth->isActive() ? m_sessionTerminalId : m_terminalId;
    }

    const QString &Display::name() const {
        return m_displayServer->display();
    }

    QString Display::sessionType() const {
        return m_displayServer->sessionType();
    }

    Seat *Display::seat() const {
        return m_seat;
    }

    bool Display::start() {
        if (m_started)
            return true;

        const bool autologinRequested =
            (daemonApp->first || mainConfig.Autologin.Relogin.get()) &&
            !mainConfig.Autologin.User.get().trimmed().isEmpty();

        // An unprivileged X11 or Wayland greeter has its own display server.
        // Start autologin before that display server so the user compositor can
        // use this display's VT and DRM devices directly.  Rootful X11 is the
        // exception: an X11 user session runs on the already-started X server.
        if (autologinRequested &&
            m_displayServerType != X11DisplayServerType) {
            daemonApp->first = false;
            m_started = true;

            if (attemptAutologin())
                return true;

            qWarning() << "Autologin could not be started; falling back to the greeter";
            m_auth->setAutologin(false);
            m_started = false;
        }

        return m_displayServer->start();
    }

    bool Display::attemptAutologin() {
        const QString autologinUser = mainConfig.Autologin.User.get().trimmed();
        if (autologinUser.isEmpty()) {
            qCritical() << "Autologin requested without a configured user";
            return false;
        }

        Session session;
        const QString configuredSession = mainConfig.Autologin.Session.get().trimmed();
        if (!configuredSession.isEmpty()) {
            if (!loadNamedSession(configuredSession, session)) {
                return false;
            }
        } else {
            const QString lastSession = stateConfig.Last.Session.get().trimmed();
            if (!lastSession.isEmpty() && loadNamedSession(lastSession, session)) {
                qDebug() << "Using last session for autologin:" << lastSession;
            } else if (!loadDefaultSession(session)) {
                return false;
            }
        }

        m_auth->setAutologin(true);
        return startAuth(autologinUser, QString(), session);
    }

    void Display::startSocketServerAndGreeter() {
        // start socket server
        m_socketServer->start(m_displayServer->display());

        if (!daemonApp->testing()) {
            // change the owner and group of the socket to avoid permission denied errors
            struct passwd *pw = getpwnam("gxdm");
            if (pw) {
                if (chown(qPrintable(m_socketServer->socketAddress()), pw->pw_uid, pw->pw_gid) == -1) {
                    qWarning() << "Failed to change owner of the socket";
                    return;
                }
            }
        }

        m_greeter->setSocket(m_socketServer->socketAddress());
        m_greeter->setTheme(findGreeterTheme());

        // start greeter
        m_greeter->start();

        // reset first flag
        daemonApp->first = false;

        // set flags
        m_started = true;
    }

    void Display::handleAutologinFailure() {
        qWarning() << "Autologin failed!";
        m_auth->setAutologin(false);

        // Non-rootful display servers are deliberately not started before an
        // autologin attempt.  Bring one up now so its started signal can launch
        // the greeter.  Rootful X11 autologin runs after its server is ready.
        if (m_displayServerType != X11DisplayServerType) {
            m_started = false;
            if (!m_displayServer->start()) {
                qCritical() << "Failed to start display server after autologin failure";
                emit displayServerFailed();
            }
            return;
        }

        startSocketServerAndGreeter();
    }

    void Display::displayServerStarted() {
        // check flag
        if (m_started)
            return;

        // setup display
        m_displayServer->setupDisplay();

        // log message
        qDebug() << "Display server started.";

        // Rootful X11 must be running before its X11 user session can start.
        // Unprivileged X11 and Wayland autologin is handled early in start().
        if (m_displayServerType == X11DisplayServerType &&
            (daemonApp->first || mainConfig.Autologin.Relogin.get()) &&
            !mainConfig.Autologin.User.get().trimmed().isEmpty()) {
            // reset first flag
            daemonApp->first = false;

            // set flags
            m_started = true;

            const bool autologinStarted = attemptAutologin();
            if (!autologinStarted)
                handleAutologinFailure();

            return;
        }

        startSocketServerAndGreeter();
    }

    void Display::stop() {
        // check flag
        if (!m_started || m_stopping)
            return;

        m_stopping = true;

        m_reuseLookupPending = false;
        ++m_reuseLookupGeneration;
        m_reuseActivationPending = false;
        m_reuseHelperFinished = false;
        ++m_reuseActivationGeneration;
        m_reuseActivationTimer->stop();
        finishLogin(false);

        terminateLogindSession(m_logindSessionId);
        m_logindSessionId.clear();
        removeDisplayManagerSession();

        // stop the greeter
        m_greeter->stop();

        m_auth->stop();

        VirtualTerminal::resetVt(m_sessionTerminalId);

        // stop socket server
        m_socketServer->stop();

        // stop display server
        m_displayServer->blockSignals(true);
        m_displayServer->stop();
        m_displayServer->blockSignals(false);

        // reset flag
        m_started = false;
        m_stopping = false;

        // emit signal
        emit stopped();
    }

    void Display::login(QLocalSocket *socket,
                        const QString &user, const QString &password,
                        const Session &session) {
        // A password typed while the reader listens takes over.
        if (m_fingerprintLogin && !m_authenticated)
            cancelFingerprintLogin();

        if (m_auth->isActive() || m_reuseLookupPending || m_socket) {
            qWarning() << "Rejecting concurrent login request for user" << user;
            emit loginFailed(socket);
            return;
        }

        m_socket = socket;
        m_auth->setFingerprint(false);

        //the gxdm user has special privileges that skip password checking so that we can load the greeter
        //block ever trying to log in as the gxdm user
        if (user == QLatin1String("gxdm")) {
            finishLogin(false);
            return;
        }

        // authenticate
        if (!startAuth(user, password, session)) {
            qWarning() << "Unable to start authentication for user" << user;
            emit loginFailed(m_socket);
            m_socket = nullptr;
        }
    }

    void Display::fingerprintProbe(QLocalSocket *socket, const QString &user) {
        QPointer<QLocalSocket> target(socket);
        gxde::ProbeEnrolledFingers(user, this, [this, target, user](bool available) {
            if (target)
                m_socketServer->fingerprintAvailable(target, user, available);
        });
    }

    void Display::fingerprintLogin(QLocalSocket *socket, const QString &user,
                                   const Session &session) {
        if (m_auth->isActive() || m_reuseLookupPending || m_socket
                || user.isEmpty() || user == QLatin1String("gxdm")) {
            qWarning() << "Refusing fingerprint login for user" << user;
            m_socketServer->fingerprintFinished(socket);
            return;
        }

        m_socket = socket;
        m_fingerprintLogin = true;
        m_auth->setFingerprint(true);
        if (!startAuth(user, QString(), session)) {
            qWarning() << "Unable to start fingerprint login for user" << user;
            m_fingerprintLogin = false;
            m_auth->setFingerprint(false);
            m_socket = nullptr;
            m_socketServer->fingerprintFinished(socket);
        }
    }

    void Display::fingerprintCancel(QLocalSocket *socket) {
        // Too late once PAM accepted the finger: the session is starting.
        if (m_fingerprintLogin && socket == m_socket && !m_authenticated)
            cancelFingerprintLogin();
    }

    void Display::cancelFingerprintLogin() {
        qDebug() << "Cancelling fingerprint login for user" << m_auth->user();
        // A pending session reuse lookup would start the helper later.
        m_reuseLookupPending = false;
        ++m_reuseLookupGeneration;
        // stop() waits for the helper, whose exit reaches slotHelperFinished()
        // while this flag is set.
        m_cancellingFingerprint = true;
        m_auth->stop();
        m_cancellingFingerprint = false;
        m_fingerprintLogin = false;
        m_auth->setFingerprint(false);
        m_socket = nullptr;
    }

    QString Display::findGreeterTheme() const {
        QString themeName = mainConfig.Theme.Current.get();

        // an unconfigured theme means the user wants to load the
        // default theme from the resources
        if (themeName.isEmpty())
            return QString();

        QDir dir(mainConfig.Theme.ThemeDir.get());

        // return the default theme if it exists
        if (dir.exists(themeName))
            return dir.absoluteFilePath(themeName);

        // otherwise use the embedded theme
        qWarning() << "The configured theme" << themeName << "doesn't exist, using the embedded theme instead";
        return QString();
    }

    bool Display::startAuth(const QString &user, const QString &password, const Session &session) {

        if (m_auth->isActive()) {
            qWarning() << "Existing authentication ongoing, aborting";
            return false;
        }

        m_passPhrase = password;
        m_authenticated = false;

        // sanity check
        if (!session.isValid()) {
            qCritical() << "Invalid session" << session.fileName();
            return false;
        }
        if (session.xdgSessionType().isEmpty()) {
            qCritical() << "Failed to find XDG session type for session" << session.fileName();
            return false;
        }
        if (session.exec().isEmpty()) {
            qCritical() << "Failed to find command for session" << session.fileName();
            return false;
        }

        m_reuseSessionId = QString();
        m_reuseSessionVt = 0;
        m_logindSessionId.clear();
        m_reuseLookupPending = false;
        ++m_reuseLookupGeneration;
        m_reuseActivationPending = false;
        m_reuseHelperFinished = false;
        ++m_reuseActivationGeneration;
        m_reuseActivationTimer->stop();

        // save session desktop file name, we'll use it to set the
        // last session later, in slotAuthenticationFinished()
        m_sessionName = session.fileName();

        m_sharedX11 = session.type() == Session::X11Session
            && m_displayServerType == X11DisplayServerType;
        m_sessionTerminalId = m_terminalId;
        if ((session.type() == Session::WaylandSession && m_displayServerType == X11DisplayServerType) || (m_greeter->isRunning() && m_displayServerType != X11DisplayServerType)) {
            // Create a new VT when we need to have another compositor running
            m_sessionTerminalId = VirtualTerminal::setUpNewVt();
        }

        // some information
        qDebug() << "Session" << m_sessionName << "selected, command:" << session.exec() << "for VT" << m_sessionTerminalId;

        QProcessEnvironment env;
        env.insert(session.additionalEnv());

        env.insert(QStringLiteral("PATH"), mainConfig.Users.DefaultPath.get());
        m_displayManagerSessionName = QStringLiteral("Session%1").arg(daemonApp->newSessionId());
        env.insert(QStringLiteral("XDG_SEAT_PATH"), daemonApp->displayManager()->seatPath(seat()->name()));
        env.insert(QStringLiteral("XDG_SESSION_PATH"), daemonApp->displayManager()->sessionPath(m_displayManagerSessionName));
        env.insert(QStringLiteral("DESKTOP_SESSION"), session.desktopSession());
        if (!session.desktopNames().isEmpty())
            env.insert(QStringLiteral("XDG_CURRENT_DESKTOP"), session.desktopNames());
        env.insert(QStringLiteral("XDG_SESSION_CLASS"), QStringLiteral("user"));
        env.insert(QStringLiteral("XDG_SESSION_TYPE"), session.xdgSessionType());
        env.insert(QStringLiteral("XDG_SEAT"), seat()->name());
        env.insert(QStringLiteral("XDG_VTNR"), QString::number(m_sessionTerminalId));
#ifdef HAVE_SYSTEMD
        env.insert(QStringLiteral("XDG_SESSION_DESKTOP"), session.desktopNames());
#endif

        if (session.xdgSessionType() == QLatin1String("x11")) {
          if (m_displayServerType == X11DisplayServerType)
            env.insert(QStringLiteral("DISPLAY"), name());
          else
            m_auth->setDisplayServerCommand(XorgUserDisplayServer::command(this));
        } else {
            m_auth->setDisplayServerCommand(QStringLiteral());
        }
        m_auth->setUser(user);
        m_auth->setSession(session.exec());
        m_auth->insertEnvironment(env);

        if (Logind::isAvailable() && mainConfig.Users.ReuseSession.get())
            findReusableSession(user, session.xdgSessionType());
        else
            m_auth->start();

        return true;
    }

    void Display::findReusableSession(const QString &user,
                                      const QString &requestedType) {
        const quint64 generation = ++m_reuseLookupGeneration;
        m_reuseLookupPending = true;

        // Reusing a session is optional. Never let an unresponsive logind
        // prevent a fresh login from starting.
        QTimer::singleShot(3000, this, [this, generation] {
            if (m_reuseLookupPending
                    && m_reuseLookupGeneration == generation) {
                qWarning() << "Timed out while looking for a reusable session";
                finishReusableSessionLookup(generation);
            }
        });

        OrgFreedesktopLogin1ManagerInterface manager(
            Logind::serviceName(), Logind::managerPath(),
            QDBusConnection::systemBus());
        auto *listWatcher = new QDBusPendingCallWatcher(
            manager.ListSessions(), this);
        connect(listWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, user, requestedType, generation]
            (QDBusPendingCallWatcher *watcher) {
                const QDBusPendingReply<SessionInfoList> reply = *watcher;
                watcher->deleteLater();

                if (!m_reuseLookupPending
                        || m_reuseLookupGeneration != generation) {
                    return;
                }
                if (reply.isError()) {
                    qWarning() << "Could not list sessions for reuse"
                               << reply.error().message();
                    finishReusableSessionLookup(generation);
                    return;
                }

                SessionInfoList candidates;
                for (const SessionInfo &session : reply.value()) {
                    if (session.userName == user)
                        candidates.append(session);
                }
                if (candidates.isEmpty()) {
                    finishReusableSessionLookup(generation);
                    return;
                }

                const auto remaining =
                    std::make_shared<int>(candidates.size());
                for (const SessionInfo &session : candidates) {
                    QDBusMessage propertyCall =
                        QDBusMessage::createMethodCall(
                            Logind::serviceName(),
                            session.sessionPath.path(),
                            QStringLiteral("org.freedesktop.DBus.Properties"),
                            QStringLiteral("GetAll"));
                    propertyCall << Logind::sessionIfaceName();
                    auto *propertyWatcher = new QDBusPendingCallWatcher(
                        QDBusConnection::systemBus().asyncCall(propertyCall),
                        this);
                    connect(propertyWatcher,
                        &QDBusPendingCallWatcher::finished, this,
                        [this, session, requestedType, generation, remaining]
                        (QDBusPendingCallWatcher *watcher) {
                            const QDBusPendingReply<QVariantMap> reply =
                                *watcher;
                            watcher->deleteLater();

                            if (!m_reuseLookupPending
                                    || m_reuseLookupGeneration != generation) {
                                return;
                            }

                            if (!reply.isError()) {
                                const QVariantMap properties = reply.value();
                                if (canReuseLogindSession(
                                        properties.value(QStringLiteral("Service")).toString(),
                                        properties.value(QStringLiteral("State")).toString(),
                                        properties.value(QStringLiteral("Class")).toString(),
                                        properties.value(QStringLiteral("Type")).toString(),
                                        requestedType)) {
                                    finishReusableSessionLookup(
                                        generation, session.sessionId,
                                        properties.value(
                                            QStringLiteral("VTNr")).toInt());
                                    return;
                                }
                            }

                            if (--*remaining == 0)
                                finishReusableSessionLookup(generation);
                        });
                }
            });
    }

    void Display::finishReusableSessionLookup(quint64 generation,
                                               const QString &sessionId,
                                               int sessionVt) {
        if (!m_reuseLookupPending
                || m_reuseLookupGeneration != generation) {
            return;
        }

        m_reuseLookupPending = false;
        m_reuseSessionId = sessionId;
        m_reuseSessionVt = sessionVt;
        if (!m_reuseSessionId.isNull())
            m_auth->setSession(QString());
        m_auth->start();
    }

    void Display::slotAuthenticationFinished(const QString &user, bool success) {
        m_authenticated = success;

        if (m_auth->autologin() && !success) {
            handleAutologinFailure();
            return;
        }

        if (success) {
            qDebug() << "Authentication for user " << user << " successful";

            const bool transitionPending = beginTransition();

            if (!m_reuseSessionId.isNull()) {
                if (!transitionPending) {
                    activateReusableSession();
                }
            } else {
                if (qobject_cast<XorgDisplayServer *>(m_displayServer))
                    m_auth->setCookie(qobject_cast<XorgDisplayServer *>(m_displayServer)->cookie());

                if (!m_displayManagerSessionName.isEmpty()) {
                    daemonApp->displayManager()->AddSession(m_displayManagerSessionName, seat()->name(), m_auth->user());
                }
            }
        } else {
            qDebug() << "Authentication for user " << user << " failed!!";
            finishLogin(false);
        }
    }

    bool Display::beginTransition() {
        // Identical Wayland executables still run as separate instances. Until
        // a compositor handoff protocol is available, only shared X11 can keep
        // its background. Restoring an existing session also uses a fade.
        const bool preserveBackground = m_sharedX11 && m_reuseSessionId.isNull();
        if (!m_socketServer->prepareTransition(m_socket, preserveBackground)) {
            return false;
        }

        qDebug() << "Preparing greeter transition:"
                 << (preserveBackground ? "preserve X11 background" : "fade to black");

        m_auth->deferSessionStart();
        m_transitionTimer->start();
        return true;
    }

    void Display::completeTransition() {
        m_transitionTimer->stop();

        if (m_stopping) {
            return;
        }

        if (!m_reuseSessionId.isNull()) {
            activateReusableSession();
        }

        m_auth->resumeSessionStart();
    }

    void Display::activateReusableSession() {
        const QString sessionId = m_reuseSessionId;
        const quint64 generation = ++m_reuseActivationGeneration;
        m_reuseActivationPending = true;
        m_reuseActivationTimer->start();

        OrgFreedesktopLogin1ManagerInterface manager(
            Logind::serviceName(), Logind::managerPath(),
            QDBusConnection::systemBus());
        auto *unlockWatcher = new QDBusPendingCallWatcher(
            manager.UnlockSession(sessionId), this);
        connect(unlockWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, sessionId, generation](QDBusPendingCallWatcher *watcher) {
                const QDBusPendingReply<> reply = *watcher;
                watcher->deleteLater();

                if (!m_reuseActivationPending
                        || m_reuseActivationGeneration != generation
                        || m_reuseSessionId != sessionId) {
                    return;
                }
                if (reply.isError()) {
                    finishReusableSessionActivation(false,
                        reply.error().message());
                    return;
                }

                OrgFreedesktopLogin1ManagerInterface manager(
                    Logind::serviceName(), Logind::managerPath(),
                    QDBusConnection::systemBus());
                auto *activateWatcher = new QDBusPendingCallWatcher(
                    manager.ActivateSession(sessionId), this);
                connect(activateWatcher,
                    &QDBusPendingCallWatcher::finished, this,
                    [this, sessionId, generation](QDBusPendingCallWatcher *watcher) {
                        const QDBusPendingReply<> reply = *watcher;
                        watcher->deleteLater();

                        if (!m_reuseActivationPending
                                || m_reuseActivationGeneration != generation
                                || m_reuseSessionId != sessionId) {
                            return;
                        }
                        finishReusableSessionActivation(!reply.isError(),
                            reply.isError() ? reply.error().message()
                                            : QString());
                    });
            });
    }

    void Display::finishReusableSessionActivation(bool success,
                                                    const QString &error) {
        if (!m_reuseActivationPending)
            return;

        m_reuseActivationPending = false;
        m_reuseActivationTimer->stop();

        if (!success) {
            qWarning() << "Failed to activate reusable logind session"
                       << m_reuseSessionId << error;
            m_reuseSessionId.clear();
            m_reuseSessionVt = 0;
        }
        finishLogin(success);

        if (m_reuseHelperFinished)
            stop();
    }

    void Display::finishLogin(bool success) {
        m_transitionTimer->stop();

        if (success) {
            if (mainConfig.Users.RememberLastUser.get())
                stateConfig.Last.User.set(m_auth->user());
            else
                stateConfig.Last.User.setDefault();
            if (mainConfig.Users.RememberLastSession.get())
                stateConfig.Last.Session.set(m_sessionName);
            else
                stateConfig.Last.Session.setDefault();
            stateConfig.save();

            if (m_socket)
                emit loginSucceeded(m_socket);
        } else if (m_socket) {
            // No match (tries used up, timeout): the greeter keeps the
            // password and decides whether to listen again. A matched finger
            // whose session failed to start is an ordinary failed login.
            if (m_fingerprintLogin && !m_authenticated)
                m_socketServer->fingerprintFinished(m_socket);
            else
                emit loginFailed(m_socket);
        }

        m_socket = nullptr;
        m_fingerprintLogin = false;
        m_auth->setFingerprint(false);
    }

    void Display::slotAuthInfo(const QString &message, Auth::Info info) {
        qWarning() << "Authentication information:" << info << message;

        // "Place your finger on <device>": the greeter shows its own hint.
        if (!m_socket || m_fingerprintLogin)
            return;

        m_socketServer->informationMessage(m_socket, message);
    }

    void Display::slotAuthError(const QString &message, Auth::Error error) {
        qWarning() << "Authentication error:" << error << message;

        if (!m_socket)
            return;

        if (m_fingerprintLogin) {
            // Conversation errors: mismatches and bad scans.
            if (error == Auth::ERROR_AUTHENTICATION)
                m_socketServer->fingerprintRejected(m_socket, message);
            return;
        }

        m_socketServer->informationMessage(m_socket, message);
    }

    void Display::slotHelperFinished(Auth::HelperExitStatus status) {
        removeDisplayManagerSession();

        // A cancelled fingerprint login was terminated on purpose: neither a
        // result for the greeter nor a reason to restart the display.
        if (m_cancellingFingerprint)
            return;

        if (m_reuseActivationPending) {
            m_reuseHelperFinished = true;
            return;
        }

        // Every greeter request must receive a terminal result. This also
        // covers helper crashes before AUTHENTICATED or SESSION_STATUS.
        if (m_socket) {
            qWarning() << "Authentication helper exited before login completed"
                       << status;
            finishLogin(false);
        }

        // Don't restart greeter and display server unless sddm-helper exited
        // with an internal error or the user session finished successfully,
        // we want to avoid greeter from restarting when an authentication
        // error happens (in this case we want to show the message from the
        // greeter
        if (status != Auth::HELPER_AUTH_ERROR)
            stop();
    }

    void Display::slotRequestChanged() {
        if (m_fingerprintLogin) {
            if (m_auth->request()->prompts().isEmpty())
                return;
            // gxdm-fingerprint asks nothing. Never answer a prompt with the
            // (empty) password: a misconfigured stack could accept it.
            qWarning() << "gxdm-fingerprint asked for input; check /etc/pam.d/gxdm-fingerprint";
            // Not from inside Auth's own signal.
            QTimer::singleShot(0, this, [this] {
                if (!m_fingerprintLogin || m_authenticated)
                    return;
                QPointer<QLocalSocket> socket = m_socket;
                cancelFingerprintLogin();
                if (socket)
                    m_socketServer->fingerprintFinished(socket);
            });
            return;
        }
        if (m_auth->request()->prompts().length() == 1) {
            m_auth->request()->prompts()[0]->setResponse(qPrintable(m_passPhrase));
            m_auth->request()->done();
        } else if (m_auth->request()->prompts().length() == 2) {
            m_auth->request()->prompts()[0]->setResponse(qPrintable(m_auth->user()));
            m_auth->request()->prompts()[1]->setResponse(qPrintable(m_passPhrase));
            m_auth->request()->done();
        }
    }

    void Display::slotSessionStarted(bool success) {
        qDebug() << "Session started" << success;
        if (success) {
            const qint64 helperPid = m_auth->helperProcessId();
            finishLogin(true);
            resolveLogindSession(helperPid);
            m_greeter->stop();
        } else {
            finishLogin(false);
            removeDisplayManagerSession();
        }
    }

    void Display::resolveLogindSession(qint64 helperPid) {
        if (helperPid <= 0 || !Logind::isAvailable())
            return;

        OrgFreedesktopLogin1ManagerInterface manager(
            Logind::serviceName(), Logind::managerPath(),
            QDBusConnection::systemBus());
        auto *sessionWatcher = new QDBusPendingCallWatcher(
            manager.GetSessionByPID(static_cast<uint>(helperPid)), this);
        connect(sessionWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, helperPid](QDBusPendingCallWatcher *watcher) {
                const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
                watcher->deleteLater();
                if (m_auth->helperProcessId() != helperPid)
                    return;
                if (reply.isError()) {
                    qWarning() << "Could not determine logind session for helper"
                               << helperPid << reply.error().message();
                    return;
                }

                QDBusMessage propertyCall = QDBusMessage::createMethodCall(
                    Logind::serviceName(), reply.value().path(),
                    QStringLiteral("org.freedesktop.DBus.Properties"),
                    QStringLiteral("Get"));
                propertyCall << QStringLiteral("org.freedesktop.login1.Session")
                             << QStringLiteral("Id");
                auto *idWatcher = new QDBusPendingCallWatcher(
                    QDBusConnection::systemBus().asyncCall(propertyCall), this);
                connect(idWatcher, &QDBusPendingCallWatcher::finished, this,
                    [this, helperPid](QDBusPendingCallWatcher *watcher) {
                        const QDBusPendingReply<QVariant> reply = *watcher;
                        watcher->deleteLater();
                        if (m_auth->helperProcessId() != helperPid)
                            return;
                        if (reply.isError()) {
                            qWarning()
                                << "Could not read logind session ID for helper"
                                << helperPid << reply.error().message();
                            return;
                        }

                        m_logindSessionId = reply.value().toString();
                        qInfo() << "User session is managed by logind session"
                                << m_logindSessionId;
                    });
            });
    }

    void Display::removeDisplayManagerSession() {
        if (m_displayManagerSessionName.isEmpty()) {
            return;
        }

        daemonApp->displayManager()->RemoveSession(m_displayManagerSessionName);
        m_displayManagerSessionName.clear();
    }
}
