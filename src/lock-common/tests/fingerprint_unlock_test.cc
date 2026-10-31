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

#include "fingerprintunlock.h"

#include <QSignalSpy>
#include <QTest>

namespace {

using gxde::lock::FingerprintUnlock;
using State = FingerprintUnlock::State;

// Replaces fprintd and gxdm-helper: records calls, and lets the test answer.
class FakeFingerprint : public FingerprintUnlock {
 public:
  FakeFingerprint() { SetQuickFailureMs(200); }

  QStringList probes;
  int runs = 0;
  int stops = 0;

  using FingerprintUnlock::ProbeFinished;
  using FingerprintUnlock::RunFinished;
  using FingerprintUnlock::RunRejected;

 protected:
  void Probe(const QString& user) override { probes << user; }
  void StartRun(const QString&) override { ++runs; }
  void StopRun() override { ++stops; }
};

}  // namespace

class FingerprintUnlockTest : public QObject {
  Q_OBJECT

 private slots:
  void matchesAfterProbe() {
    FakeFingerprint fp;
    QSignalSpy matched(&fp, &FingerprintUnlock::Matched);
    fp.Start(QStringLiteral("demo"));
    QCOMPARE(fp.GetState(), State::kProbing);
    QCOMPARE(fp.probes, QStringList{QStringLiteral("demo")});
    QCOMPARE(fp.runs, 0);

    fp.ProbeFinished(QStringLiteral("demo"), true);
    QCOMPARE(fp.GetState(), State::kListening);
    QVERIFY(fp.Listening());
    QCOMPARE(fp.runs, 1);

    // Starting again while listening changes nothing.
    fp.Start(QStringLiteral("demo"));
    QCOMPARE(fp.runs, 1);
    QCOMPARE(fp.probes.size(), 1);

    fp.RunFinished(true);
    QCOMPARE(matched.size(), 1);
    QCOMPARE(fp.GetState(), State::kIdle);
  }

  void unavailableWithoutReaderOrPrints() {
    FakeFingerprint fp;
    fp.Start(QStringLiteral("demo"));
    fp.ProbeFinished(QStringLiteral("demo"), false);
    QCOMPARE(fp.GetState(), State::kUnavailable);
    QCOMPARE(fp.runs, 0);

    // Start() probes again: a reader may have been plugged in since.
    fp.Start(QStringLiteral("demo"));
    QCOMPARE(fp.probes.size(), 2);
  }

  void rejectionsAreReportedAndRunGoesOn() {
    FakeFingerprint fp;
    QSignalSpy rejected(&fp, &FingerprintUnlock::Rejected);
    fp.Start(QStringLiteral("demo"));
    fp.ProbeFinished(QStringLiteral("demo"), true);

    fp.RunRejected(QStringLiteral("Failed to match fingerprint"));
    QCOMPARE(rejected.size(), 1);
    QCOMPARE(rejected.at(0).at(0).toString(),
             QStringLiteral("Failed to match fingerprint"));
    QCOMPARE(fp.GetState(), State::kListening);
  }

  void givesUpRightAfterLastMismatch() {
    FakeFingerprint fp;
    fp.Start(QStringLiteral("demo"));
    fp.ProbeFinished(QStringLiteral("demo"), true);
    fp.RunRejected(QStringLiteral("no match"));
    fp.RunRejected(QStringLiteral("no match"));
    fp.RunRejected(QStringLiteral("no match"));
    // pam_fprintd fails at once after its last try.
    fp.RunFinished(false);
    QCOMPARE(fp.GetState(), State::kGaveUp);
    QCOMPARE(fp.runs, 1);

    // Re-armed by an explicit Start().
    fp.Start(QStringLiteral("demo"));
    fp.ProbeFinished(QStringLiteral("demo"), true);
    QCOMPARE(fp.GetState(), State::kListening);
    QCOMPARE(fp.runs, 2);
  }

  void restartsQuietlyAfterTimeout() {
    FakeFingerprint fp;
    QSignalSpy rejected(&fp, &FingerprintUnlock::Rejected);
    fp.Start(QStringLiteral("demo"));
    fp.ProbeFinished(QStringLiteral("demo"), true);
    // No finger until pam_fprintd's timeout.
    QTest::qWait(250);
    fp.RunFinished(false);
    QCOMPARE(fp.GetState(), State::kListening);
    QCOMPARE(fp.runs, 2);
    QCOMPARE(fp.probes.size(), 1);
    QCOMPARE(rejected.size(), 0);
  }

  void timeoutAfterOneMismatchKeepsListening() {
    FakeFingerprint fp;
    fp.Start(QStringLiteral("demo"));
    fp.ProbeFinished(QStringLiteral("demo"), true);
    fp.RunRejected(QStringLiteral("no match"));
    // A wrong finger once, then nothing: the next try timed out.
    QTest::qWait(250);
    fp.RunFinished(false);
    QCOMPARE(fp.GetState(), State::kListening);
    QCOMPARE(fp.runs, 2);
  }

  void quickFailureWithoutScanMeansUnavailable() {
    FakeFingerprint fp;
    fp.Start(QStringLiteral("demo"));
    fp.ProbeFinished(QStringLiteral("demo"), true);
    // e.g. no gxdm-fingerprint PAM service: fails before any scan.
    fp.RunFinished(false);
    QCOMPARE(fp.GetState(), State::kUnavailable);
    QCOMPARE(fp.runs, 1);
  }

  void stopEndsTheRunAndIgnoresLateResults() {
    FakeFingerprint fp;
    QSignalSpy matched(&fp, &FingerprintUnlock::Matched);
    fp.Start(QStringLiteral("demo"));
    fp.ProbeFinished(QStringLiteral("demo"), true);
    fp.Stop();
    QCOMPARE(fp.GetState(), State::kIdle);
    QCOMPARE(fp.stops, 1);

    fp.RunFinished(true);
    fp.RunRejected(QStringLiteral("late"));
    QCOMPARE(matched.size(), 0);
    QCOMPARE(fp.GetState(), State::kIdle);
  }

  void staleProbeIsIgnored() {
    FakeFingerprint fp;
    fp.Start(QStringLiteral("demo"));
    fp.Stop();
    fp.ProbeFinished(QStringLiteral("demo"), true);
    QCOMPARE(fp.GetState(), State::kIdle);
    QCOMPARE(fp.runs, 0);

    // A probe for another user than the current one.
    fp.Start(QStringLiteral("demo"));
    fp.ProbeFinished(QStringLiteral("other"), true);
    QCOMPARE(fp.GetState(), State::kProbing);
  }
};

QTEST_GUILESS_MAIN(FingerprintUnlockTest)
#include "fingerprint_unlock_test.moc"
