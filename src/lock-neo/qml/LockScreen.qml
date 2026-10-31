// Copyright (C) 2026 CharOfString <root@charofstring.cc>
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, version 3.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.

import QtQuick
import QtQuick.Effects

Item {
  id: screen

  property var backend: null
  property bool primaryScreen: true
  property bool virtualKeyboardAvailable: false
  property bool testMode: false
  property bool windowVisible: true
  readonly property bool ready: backend !== null

  // Lock anim: blur strength graduately applies, and 
  property real revealProgress: 0
  readonly property real wallpaperBlurMax: 1.0
  property bool unlocking: false

  property bool passwordStage: false
  property real loginProgress: passwordStage ? 1.0 : 0.0
  property bool mediaCardRequested: false

  Behavior on loginProgress {
    enabled: screen.windowVisible
    NumberAnimation {
      duration: 180
      easing.type: Easing.Linear
    }
  }

  function stageEase(start, end) {
    const t = Math.max(0, Math.min(1,
      (screen.loginProgress - start) / (end - start)))
    return 1 - Math.pow(1 - t, 3)
  }

  readonly property real clockProgress: stageEase(0.0, 1.0)
  readonly property real pillProgress: stageEase(0.0, 0.25)
  readonly property real cardProgress: stageEase(0.05, 1.0)
  readonly property real controlsProgress: stageEase(0.1, 1.0)

  function enterPasswordStage() {
    if (!screen.primaryScreen || screen.unlocking) return
    screen.passwordStage = true
    screen.forceActiveFocus()
    loginCard.focusPassword()
  }

  function leavePasswordStage() {
    if (!screen.passwordStage) return
    if (screen.backend && screen.backend.authenticating) return
    if (screen.backend && screen.backend.pendingPowerAction.length > 0) {
      screen.backend.CancelPowerAction()
    }
    screen.passwordStage = false
    screen.mediaCardRequested = false
    vkbLoader.active = false
    loginCard.clearPassword()
    screen.forceActiveFocus()
  }

  function closePopups() {
    let closed = false
    if (userSwitcher.opened) { userSwitcher.close(); closed = true }
    if (powerMenu.opened) { powerMenu.close(); closed = true }
    if (screen.mediaCardRequested) {
      screen.mediaCardRequested = false
      closed = true
    }
    return closed
  }

  signal requestQuit()
  signal unlockFadeFinished()

  function startReveal() {
    if (!screen.windowVisible || wallpaper.status !== Image.Ready) return
    revealAnimation.restart()
  }

  function resetReveal() {
    revealAnimation.stop()
    unlockFadeAnimation.stop()
    screen.revealProgress = 0
    screen.unlocking = false
    screen.opacity = 1
  }

  function cycleLayout() {
    if (!screen.backend) return
    const layouts = screen.backend.kbLayouts
    if (!layouts || layouts.length < 2) return
    const current = layouts.indexOf(screen.backend.currentKbLayout)
    screen.backend.SetKbLayout(layouts[(current + 1) % layouts.length])
  }

  function requestPower(action) {
    if (!screen.backend) return
    screen.backend.RequestPower(action)
    loginCard.focusPassword()
  }

  function handleAuthenticated() {
    loginCard.showSuccess()
    screen.unlocking = true
    unlockFadeAnimation.restart()
  }

  function handleAuthenticationFailed(message) {
    screen.enterPasswordStage()
    loginCard.showError(message)
    authFailureGlow.flash()
  }

  function handlePowerActionStarted(action) {
    screen.enterPasswordStage()
    loginCard.showPowerProgress(action)
  }

  function handleShowUserList() {
    if (screen.primaryScreen && screen.backend
        && screen.backend.userSwitchAvailable) {
      screen.enterPasswordStage()
      userSwitcher.open()
    }
  }

  Shortcut {
    sequence: "Escape"
    enabled: !screen.unlocking
    onActivated: {
      if (screen.closePopups()) return
      if (screen.passwordStage) {
        screen.leavePasswordStage()
      } else if (screen.testMode) {
        screen.requestQuit()
      }
    }
  }

  Keys.onPressed: (event) => {
    if (screen.passwordStage || screen.unlocking) return
    screen.enterPasswordStage()
    event.accepted = true
  }

  TapHandler {
    acceptedButtons: Qt.AllButtons
    enabled: !screen.passwordStage && !screen.unlocking
    onTapped: screen.enterPasswordStage()
  }

  onVisibleChanged: {
    if (visible && screen.windowVisible) screen.forceActiveFocus()
  }

  function resetNotifications() {
    if (screen.backend && screen.backend.notifications)
      screen.backend.notifications.Reset()
  }

  onWindowVisibleChanged: {
    if (windowVisible) {
      screen.resetNotifications()
      screen.startReveal()
      screen.forceActiveFocus()
    } else {
      screen.resetReveal()
      screen.passwordStage = false
      screen.mediaCardRequested = false
      vkbLoader.active = false
      loginCard.clearPassword()
    }
  }

  Component.onCompleted: {
    screen.resetNotifications()
    screen.startReveal()
    if (screen.windowVisible) screen.forceActiveFocus()
  }

  NumberAnimation {
    id: revealAnimation

    target: screen
    property: "revealProgress"
    from: 0
    to: 1
    duration: 700
    easing.type: Easing.OutCubic
  }

  NumberAnimation {
    id: unlockFadeAnimation

    target: screen
    property: "opacity"
    to: 0
    duration: 450
    easing.type: Easing.InOutQuad
    onFinished: screen.unlockFadeFinished()
  }

  // --- Wallpaper -------------------------------------------------------------

  Rectangle {
    anchors.fill: parent
    gradient: Gradient {
      GradientStop { position: 0.0; color: "#18202b" }
      GradientStop { position: 0.55; color: "#10151c" }
      GradientStop { position: 1.0; color: "#07090c" }
    }
  }

  Image {
    id: wallpaper

    anchors.fill: parent
    source: screen.ready ? screen.backend.backgroundUri : ""
    fillMode: Image.PreserveAspectCrop
    asynchronous: true
    cache: false
    smooth: true
    mipmap: true
    onStatusChanged: if (status === Image.Ready) screen.startReveal()
    visible: source.toString() !== "" && status === Image.Ready
    opacity: screen.revealProgress
    layer.enabled: visible
    layer.smooth: true
    layer.effect: MultiEffect {
      blurEnabled: true
      blurMax: 32
      blur: screen.wallpaperBlurMax * screen.revealProgress
      brightness: -0.22
      saturation: 0.9
      contrast: 0.02
    }
  }

  Rectangle {
    anchors.fill: parent
    color: "#33000000"
  }

  Rectangle {
    anchors.fill: parent
    gradient: Gradient {
      GradientStop { position: 0.0; color: "#66000000" }
      GradientStop { position: 0.45; color: "#1f000000" }
      GradientStop { position: 1.0; color: "#a6000000" }
    }
  }

  // --- Clock -----------------------------------------------------------------

  Clock {
    id: clock

    readonly property real topY: Math.max(32, Math.round(parent.height * 0.06))
    readonly property real centerY: Math.round((parent.height - height
      - (notificationPill.shown ? notificationPill.height + 20 : 0)) / 2)

    anchors.horizontalCenter: parent.horizontalCenter
    y: Math.round(centerY + (topY - centerY) * screen.clockProgress)
    referenceHeight: parent.height
    compact: screen.clockProgress
  }

  NotificationPill {
    id: notificationPill

    readonly property bool shown: !!(screen.primaryScreen && screen.backend
      && screen.backend.notifications
      && screen.backend.notifications.available
      && screen.backend.notifications.count > 0)

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: clock.bottom
    anchors.topMargin: 20
    count: shown ? screen.backend.notifications.count : 0
    opacity: 1.0 - screen.pillProgress
    visible: shown && opacity > 0
  }

  // Secondary outputs keep the wallpaper and clock but do not duplicate the
  // password prompt.
  ShadowText {
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: clock.bottom
    anchors.topMargin: 16
    visible: !screen.primaryScreen
    text: qsTr("屏幕已锁定")
    pixelSize: 18
    weight: Font.Light
    shadowOpacity: 0.22
  }


  // --- Login -----------------------------------------------------------------

  LoginCard {
    id: loginCard

    anchors.centerIn: parent
    anchors.verticalCenterOffset: vkbLoader.active
      ? -Math.round(Math.min(vkbLoader.height / 2, parent.height * 0.18)) : 0
    visible: screen.primaryScreen
      && (screen.passwordStage || screen.cardProgress > 0)
    enabled: screen.passwordStage && screen.backend !== null
    opacity: screen.cardProgress
    scale: 0.97 + 0.03 * screen.cardProgress
    transform: Translate { y: Math.round((1 - screen.cardProgress) * 24) }
    backend: screen.backend
    testMode: screen.testMode
    onRequestQuit: screen.requestQuit()
  }

  Text {
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 28
    visible: screen.testMode && screen.primaryScreen && !screen.passwordStage
    text: qsTr("演示模式：按任意键继续，Esc 退出")
    color: "#99c9d5e8"
    font.pixelSize: 12
    renderType: Text.NativeRendering
  }

  // --- Bottom controls -------------------------------------------------------

  Row {
    id: controls

    readonly property real revealProgress: screen.controlsProgress
    readonly property real bottomMargin: 28
    readonly property real hiddenY: parent.height - height / 2
    readonly property real shownY: parent.height - height - bottomMargin

    anchors.right: parent.right
    anchors.rightMargin: 28
    y: hiddenY + (shownY - hiddenY) * revealProgress
    opacity: revealProgress
    spacing: 12
    visible: screen.primaryScreen && screen.ready
      && (screen.passwordStage || revealProgress > 0)

    IconButton {
      visible: screen.backend && screen.backend.kbLayouts
        && screen.backend.kbLayouts.length > 1
      label: screen.backend && screen.backend.currentKbLayout
        ? screen.backend.currentKbLayout.toUpperCase() : ""
      accessibleName: qsTr("键盘布局")
      onClicked: screen.cycleLayout()
    }

    IconButton {
      visible: screen.backend && screen.backend.userSwitchAvailable
      source: "qrc:/lock/icons/user-switch.svg"
      accessibleName: qsTr("切换用户")
      onClicked: {
        screen.closePopups()
        userSwitcher.open()
      }
    }

    IconButton {
      id: volumeButton

      objectName: "volumeButton"
      readonly property var audio: (screen.backend && screen.backend.audio)
        || null

      visible: mediaIndicator.hasAudio || mediaIndicator.hasMedia
      source: mediaIndicator.volumeIcon(audio)
      padding: 7
      iconSize: 18
      accessibleName: qsTr("音量")
      onClicked: {
        const wasOpen = screen.mediaCardRequested
        screen.closePopups()
        screen.mediaCardRequested = !wasOpen
        if (!wasOpen) mediaIndicator.volumePinned = true
      }

      WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
          const audio = volumeButton.audio
          if (!audio || event.angleDelta.y === 0) return
          const base = audio.muted ? 0 : audio.volume
          audio.SetVolume(base + (event.angleDelta.y > 0 ? 0.05 : -0.05))
        }
      }
    }

    IconButton {
      visible: screen.virtualKeyboardAvailable
      source: "qrc:/icons/keyboard.svg"
      accessibleName: qsTr("虚拟键盘")
      onClicked: vkbLoader.active = !vkbLoader.active
    }

    IconButton {
      source: "qrc:/icons/power.svg"
      accessibleName: qsTr("电源")
      onClicked: {
        const wasOpen = powerMenu.opened
        screen.closePopups()
        if (!wasOpen) powerMenu.open()
      }
    }
  }

  MediaIndicator {
    id: mediaIndicator

    readonly property bool shown: screen.primaryScreen && !vkbLoader.active
      && (screen.passwordStage
            ? screen.mediaCardRequested && (hasMedia || hasAudio)
            : hasMedia)

    objectName: "mediaCard"
    anchors.right: parent.right
    anchors.rightMargin: 28
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 28 + 32 + 16
    media: (screen.backend && screen.backend.media) || null
    audio: (screen.backend && screen.backend.audio) || null
    opacity: shown ? 1.0 : 0.0
    visible: opacity > 0
    onShownChanged: if (!shown) collapse()

    Behavior on opacity {
      NumberAnimation {
        duration: 140
        easing.type: Easing.OutCubic
      }
    }
  }

  UserSwitcher {
    id: userSwitcher
    backend: screen.backend
  }

  PowerMenu {
    id: powerMenu
    backend: screen.backend
    onTriggered: function(action) { screen.requestPower(action) }
  }

  Loader {
    id: vkbLoader

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    width: Math.min(parent.width - 32, 900)
    height: item ? item.implicitHeight : 0
    active: false
    sourceComponent: Component {
      VirtualKeyboardPanel { }
    }

    onLoaded: {
      if (item) {
        item.width = Qt.binding(function() { return vkbLoader.width })
      }
    }
  }

  Item {
    id: authFailureGlow

    readonly property color glowColor: "#be543c"
    readonly property real glowSize: Math.min(width, height) * 0.14

    function flash() {
      flashAnimation.restart()
    }

    objectName: "authFailureGlow"
    anchors.fill: parent
    z: 100
    opacity: 0
    visible: opacity > 0

    SequentialAnimation {
      id: flashAnimation

      NumberAnimation {
        target: authFailureGlow
        property: "opacity"
        to: 1.0
        duration: 160
        easing.type: Easing.OutCubic
      }
      PauseAnimation {
        duration: 700
      }
      NumberAnimation {
        target: authFailureGlow
        property: "opacity"
        to: 0.0
        duration: 900
        easing.type: Easing.InOutQuad
      }
    }

    component EdgeGlow: Rectangle {
      required property real edgeRotation
      rotation: edgeRotation
      gradient: Gradient {
        GradientStop {
          position: 0.0
          color: Qt.rgba(authFailureGlow.glowColor.r,
            authFailureGlow.glowColor.g, authFailureGlow.glowColor.b, 0.6)
        }
        GradientStop {
          position: 0.35
          color: Qt.rgba(authFailureGlow.glowColor.r,
            authFailureGlow.glowColor.g, authFailureGlow.glowColor.b, 0.22)
        }
        GradientStop {
          position: 1.0
          color: Qt.rgba(authFailureGlow.glowColor.r,
            authFailureGlow.glowColor.g, authFailureGlow.glowColor.b, 0.0)
        }
      }
    }

    EdgeGlow {
      edgeRotation: 0
      x: 0
      y: 0
      width: parent.width
      height: parent.glowSize
    }

    EdgeGlow {
      edgeRotation: 180
      x: 0
      y: parent.height - height
      width: parent.width
      height: parent.glowSize
    }

    EdgeGlow {
      edgeRotation: -90
      x: (parent.glowSize - parent.height) / 2
      y: (parent.height - parent.glowSize) / 2
      width: parent.height
      height: parent.glowSize
    }

    EdgeGlow {
      edgeRotation: 90
      x: parent.width - (parent.glowSize + parent.height) / 2
      y: (parent.height - parent.glowSize) / 2
      width: parent.height
      height: parent.glowSize
    }
  }
}
