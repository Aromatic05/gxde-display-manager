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

Popup {
  id: switcher

  property var backend: null

  width: Math.min(parent ? parent.width - 96 : 640, 720)
  height: 236
  padding: 0
  modal: true
  focus: true
  closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
  x: parent ? Math.round((parent.width - width) / 2) : 0
  y: parent ? Math.round((parent.height - height) / 2) : 0

  onOpened: list.forceActiveFocus()

  enter: Transition {
    ParallelAnimation {
      NumberAnimation {
        property: "opacity"
        from: 0.0
        to: 1.0
        duration: 120
        easing.type: Easing.OutCubic
      }
      NumberAnimation {
        property: "scale"
        from: 0.96
        to: 1.0
        duration: 120
        easing.type: Easing.OutCubic
      }
    }
  }

  exit: Transition {
    NumberAnimation {
      property: "opacity"
      to: 0.0
      duration: 90
      easing.type: Easing.OutCubic
    }
  }

  background: Rectangle {
    radius: 8
    color: "#e6141a22"
    border.width: 1
    border.color: "#2effffff"
  }

  contentItem: Column {
    width: switcher.width
    topPadding: 16
    bottomPadding: 12

    Text {
      width: parent.width
      leftPadding: 22
      text: qsTr("选择用户")
      color: "#f2f6ff"
      font.pixelSize: 15
      font.weight: Font.Medium
      renderType: Text.NativeRendering
    }

    Item {
      width: 1
      height: 10
    }

    ListView {
      id: list

      width: parent.width
      height: 156
      orientation: ListView.Horizontal
      spacing: 10
      clip: true
      boundsBehavior: Flickable.StopAtBounds
      model: switcher.backend ? switcher.backend.users : null
      focus: true

      Keys.onEscapePressed: switcher.close()

      delegate: Item {
        id: userEntry

        required property int index
        required property string username
        required property string displayName
        required property string avatar
        required property bool isCurrent
        required property bool locked
        required property int lockMinutes

        width: 96
        height: list.height

        Rectangle {
          anchors.fill: parent
          radius: 6
          color: pointer.containsMouse || userEntry.isCurrent
            ? "#24ffffff" : "#12ffffff"
          border.width: 1
          border.color: userEntry.isCurrent ? "#66ffffff" : "#14ffffff"

          Behavior on color {
            ColorAnimation { duration: 120 }
          }
        }

        UserAvatar {
          anchors.horizontalCenter: parent.horizontalCenter
          y: 12
          width: 60
          height: 60
          radius: 15
          source: userEntry.avatar
          name: userEntry.displayName || userEntry.username
        }

        Text {
          anchors.horizontalCenter: parent.horizontalCenter
          anchors.top: parent.top
          anchors.topMargin: 80
          width: parent.width - 10
          text: userEntry.displayName || userEntry.username
          color: "#f2f6ff"
          font.pixelSize: 12
          horizontalAlignment: Text.AlignHCenter
          elide: Text.ElideRight
          renderType: Text.NativeRendering
        }

        Text {
          anchors.horizontalCenter: parent.horizontalCenter
          anchors.top: parent.top
          anchors.topMargin: 101
          text: userEntry.locked
            ? qsTr("已锁定 %1 分钟").arg(userEntry.lockMinutes)
            : (userEntry.isCurrent ? qsTr("当前用户") : "")
          color: userEntry.locked ? "#ff9a8a" : "#8fc9d5e8"
          font.pixelSize: 10
          renderType: Text.NativeRendering
        }

        MouseArea {
          id: pointer

          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: {
            if (!switcher.backend) return
            if (userEntry.isCurrent) {
              switcher.close()
              return
            }
            switcher.backend.SwitchUser(userEntry.index)
            switcher.close()
          }
        }
      }
    }
  }
}
