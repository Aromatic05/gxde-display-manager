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
  id: menu

  property var backend: null
  signal triggered(string action)

  readonly property var allActions: [
    { action: "reboot", label: qsTr("重启"), icon: "qrc:/lock/icons/power-reboot.svg" },
    { action: "shutdown", label: qsTr("关机"), icon: "qrc:/lock/icons/power-shutdown.svg" },
    { action: "suspend", label: qsTr("待机"), icon: "qrc:/lock/icons/power-suspend.svg" },
    { action: "hibernate", label: qsTr("休眠"), icon: "qrc:/lock/icons/power-hibernate.svg" }
  ]

  readonly property var actions: {
    if (!backend) return []
    const available = backend.availableActions || []
    return allActions.filter(function(entry) {
      return available.indexOf(entry.action) !== -1
    })
  }

  width: 188
  padding: 0
  modal: false
  focus: true
  closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
  x: parent ? parent.width - width - 28 : 0
  y: parent ? parent.height - height - 78 : 0

  onOpened: forceActiveFocus()

  enter: Transition {
    ParallelAnimation {
      NumberAnimation {
        property: "opacity"
        from: 0.0
        to: 1.0
        duration: 100
        easing.type: Easing.OutCubic
      }
      NumberAnimation {
        property: "scale"
        from: 0.95
        to: 1.0
        duration: 100
        easing.type: Easing.OutCubic
      }
    }
  }

  exit: Transition {
    NumberAnimation {
      property: "opacity"
      to: 0.0
      duration: 80
      easing.type: Easing.OutCubic
    }
  }

  background: Rectangle {
    radius: 8
    color: "#d9141a22"
    border.width: 1
    border.color: "#2effffff"
  }

  contentItem: Column {
    width: menu.width
    topPadding: 8
    bottomPadding: 8

    Text {
      width: parent.width
      leftPadding: 14
      bottomPadding: 6
      text: qsTr("电源")
      color: "#8fc9d5e8"
      font.pixelSize: 11
      renderType: Text.NativeRendering
    }

    Repeater {
      model: menu.actions

      delegate: Item {
        id: entry

        required property var modelData
        required property int index

        width: menu.width
        height: 38

        Rectangle {
          anchors.fill: parent
          anchors.leftMargin: 6
          anchors.rightMargin: 6
          radius: 6
          color: pointer.containsMouse ? "#2bffffff" : "transparent"

          Behavior on color {
            ColorAnimation { duration: 100 }
          }
        }

        Image {
          anchors.left: parent.left
          anchors.leftMargin: 12
          anchors.verticalCenter: parent.verticalCenter
          width: 24
          height: 24
          source: entry.modelData.icon
          sourceSize: Qt.size(48, 48)
          opacity: 0.9
        }

        Text {
          anchors.left: parent.left
          anchors.leftMargin: 46
          anchors.verticalCenter: parent.verticalCenter
          text: entry.modelData.label
          color: "#f2f6ff"
          font.pixelSize: 14
          renderType: Text.NativeRendering
        }

        MouseArea {
          id: pointer

          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: {
            menu.close()
            menu.triggered(entry.modelData.action)
          }
        }
      }
    }
  }
}
