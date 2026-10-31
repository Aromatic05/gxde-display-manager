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

#ifndef SDDM_DISPLAY_H
#define SDDM_DISPLAY_H

#include <QObject>
#include <QPointer>
#include <QDir>

#include "Auth.h"
#include "Session.h"

class QLocalSocket;
class QTimer;

namespace SDDM {
    class Authenticator;
    class DisplayServer;
    class Seat;
    class SocketServer;
    class Greeter;

    class Display : public QObject {
        Q_OBJECT
        Q_DISABLE_COPY(Display)
    public:
        enum DisplayServerType {
            X11DisplayServerType,
            X11UserDisplayServerType,
            WaylandDisplayServerType
        };
        Q_ENUM(DisplayServerType)

        static DisplayServerType defaultDisplayServerType();
        explicit Display(Seat *parent, DisplayServerType serverType);
        ~Display();

        DisplayServerType displayServerType() const;
        DisplayServer *displayServer() const;

        int terminalId() const;

        const QString &name() const;

        QString sessionType() const;
        QString reuseSessionId() const { return m_reuseSessionId; }
        int reuseSessionVt() const { return m_reuseSessionVt; }

        Seat *seat() const;

    public slots:
        bool start();
        void stop();

        void login(QLocalSocket *socket,
                   const QString &user, const QString &password,
                   const Session &session);
        bool attemptAutologin();
        void displayServerStarted();

        // Fingerprint login: the gxdm-fingerprint PAM service on the login
        // Auth, so a match opens the session exactly like a password.
        void fingerprintProbe(QLocalSocket *socket, const QString &user);
        void fingerprintLogin(QLocalSocket *socket, const QString &user,
                              const Session &session);
        void fingerprintCancel(QLocalSocket *socket);

    signals:
        void stopped();
        void displayServerFailed();

        void loginFailed(QLocalSocket *socket);
        void loginSucceeded(QLocalSocket *socket);

    private:
        QString findGreeterTheme() const;

        bool startAuth(const QString &user, const QString &password,
                       const Session &session);

        void startSocketServerAndGreeter();
        void handleAutologinFailure();
        void findReusableSession(const QString &user,
            const QString &requestedType);
        void finishReusableSessionLookup(quint64 generation,
            const QString &sessionId = QString(),
            int sessionVt = 0);
        void activateReusableSession();
        void finishReusableSessionActivation(bool success,
                                              const QString &error = QString());
        void finishLogin(bool success);
        bool beginTransition();
        void completeTransition();
        void resolveLogindSession(qint64 helperPid);
        void removeDisplayManagerSession();
        void cancelFingerprintLogin();

        DisplayServerType m_displayServerType = X11DisplayServerType;

        bool m_sharedX11 { false };
        QTimer *m_transitionTimer { nullptr };

        bool m_relogin { true };
        bool m_started { false };
        bool m_stopping { false };
        bool m_reuseLookupPending { false };
        bool m_reuseActivationPending { false };
        bool m_reuseHelperFinished { false };
        bool m_fingerprintLogin { false };       // m_auth runs gxdm-fingerprint
        bool m_cancellingFingerprint { false };  // inside cancelFingerprintLogin()
        bool m_authenticated { false };          // PAM accepted the current request
        quint64 m_reuseLookupGeneration { 0 };
        quint64 m_reuseActivationGeneration { 0 };

        int m_terminalId = 0;
        int m_sessionTerminalId = 0;
        int m_reuseSessionVt = 0;

        QString m_passPhrase;
        QString m_sessionName;
        QString m_displayManagerSessionName;
        QString m_reuseSessionId;
        QString m_logindSessionId;

        Auth *m_auth { nullptr };
        DisplayServer *m_displayServer { nullptr };
        Seat *m_seat { nullptr };
        SocketServer *m_socketServer { nullptr };
        QPointer<QLocalSocket> m_socket;
        Greeter *m_greeter { nullptr };
        QTimer *m_reuseActivationTimer { nullptr };

    private slots:
        void slotRequestChanged();
        void slotAuthenticationFinished(const QString &user, bool success);
        void slotSessionStarted(bool success);
        void slotHelperFinished(Auth::HelperExitStatus status);
        void slotAuthInfo(const QString &message, Auth::Info info);
        void slotAuthError(const QString &message, Auth::Error error);
    };
}

#endif // SDDM_DISPLAY_H
