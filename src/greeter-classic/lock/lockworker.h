#ifndef LOCKWORKER_H
#define LOCKWORKER_H
// PATCHS
// Substantially rewritten vs the dde-lock original. dde-daemon auth (Authority /
// DBusLockService / Hotzone / Accounts / Logined) is gone. Unlock reuses GXDM's
// own SDDM::Auth in "auth-only" mode: with no session set, gxdm-helper runs the
// gxdm PAM stack and exits on success without starting a session. No hand-rolled
// PAM and no direct logind here.

#include "userinfo.h"
#include "authinterface.h"

#include "Auth.h"  // SDDM::Auth (src/auth)
#include "fingerprintunlock.h"

#include <QObject>

class SessionBaseModel;
class LockWorker : public Auth::AuthInterface
{
    Q_OBJECT
public:
    explicit LockWorker(SessionBaseModel *const model, QObject *parent = nullptr);

    void switchToUser(std::shared_ptr<User> user) override;
    void authUser(const QString &password) override;

    bool IsAuthenticating() const override;

    void enableZoneDetected(bool disable);  // hot-zone removed; kept as no-op

    // Fingerprint unlock, run alongside the password (see FingerprintUnlock).
    // It listens while the lock is shown (SessionBaseModel::visibleChanged);
    // frontends call startFingerprint() again to re-arm it after pam_fprintd
    // gave up.
    gxde::lock::FingerprintUnlock *fingerprint() const { return m_fingerprint; }
    void startFingerprint();

signals:
    // Emitted when a successful authentication is about to perform a power
    // action instead of simply unlocking the session. The QML frontend uses
    // this to leave its busy state and show progress.
    void powerActionRequested(const QString &action);

private:
    void onUserAdded(const QString &user) override;
    void slotRequestChanged();                          // feed the password to PAM
    void onAuthentication(const QString &user, bool ok);  // unlock result
    void onFingerprintMatched();
    void onFingerprintRejected(const QString &message);
    void finishUnlock();  // after a password or a finger: power action or unlock

    void requestGxdmPowerAction(const QString &method);     // route through GXDM
    void requestLogin1PowerAction(const QString &method);  // fallback

    bool m_authenticating;
    SDDM::Auth *m_auth;
    gxde::lock::FingerprintUnlock *m_fingerprint;
    QString m_password;
};

// PATCHE
#endif  // LOCKWORKER_H
