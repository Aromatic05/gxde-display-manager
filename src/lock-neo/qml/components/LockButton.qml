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

Rectangle {
  id: button

  property string text: ""
  property url iconSource
  property string tooltip: ""
  signal clicked()

  readonly property bool hovered: pointer.containsMouse

  implicitWidth: Math.max(38, label.implicitWidth + 22)
  implicitHeight: 38
  width: implicitWidth
  height: implicitHeight
  radius: height / 2
  color: pointer.pressed ? "#3dffffff"
    : hovered ? "#2bffffff" : "#1fffffff"
  border.width: 1
  border.color: hovered ? "#3dffffff" : "#14ffffff"
  opacity: enabled ? 1.0 : 0.35

  Behavior on color {
    ColorAnimation { duration: 120 }
  }

  Image {
    anchors.centerIn: parent
    visible: button.iconSource.toString().length > 0
    width: 20
    height: 20
    source: button.iconSource
    sourceSize: Qt.size(40, 40)
  }

  Text {
    id: label

    anchors.centerIn: parent
    visible: button.iconSource.toString().length === 0
    text: button.text
    color: "#f2f6ff"
    font.pixelSize: 15
    font.weight: Font.Medium
    renderType: Text.NativeRendering
  }

  MouseArea {
    id: pointer

    anchors.fill: parent
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor
    onClicked: button.clicked()
  }

  ToolTip.visible: button.tooltip.length > 0 && button.hovered
  ToolTip.text: button.tooltip
  ToolTip.delay: 600
}
