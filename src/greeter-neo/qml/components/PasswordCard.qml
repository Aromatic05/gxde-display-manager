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

Popup {
  id: card

  property bool light: false
  property Item backdrop: null
  property bool askUserName: false
  property string userName: ""
  property url userAvatar
  property string errorText: ""
  property bool busy: false
  // 指纹也能登录时提示用户。
  property bool fingerprintListening: false
  property url submitIconSource: "qrc:/icons/next-indicator.svg"

  readonly property color highlightColor: light ? "#0081ff" : "#024cca"
  readonly property color highlightedTextColor: light ? "#ffffff" : "#f1f6ff"
  readonly property color textColor: light ? "#000000" : "#ffffff"
  readonly property color secondaryTextColor: light ? "#b2000000" : "#b2ffffff"
  readonly property color warningColor: "#be543c"
  readonly property color fieldColor: light ? "#14000000" : "#14ffffff"
  readonly property int frameRadius: 8
  readonly property int itemRadius: 6
  property bool shown: false
  property bool moving: false
  property real shakeOffset: 0
  signal submitted(string userName, string password)

  function clearPassword() {
    passwordField.clear()
  }

  function showError(message) {
    card.errorText = message
    card.busy = false
    passwordField.selectAll()
    passwordField.forceActiveFocus()
    shakeAnimation.restart()
  }

  function enteredUserName() {
    return card.askUserName ? userNameField.text.trim() : card.userName
  }

  function canSubmit() {
    return !card.busy && passwordField.text.length > 0
      && enteredUserName().length > 0
  }

  function submit() {
    if (!canSubmit()) {
      return
    }
    card.errorText = ""
    card.submitted(enteredUserName(), passwordField.text)
  }

  width: 300
  padding: 16
  topPadding: 20
  modal: false
  focus: true
  margins: 12
  transformOrigin: Popup.Center
  closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape

  onAboutToShow: {
    card.moving = card.shown
    if (card.moving) return
    card.shown = true
    userNameField.clear()
    passwordField.clear()
    card.errorText = ""
    card.busy = false
  }
  onAboutToHide: card.shown = false
  onOpened: {
    if (card.moving) {
      card.moving = false
    } else if (card.askUserName) {
      userNameField.forceActiveFocus()
    } else {
      passwordField.forceActiveFocus()
    }
  }


  enter: Transition {
    ParallelAnimation {
      NumberAnimation {
        property: "opacity"
        from: 0.0
        to: 1.0
        duration: 180
        easing.type: Easing.OutCubic
      }
      NumberAnimation {
        property: "scale"
        from: 0.92
        to: 1.0
        duration: 180
        easing.type: Easing.OutCubic
      }
    }
  }

  exit: Transition {
    ParallelAnimation {
      NumberAnimation {
        property: "opacity"
        to: 0.0
        duration: 120
        easing.type: Easing.OutCubic
      }
      NumberAnimation {
        property: "scale"
        to: 0.96
        duration: 120
        easing.type: Easing.OutCubic
      }
    }
  }

  background: DtkFrame {
    light: card.light
    backdrop: card.backdrop
    radius: card.frameRadius
    trackedGeometry: [card.x, card.y, card.scale, card.opened]
  }

  contentItem: Column {
    spacing: 0

    UserAvatar {
      anchors.horizontalCenter: parent.horizontalCenter
      width: 64
      height: 64
      radius: 13
      visible: !card.askUserName
      source: card.userAvatar
      name: card.userName
    }

    Text {
      width: parent.width
      topPadding: card.askUserName ? 0 : 10
      bottomPadding: 16
      text: card.askUserName ? qsTr("其他用户") : card.userName
      color: card.textColor
      font.pixelSize: 15
      font.weight: Font.Medium
      horizontalAlignment: Text.AlignHCenter
      elide: Text.ElideRight
      renderType: Text.NativeRendering
    }

    CardField {
      id: userNameField

      visible: card.askUserName
      placeholderText: qsTr("用户名")
      inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
      onAccepted: {
        if (passwordField.text.length > 0) {
          card.submit()
        } else {
          passwordField.forceActiveFocus()
        }
      }
    }

    Item {
      width: 1
      height: 8
      visible: card.askUserName
    }

    CardField {
      id: passwordField

      rightPadding: submitButton.width + 8
      echoMode: TextInput.Password
      passwordCharacter: "\u2022"
      placeholderText: card.fingerprintListening ? qsTr("密码或指纹")
                                                 : qsTr("密码")
      inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
      showError: card.errorText.length > 0
      onAccepted: card.submit()

      Button {
        id: submitButton

        readonly property bool ready: card.canSubmit()

        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 32
        height: 28
        padding: 0
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        enabled: ready
        display: AbstractButton.IconOnly
        icon.source: card.submitIconSource
        icon.width: 16
        icon.height: 16
        icon.color: ready ? card.textColor : card.secondaryTextColor
        onClicked: card.submit()

        background: Rectangle {
          radius: 4
          color: !submitButton.ready ? (card.light ? "#08000000" : "#08ffffff")
            : submitButton.down ? (card.light ? "#0a000000" : "#0affffff")
            : submitButton.hovered ? (card.light ? "#1f000000" : "#1fffffff")
            : (card.light ? "#14000000" : "#14ffffff")

          Behavior on color {
            ColorAnimation {
              duration: 120
            }
          }
        }

        HoverHandler {
          enabled: submitButton.ready
          cursorShape: Qt.PointingHandCursor
        }
      }
    }

    Item {
      id: errorRow

      property string message: ""
      readonly property bool active: card.errorText.length > 0

      width: parent.width
      height: active ? errorLabel.implicitHeight : 0
      clip: true

      Connections {
        target: card
        function onErrorTextChanged() {
          if (card.errorText.length > 0) {
            errorRow.message = card.errorText
          }
        }
      }

      Behavior on height {
        NumberAnimation {
          duration: 180
          easing.type: Easing.OutCubic
        }
      }

      Text {
        id: errorLabel

        width: parent.width
        topPadding: 6
        leftPadding: 4
        text: errorRow.message
        color: card.warningColor
        opacity: errorRow.active ? 1.0 : 0.0
        font.pixelSize: 12
        wrapMode: Text.Wrap
        renderType: Text.NativeRendering

        Behavior on opacity {
          NumberAnimation {
            duration: 180
            easing.type: Easing.OutCubic
          }
        }
      }
    }
  }

  component CardField: TextField {
    id: field

    property bool showError: false

    width: parent ? parent.width : 0
    height: 36
    leftPadding: 10
    rightPadding: 10
    verticalAlignment: TextInput.AlignVCenter
    placeholderTextColor: card.secondaryTextColor
    color: card.textColor
    selectionColor: card.highlightColor
    selectedTextColor: card.highlightedTextColor
    font.pixelSize: 14
    enabled: !card.busy
    onTextEdited: card.errorText = ""

    background: Rectangle {
      radius: card.itemRadius
      color: card.fieldColor
      border.width: 1
      border.color: field.showError ? card.warningColor
        : field.activeFocus ? (card.light ? "#66000000" : "#99ffffff")
        : (card.light ? "#00000000" : "#00ffffff")

      Behavior on border.color {
        ColorAnimation {
          duration: 160
          easing.type: Easing.OutCubic
        }
      }
    }
  }

  SequentialAnimation {
    id: shakeAnimation

    NumberAnimation {
      target: card
      property: "shakeOffset"
      to: -8
      duration: 50
      easing.type: Easing.OutQuad
    }
    NumberAnimation {
      target: card
      property: "shakeOffset"
      to: 7
      duration: 80
      easing.type: Easing.InOutQuad
    }
    NumberAnimation {
      target: card
      property: "shakeOffset"
      to: -4
      duration: 70
      easing.type: Easing.InOutQuad
    }
    NumberAnimation {
      target: card
      property: "shakeOffset"
      to: 2
      duration: 60
      easing.type: Easing.InOutQuad
    }
    NumberAnimation {
      target: card
      property: "shakeOffset"
      to: 0
      duration: 50
      easing.type: Easing.OutQuad
    }
  }
}
