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
import QtQuick.Controls.Basic

Item {
  id: card

  property var backend: null
  property bool testMode: false
  property string errorText: ""
  property string statusText: ""
  readonly property color warningColor: "#be543c"
  readonly property bool warning: errorText.length > 0
    || !!(backend && backend.lockedOut)
  readonly property bool fingerprintListening:
    !!(backend && backend.fingerprintListening)

  // The last rejection already shook the card; say why the reader stopped.
  Connections {
    target: card.backend
    ignoreUnknownSignals: true
    function onFingerprintChanged() {
      if (card.backend.fingerprintGaveUp)
        card.errorText = qsTr("指纹多次不匹配，请输入密码")
    }
  }

  signal requestQuit()
  signal errorShown()

  implicitWidth: 380
  implicitHeight: content.implicitHeight + 48
  width: Math.max(260, Math.min(implicitWidth,
    parent ? parent.width - 64 : implicitWidth))
  height: implicitHeight
  enabled: backend !== null

  transform: Translate { id: shakeTranslate }

  function powerLabel(action) {
    switch (action) {
    case "shutdown": return qsTr("关机")
    case "reboot": return qsTr("重启")
    case "suspend": return qsTr("待机")
    case "hibernate": return qsTr("休眠")
    default: return action
    }
  }

  function focusPassword() {
    if (!visible || !enabled) return
    if (backend && backend.lockedOut) return
    passwordField.forceActiveFocus()
  }

  function submit() {
    if (!backend || backend.authenticating || backend.lockedOut) return
    if (passwordField.text.length === 0) {
      showError(qsTr("请输入密码"))
      return
    }
    errorText = ""
    statusText = ""
    backend.Authenticate(passwordField.text)
  }

  function showError(message) {
    errorText = String(message || qsTr("鉴权失败"))
    statusText = ""
    passwordField.selectAll()
    focusPassword()
    shake.restart()
    card.errorShown()
  }

  function showSuccess() {
    errorText = ""
    statusText = qsTr("认证成功")
    passwordField.text = ""
  }

  function showPowerProgress(action) {
    errorText = ""
    statusText = qsTr("正在%1…").arg(powerLabel(action))
    passwordField.text = ""
  }

  function clearPassword() {
    passwordField.text = ""
    errorText = ""
    statusText = ""
  }

  onVisibleChanged: {
    if (visible) focusPassword()
  }

  Component.onCompleted: focusPassword()

  // Soft, translucent card. Keeping the background independent from the
  // wallpaper makes the password field readable on both light and dark images.
  Rectangle {
    anchors.fill: parent
    radius: 8
    color: "#b8121720"
    border.width: 1
    border.color: "#2effffff"
  }

  Column {
    id: content

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    anchors.margins: 24
    spacing: 0

    UserAvatar {
      anchors.horizontalCenter: parent.horizontalCenter
      width: 76
      height: 76
      radius: 14
      source: card.backend ? card.backend.currentUserAvatar : ""
      name: card.backend ? card.backend.currentUserDisplayName : ""
    }

    Text {
      width: parent.width
      topPadding: 12
      text: card.backend
        ? (card.backend.currentUserDisplayName || card.backend.currentUserName)
        : ""
      color: "#f5f8ff"
      font.pixelSize: 18
      font.weight: Font.Medium
      horizontalAlignment: Text.AlignHCenter
      elide: Text.ElideRight
      renderType: Text.NativeRendering
    }

    Text {
      width: parent.width
      topPadding: 2
      visible: card.testMode && card.statusText.length === 0
      text: qsTr("演示模式：任意密码解锁，Esc 返回")
      color: "#99c9d5e8"
      font.pixelSize: 11
      horizontalAlignment: Text.AlignHCenter
      renderType: Text.NativeRendering
    }

    Item {
      width: 1
      height: 16
    }

    // Power actions still require authentication; explain what the next
    // password submission will do and allow cancelling it.
    Rectangle {
      id: powerBanner

      width: parent.width
      height: visible ? 38 : 0
      visible: card.backend && card.backend.pendingPowerAction.length > 0
      radius: 6
      color: "#2b8a6a32"
      border.width: 1
      border.color: "#4dcaa25a"

      Text {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: card.backend
          ? qsTr("输入密码以%1").arg(card.powerLabel(
              card.backend.pendingPowerAction)) : ""
        color: "#ffe6b8"
        font.pixelSize: 12
        renderType: Text.NativeRendering
      }

      LockButton {
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 48
        height: 30
        text: qsTr("取消")
        onClicked: {
          if (card.backend) card.backend.CancelPowerAction()
          card.focusPassword()
        }
      }
    }

    Item {
      width: 1
      height: powerBanner.visible ? 10 : 0
    }

    Item {
      width: parent.width
      height: 44

      TextField {
        id: passwordField

        anchors.fill: parent
        rightPadding: 48
        leftPadding: 14
        verticalAlignment: TextInput.AlignVCenter
        placeholderText: card.backend && card.backend.lockedOut
          ? qsTr("已锁定")
          : card.fingerprintListening ? qsTr("密码或指纹") : qsTr("密码")
        placeholderTextColor: "#80ffffff"
        color: "#f5f8ff"
        selectionColor: "#0081ff"
        selectedTextColor: "#ffffff"
        font.pixelSize: 15
        echoMode: TextInput.Password
        passwordCharacter: "\u25CF"
        inputMethodHints: Qt.ImhSensitiveData
          | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        enabled: card.backend && !card.backend.authenticating
          && !card.backend.lockedOut
        onAccepted: card.submit()
        onTextEdited: {
          if (card.errorText.length > 0) card.errorText = ""
          if (card.statusText.length > 0) card.statusText = ""
        }

        background: Rectangle {
          radius: 6
          color: passwordField.activeFocus ? "#26ffffff" : "#1affffff"
          border.width: 1
          border.color: card.errorText.length > 0 ? card.warningColor
            : passwordField.activeFocus ? "#66ffffff" : "#1fffffff"

          Behavior on border.color {
            ColorAnimation { duration: 160; easing.type: Easing.OutCubic }
          }
        }
      }

      LockButton {
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 36
        height: 36
        radius: 4
        iconSource: {
          const action = card.backend ? card.backend.pendingPowerAction : ""
          if (action === "reboot") return "qrc:/lock/icons/action-reboot.svg"
          if (action === "shutdown") return "qrc:/lock/icons/action-shutdown.svg"
          if (action === "suspend") return "qrc:/lock/icons/power-suspend.svg"
          if (action === "hibernate") return "qrc:/lock/icons/power-hibernate.svg"
          return "qrc:/lock/icons/lock.svg"
        }
        enabled: card.backend && !card.backend.authenticating
          && !card.backend.lockedOut
          && passwordField.text.length > 0
        onClicked: card.submit()
      }
    }

    Item {
      width: 1
      height: 6
    }

    Row {
      id: messageRow

      property string shownText: ""
      readonly property string currentText: {
        if (card.errorText.length > 0) return card.errorText
        if (card.backend && card.backend.lockedOut) {
          return qsTr("尝试次数过多，请在 %1 分钟后重试")
            .arg(card.backend.lockMinutes)
        }
        return card.statusText
      }
      readonly property bool active: currentText.length > 0
        || busyIndicator.visible

      onCurrentTextChanged: if (currentText.length > 0) shownText = currentText
      Component.onCompleted: shownText = currentText

      width: parent.width
      height: active ? Math.max(busyIndicator.visible ? 22 : 0,
                                message.implicitHeight) : 0
      clip: true
      spacing: 8

      Behavior on height {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
      }

      BusyIndicator {
        id: busyIndicator

        anchors.verticalCenter: parent.verticalCenter
        width: 22
        height: 22
        running: card.backend && card.backend.authenticating
        visible: running
      }

      Text {
        id: message

        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - (busyIndicator.visible ? 30 : 0)
        leftPadding: 4
        text: messageRow.shownText
        color: card.warning ? card.warningColor : "#b8d6c0"
        opacity: messageRow.currentText.length > 0 ? 1.0 : 0.0
        font.pixelSize: 12

        Behavior on opacity {
          NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }
        wrapMode: Text.Wrap
        renderType: Text.NativeRendering
      }
    }

    Item {
      width: 1
      height: 8
    }

    Text {
      width: parent.width
      text: qsTr("按 Enter 解锁")
      color: "#66c9d5e8"
      font.pixelSize: 11
      horizontalAlignment: Text.AlignHCenter
      renderType: Text.NativeRendering
    }
  }

  SequentialAnimation {
    id: shake

    NumberAnimation {
      target: shakeTranslate
      property: "x"
      to: -8
      duration: 50
      easing.type: Easing.OutQuad
    }
    NumberAnimation {
      target: shakeTranslate
      property: "x"
      to: 7
      duration: 80
      easing.type: Easing.InOutQuad
    }
    NumberAnimation {
      target: shakeTranslate
      property: "x"
      to: -4
      duration: 70
      easing.type: Easing.InOutQuad
    }
    NumberAnimation {
      target: shakeTranslate
      property: "x"
      to: 2
      duration: 60
      easing.type: Easing.InOutQuad
    }
    NumberAnimation {
      target: shakeTranslate
      property: "x"
      to: 0
      duration: 50
      easing.type: Easing.OutQuad
    }
  }

}
