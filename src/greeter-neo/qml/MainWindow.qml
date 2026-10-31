// Copyright (C) CharOfString <root@charofstring.cc>
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
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Window

Item {
  id: root

  property url backgroundUri: ""
  property date now: new Date()
  property color iconTint: "white"
  property bool isGxdeOs: false
  property string gxdeReleaseLabel: ""
  property var userModel: []
  property var sessionModel: []
  property int selectedSessionIndex: sessionModel.length > 0 ? 0 : -1
  readonly property var selectedSession: selectedSessionIndex >= 0
    && selectedSessionIndex < sessionModel.length
      ? sessionModel[selectedSessionIndex] : null
  property real activeBackgroundScale: 1.08
  property real activeBackgroundBlur: 0.55
  property real loginProgress: clockView.positionedAtTop ? 1.0 : 0.0
  property real dialogProgress: passwordCard.shown ? 1.0 : 0.0
  property QtObject greeterBackend: null
  property QtObject screenManager: null
  property bool sessionChosen: false
  property bool loginSucceeded: false
  property bool transitioning: false
  property bool preserveBackground: false
  property real transitionProgress: 0

  NumberAnimation {
    id: loginTransition
    target: root
    property: "transitionProgress"
    from: 0
    to: 1
    duration: 150
    easing.type: Easing.InOutQuad
    onFinished: {
      if (root.greeterBackend) {
        root.greeterBackend.FinishTransition()
      }
    }
  }
  readonly property var availablePowerActions: greeterBackend
    ? greeterBackend.availableActions : []
  property bool virtualKeyboardRequested: false
  readonly property real keyboardInset: virtualKeyboard.visible
    ? (virtualKeyboard.height + virtualKeyboard.screenMargin)
      * virtualKeyboard.slide
    : 0
  readonly property var weekdays: [
    "星期日",
    "星期一",
    "星期二",
    "星期三",
    "星期四",
    "星期五",
    "星期六"
  ]

  signal loginRequested()
  signal sessionSelected(var session)
  signal userSelected(int index, var user)
  signal passwordSubmitted(int index, var user, string password)
  signal manualLoginRequested()
  signal powerRequested(string action)
  onPowerRequested: (action) => {
    if (greeterBackend) {
      greeterBackend.Request(action)
    }
  }

  function restoreSession() {
    if (sessionChosen || !greeterBackend) return
    const preferred = greeterBackend.lastSession
    for (let i = 0; i < sessionModel.length; ++i) {
      if (sessionModel[i].id === preferred) {
        selectedSessionIndex = i
        return
      }
    }
  }

  property var backendConnections: []

  function connectSignal(signal, handler) {
    signal.connect(handler)
    backendConnections.push({ signal: signal, handler: handler })
  }

  Component.onCompleted: {
    if (screenManager) {
      connectSignal(screenManager.CancelForwarded, root.closeTopPopup)
    }

    if (!greeterBackend) {
      return
    }

    connectSignal(greeterBackend.RequestFailed, root.handleRequestFailed)
    if (greeterBackend.PrepareTransition) {
      connectSignal(greeterBackend.PrepareTransition, root.handlePrepareTransition)
    }

    connectSignal(greeterBackend.LoginFailed, root.handleLoginFailed)
    connectSignal(greeterBackend.InformationMessage, root.handleInformationMessage)
    connectSignal(greeterBackend.LoginSucceeded, root.handleLoginSucceeded)
    connectSignal(greeterBackend.LastSessionChanged, root.restoreSession)
    if (greeterBackend.FingerprintRejected) {
      connectSignal(greeterBackend.FingerprintRejected, root.showPasswordError)
      connectSignal(greeterBackend.FingerprintChanged,
                    root.handleFingerprintChanged)
    }
    restoreSession()
    syncFingerprint()
  }

  // 指纹登录：登录框为列表中的用户打开时监听（手动输入用户名时不监听），
  // 匹配后按当前所选会话登录，与密码走同一条路径。
  readonly property string fingerprintTarget: passwordCard.shown
      && !passwordCard.askUserName && passwordCard.user
      && !passwordCard.busy && !root.transitioning && !root.loginSucceeded
    ? passwordCard.user.username + "\n"
      + (root.selectedSession ? root.selectedSession.id : "")
    : ""
  readonly property bool fingerprintListening: !!greeterBackend
    && !!greeterBackend.fingerprintListening

  onFingerprintTargetChanged: syncFingerprint()

  function syncFingerprint() {
    if (!greeterBackend || !greeterBackend.StartFingerprint) return
    if (fingerprintTarget === "") {
      greeterBackend.StopFingerprint()
    } else {
      greeterBackend.StartFingerprint(passwordCard.user.username,
        root.selectedSession ? root.selectedSession.id : "")
    }
  }

  function handleFingerprintChanged() {
    if (greeterBackend.fingerprintGaveUp && passwordCard.shown) {
      passwordCard.errorText = qsTr("指纹多次不匹配，请输入密码")
    }
  }

  Component.onDestruction: {
    for (const connection of backendConnections) {
      connection.signal.disconnect(connection.handler)
    }
  }

  function handleRequestFailed(message) {
    powerError.text = message
    powerErrorPopup.open()
  }

  function handlePrepareTransition(preserve) {
    root.preserveBackground = preserve
    root.transitioning = true
    passwordCard.clearPassword()
    foregroundFadeIn.stop()
    loginTransition.restart()
  }

  function handleLoginFailed(message) {
    loginTransition.stop()
    root.transitioning = false
    root.transitionProgress = 0
    root.showPasswordError(message)
  }

  function handleInformationMessage(message) {
    passwordCard.errorText = message
  }

  function handleLoginSucceeded() {
    root.loginSucceeded = true
    passwordCard.clearPassword()
    root.virtualKeyboardRequested = false
  }

  function moveScene() {
    const target = screenManager ? screenManager.currentWindow : null
    const previous = scene.Window.window
    if (!target || target === previous) return
    const focused = previous ? previous.activeFocusItem : null
    scene.parent = target.contentItem
    if (Qt.platform.pluginName === "xcb") target.raise()
    target.requestActivate()
    if (previous && !root.transitioning) foregroundFadeIn.restart()
    if (focused && focused !== previous.contentItem) {
      focused.forceActiveFocus()
    } else if (passwordCard.opened) {
      passwordCard.forceActiveFocus()
    } else {
      keyCatcher.forceActiveFocus()
    }
  }

  Connections {
    target: root.screenManager
    function onCurrentWindowChanged() {
      root.moveScene()
    }
  }

  function closeTopPopup() {
    for (const popup of [powerErrorPopup, sessionMenu, powerMenu,
        passwordCard]) {
      if (popup.opened && (popup.closePolicy & Popup.CloseOnEscape)) {
        popup.close()
        return
      }
    }
  }

  readonly property Component screenWindow: Component {
    Window {
      id: screenWindow

      readonly property bool hostsScene: root.screenManager
        && root.screenManager.currentWindow === screenWindow

      width: 640
      height: 480
      title: "GXDM Greeter"
      color: "#101010"

      Rectangle {
        parent: screenWindow.Overlay.overlay
        anchors.fill: parent
        z: 100000
        color: "black"
        opacity: root.preserveBackground ? 0 : root.transitionProgress
        visible: root.transitioning && !root.preserveBackground

        MouseArea {
          anchors.fill: parent
        }
      }

      GreeterBackground {
        anchors.fill: parent
        visible: !screenWindow.hostsScene
        source: root.backgroundUri
        progress: root.loginProgress
        activeScale: root.activeBackgroundScale
        activeBlur: root.activeBackgroundBlur
      }
    }
  }

  Behavior on dialogProgress {
    NumberAnimation {
      duration: 180
      easing.type: Easing.OutCubic
    }
  }

  Behavior on loginProgress {
    NumberAnimation {
      duration: 250
      easing.type: Easing.Linear
    }
  }

  function dateLabel(value) {
    return value.getFullYear() + "年"
      + (value.getMonth() + 1) + "月"
      + value.getDate() + "日 "
      + weekdays[value.getDay()]
  }

  function openPasswordDialog(index, user) {
    if (passwordCard.busy || loginSucceeded) return
    passwordCard.askUserName = false
    passwordCard.userIndex = index
    passwordCard.user = user
    passwordCard.userName = userSelecter.fullNameFor(user)
    passwordCard.userAvatar = userSelecter.avatarFor(user)
    passwordCard.open()
  }

  function openManualLoginDialog() {
    if (passwordCard.busy || loginSucceeded) return
    passwordCard.askUserName = true
    passwordCard.userIndex = -1
    passwordCard.user = null
    passwordCard.userName = ""
    passwordCard.userAvatar = ""
    passwordCard.open()
  }

  function showPasswordError(message) {
    passwordCard.showError(message || qsTr("鉴权失败"))
    authFailureGlow.flash()
  }

  // 只有一个用户时没有可挑选的对象，默认选中该用户：退出时间窗口后再次
  // 回车或点击头像即可展开登录框，同时保留“未列出？”入口给列表外的账户。
  function selectSingleUser() {
    if (userSelecter.modelCount() === 1) {
      userSelecter.currentIndex = 0
    }
  }

  function requestLogin() {
    if (root.transitioning || root.loginSucceeded) return
    clockView.positionedAtTop = true
    root.loginRequested()
    root.selectSingleUser()
  }

  Timer {
    interval: 1000
    running: true
    repeat: true
    onTriggered: root.now = new Date()
  }

  Item {
    id: scene

    anchors.fill: parent

    Popup {
      id: powerErrorPopup
      opacity: root.preserveBackground ? 1 - root.transitionProgress : 1
      enabled: !root.transitioning
      parent: scene
      anchors.centerIn: parent
      width: Math.min(420, scene.width - 40)
      padding: 20
      modal: true
      focus: true
      closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
      background: DtkFrame { backdrop: windowContent }
      contentItem: Column {
        spacing: 16
        Label {
          id: powerError
          width: parent.width
          wrapMode: Text.Wrap
          color: "white"
        }
        Button {
          text: qsTr("关闭")
          anchors.right: parent.right
          onClicked: powerErrorPopup.close()
        }
      }
    }

    Item {
      id: windowContent
      enabled: !root.transitioning

      anchors.fill: parent

      GreeterBackground {
        anchors.fill: parent
        source: root.backgroundUri
        progress: root.loginProgress
        activeScale: root.activeBackgroundScale
        activeBlur: root.activeBackgroundBlur
      }

      Item {
        id: foreground
        opacity: root.preserveBackground ? 1 - root.transitionProgress : 1

        anchors.fill: parent

        NumberAnimation on opacity {
          id: foregroundFadeIn

          running: false
          from: 0.0
          to: 1.0
          duration: 150
          easing.type: Easing.OutCubic
        }

        Rectangle {
          anchors.fill: parent
          color: "transparent"
          gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 0.55; color: "#00000000" }
            GradientStop { position: 1.0; color: "#36000000" }
          }
        }

        Item {
          id: sceneContent

          anchors.fill: parent
          opacity: 1.0 - 0.7 * root.dialogProgress
          layer.enabled: root.dialogProgress > 0
          layer.effect: MultiEffect {
            blurEnabled: true
            blurMax: 32
            blur: 0.8 * root.dialogProgress
            autoPaddingEnabled: false
          }

          Item {
            id: clockView

            property bool positionedAtTop: false

            x: (parent.width - width) / 2
            y: positionedAtTop ? 72 : (parent.height - height) / 2
            width: Math.max(timeLabel.width, promptLabel.width)
            height: timeLabel.inkHeight + 8 + promptLabel.inkHeight

            Behavior on y {
              NumberAnimation {
                duration: 250
                easing.type: Easing.OutExpo
              }
            }

            ShadowText {
              id: timeLabel

              anchors.horizontalCenter: parent.horizontalCenter
              y: -inkTop
              text: Qt.formatTime(root.now, "hh:mm")
              pixelSize: 48
              weight: Font.Black
              shadowOpacity: 0.24
            }

            ShadowText {
              id: promptLabel

              anchors.horizontalCenter: parent.horizontalCenter
              y: timeLabel.inkHeight + 8 - inkTop
              text: dateLabel(root.now)
              pixelSize: 16
              weight: Font.Light
              shadowOpacity: 0.22
            }
          }

          UserSelecter {
            id: userSelecter

            anchors.centerIn: parent
            anchors.verticalCenterOffset: -Math.min(root.keyboardInset / 2,
              Math.max(0, (parent.height - height) / 2
                - (clockView.y + clockView.height + 24)))
            width: Math.min(implicitWidth, Math.max(0, parent.width - 48))
            model: root.userModel
            foregroundColor: "white"
            visible: clockView.positionedAtTop || root.loginProgress > 0
            enabled: clockView.positionedAtTop
            opacity: root.loginProgress
            revealed: clockView.positionedAtTop
            onUserConfirmed: (index, user) => {
              root.userSelected(index, user)
              root.openPasswordDialog(index, user)
            }
            onNotListedRequested: {
              root.manualLoginRequested()
              root.openManualLoginDialog()
            }
          }
        }

        Rectangle {
          id: dialogScrim

          anchors.fill: parent
          color: "#33000000"
          opacity: root.dialogProgress
          visible: opacity > 0

          MouseArea {
            anchors.fill: parent
            enabled: passwordCard.shown && !passwordCard.busy
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            onPressed: passwordCard.close()
            onWheel: (wheel) => wheel.accepted = true
          }
        }

        Item {
          id: gxdeInfoView

          property bool meetsAdditionalVisibilityConditions: true
          property real revealProgress: clockView.positionedAtTop ? 1.0 : 0.0
          readonly property real bottomMargin: 12
          readonly property real hiddenY: parent.height - height / 2
          readonly property real shownY: parent.height - height - bottomMargin
          readonly property real logoRightInkPadding: gxdeLogo.width * 5 / 150
          readonly property real versionLeftMargin: 5 - logoRightInkPadding
          readonly property rect versionInkBounds:
            versionMetrics.tightBoundingRect(gxdeVersion.text)
          readonly property real versionInkTop:
            gxdeVersion.baselineOffset + versionInkBounds.y

          anchors.left: parent.left
          anchors.leftMargin: 12
          visible: root.isGxdeOs
            && root.gxdeReleaseLabel.length > 0
            && (clockView.positionedAtTop || revealProgress > 0)
            && meetsAdditionalVisibilityConditions
          opacity: revealProgress
          y: hiddenY + (shownY - hiddenY) * revealProgress - root.keyboardInset
          width: gxdeLogo.width + versionLeftMargin + gxdeVersion.implicitWidth
          height: gxdeLogo.height

          Behavior on revealProgress {
            NumberAnimation {
              duration: 140
              easing.type: Easing.OutCubic
            }
          }

          FontMetrics {
            id: versionMetrics
            font: gxdeVersion.font
          }

          Image {
            id: gxdeLogo
            anchors.bottom: parent.bottom
            anchors.left: parent.left

            width: Math.round(height * 150 / 35)
            height: 32
            source: "qrc:/icons/gxde.png"
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: false
          }

          Text {
            id: gxdeVersion
            anchors.left: gxdeLogo.right
            anchors.leftMargin: gxdeInfoView.versionLeftMargin
            y: -gxdeInfoView.versionInkTop
            text: root.gxdeReleaseLabel
            color: "#d0d0d0"
            font.family: "Source Han Sans"
            font.pixelSize: 10
            font.weight: Font.Light
            renderType: Text.NativeRendering
          }
        }

        Row {
          id: operationView

          property real revealProgress: clockView.positionedAtTop ? 1.0 : 0.0
          readonly property real bottomMargin: 12
          readonly property real hiddenY: parent.height - height / 2
          readonly property real shownY: parent.height - height - bottomMargin

          anchors.right: parent.right
          anchors.rightMargin: 12
          visible: clockView.positionedAtTop || revealProgress > 0
          opacity: revealProgress
          y: hiddenY + (shownY - hiddenY) * revealProgress - root.keyboardInset
          spacing: 12

          Behavior on revealProgress {
            NumberAnimation {
              duration: 140
              easing.type: Easing.OutCubic
            }
          }

          IconButton {
            id: sessionButton

            objectName: "sessionButton"
            source: sessionMenu.iconFor(root.selectedSession)
            tint: "transparent"
            visible: root.sessionModel.length > 0
            Accessible.name: qsTr("选择桌面会话：")
              + (root.selectedSession ? root.selectedSession.name : "")
            onClicked: {
              powerMenu.close()
              sessionMenu.opened ? sessionMenu.close() : sessionMenu.open()
            }
          }

          IconButton {
            source: "qrc:/icons/keyboard.svg"
            tint: root.iconTint
            visible: keyboardLoader.status === Loader.Ready
            onClicked: root.virtualKeyboardRequested
              = !root.virtualKeyboardRequested
          }

          IconButton {
            id: powerButton

            source: "qrc:/icons/power.svg"
            tint: root.iconTint
            onClicked: {
              sessionMenu.close()
              powerMenu.opened ? powerMenu.close() : powerMenu.open()
            }
          }
        }
      }
    }

    SessionMenu {
      id: sessionMenu
      opacity: root.preserveBackground ? 1 - root.transitionProgress : 1
      enabled: !root.transitioning

      readonly property point anchor: {
        operationView.x
        operationView.y
        return sessionButton.mapToItem(parent, sessionButton.width, 0)
      }

      objectName: "sessionMenu"
      parent: scene
      z: 20
      width: Math.min(implicitWidth, scene.width - 24)
      maximumHeight: Math.max(46, Math.min(320, anchor.y - 20))
      x: Math.max(12, Math.round(anchor.x - width))
      y: Math.round(anchor.y - height - 8)
      backdrop: windowContent
      sessions: root.sessionModel
      selectedIndex: root.selectedSessionIndex
      onSelected: (index) => {
        root.sessionChosen = true
        root.selectedSessionIndex = index
        root.sessionSelected(root.selectedSession)
      }
      onClosed: {
        if (passwordCard.opened) {
          passwordCard.forceActiveFocus()
        } else {
          keyCatcher.forceActiveFocus()
        }
      }
    }

    PowerMenu {
      id: powerMenu
      opacity: root.preserveBackground ? 1 - root.transitionProgress : 1
      enabled: !root.transitioning

      readonly property point anchor: {
        operationView.x
        operationView.y
        return powerButton.mapToItem(parent, powerButton.width, 0)
      }

      parent: scene
      z: 20
      x: Math.round(anchor.x - width)
      y: Math.round(anchor.y - height - 8)
      backdrop: windowContent
      availableActions: root.availablePowerActions
      originCorner: Item.BottomRight
      onTriggered: (action) => root.powerRequested(action)
      onClosed: {
        if (passwordCard.opened) {
          passwordCard.forceActiveFocus()
        } else {
          keyCatcher.forceActiveFocus()
        }
      }
    }

    Item {
      id: keyCatcher

      anchors.fill: parent
      focus: true
      enabled: !root.transitioning

      Component.onCompleted: forceActiveFocus()

      Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) {
          clockView.positionedAtTop = false
          root.virtualKeyboardRequested = false
        } else if (clockView.positionedAtTop
            && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
          userSelecter.confirmUser(userSelecter.currentIndex)
        } else {
          root.requestLogin()
        }
        event.accepted = true
      }
    }

    TapHandler {
      acceptedButtons: Qt.AllButtons
      onTapped: root.requestLogin()
    }

    PasswordCard {
      id: passwordCard
      objectName: "passwordCard"
      opacity: root.preserveBackground ? 1 - root.transitionProgress : 1
      enabled: !root.transitioning

      property int userIndex: -1
      property var user: null
      fingerprintListening: root.fingerprintListening

      parent: scene
      x: Math.round((parent.width - width) / 2 + shakeOffset)
      y: Math.round((parent.height - root.keyboardInset - height) / 2)
      backdrop: windowContent
      onSubmitted: (userName, password) => {
        root.passwordSubmitted(userIndex, askUserName
          ? { "username": userName, "fullName": userName } : user, password)
        busy = true
        if (!root.greeterBackend) {
          root.showPasswordError(qsTr("登录服务不可用。"))
          return
        }
        root.greeterBackend.Login(askUserName ? userName : user.username,
          password, root.selectedSession ? root.selectedSession.id : "")
      }
      onClosed: {
        keyCatcher.forceActiveFocus()
      }
    }

    Popup {
      id: virtualKeyboard
      opacity: root.preserveBackground ? 1 - root.transitionProgress : 1
      enabled: !root.transitioning

      property real slide: 0

      readonly property real screenMargin: 12

      parent: scene
      z: 10
      width: Math.min(parent.width - 24, 960)
      x: Math.round((parent.width - width) / 2)
      y: parent.height - (height + screenMargin) * slide
      padding: 8
      modal: false
      dim: false
      focus: false
      closePolicy: Popup.NoAutoClose
      visible: root.virtualKeyboardRequested && clockView.positionedAtTop
        && keyboardLoader.status === Loader.Ready

      background: DtkFrame {
        backdrop: windowContent
        trackedGeometry: [virtualKeyboard.x, virtualKeyboard.y,
          virtualKeyboard.visible]
      }

      enter: Transition {
        NumberAnimation {
          property: "slide"
          from: 0.0
          to: 1.0
          duration: 220
          easing.type: Easing.OutCubic
        }
      }

      exit: Transition {
        NumberAnimation {
          property: "slide"
          to: 0.0
          duration: 180
          easing.type: Easing.InCubic
        }
      }

      contentItem: Item {
        implicitHeight: keyboardLoader.item ? keyboardLoader.item.height : 0

        Loader {
          id: keyboardLoader

          width: parent.width
          asynchronous: true
          source: "VirtualKeyboardPanel.qml"
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

      parent: scene.Overlay.overlay
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

  component GreeterBackground: Item {
    id: greeterBackground

    property url source
    property real progress: 0
    property real activeScale: 1.0
    property real activeBlur: 0.0

    scale: 1.0 + (activeScale - 1.0) * progress

    Item {
      id: backgroundSource

      anchors.fill: parent

      Image {
        anchors.fill: parent
        source: greeterBackground.source
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        smooth: true
        visible: greeterBackground.source.toString() !== ""
      }

      Rectangle {
        anchors.fill: parent
        visible: greeterBackground.source.toString() === ""
        gradient: Gradient {
          GradientStop { position: 0; color: "#e9def8" }
          GradientStop { position: 1; color: "#cfdff2" }
        }
      }
    }

    MultiEffect {
      anchors.fill: backgroundSource
      source: backgroundSource
      visible: greeterBackground.progress > 0
      blurEnabled: true
      blurMax: 32
      blur: greeterBackground.activeBlur * greeterBackground.progress
      autoPaddingEnabled: false
    }
  }

  component ShadowText: Item {
    id: shadowText

    property alias text: label.text
    property int pixelSize: 24
    property int weight: Font.Normal
    property real shadowOpacity: 0.25
    readonly property rect inkBounds: textMetrics.tightBoundingRect(label.text)
    readonly property real inkTop: label.baselineOffset + inkBounds.y
    readonly property real inkHeight: inkBounds.height

    implicitWidth: label.implicitWidth + 4
    implicitHeight: label.implicitHeight + 4
    width: implicitWidth
    height: implicitHeight

    FontMetrics {
      id: textMetrics
      font: label.font
    }

    Text {
      x: 2
      y: 2
      text: label.text
      color: Qt.rgba(0, 0, 0, shadowText.shadowOpacity)
      font.family: "Source Han Sans"
      font.pixelSize: shadowText.pixelSize
      font.weight: shadowText.weight
      renderType: Text.NativeRendering
    }

    Text {
      id: label
      color: "white"
      font.family: "Source Han Sans"
      font.pixelSize: shadowText.pixelSize
      font.weight: shadowText.weight
      renderType: Text.NativeRendering
    }
  }

  component IconButton: ToolButton {
    id: iconButton

    property url source
    property color tint: "white"

    width: 32
    height: 32
    padding: 4
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    display: AbstractButton.IconOnly
    icon.source: iconButton.source
    icon.width: 24
    icon.height: 24
    icon.color: iconButton.tint
    opacity: hovered ? 1.0 : 0.86

    background: Item {
    }

    HoverHandler {
      cursorShape: Qt.PointingHandCursor
    }
  }
}
