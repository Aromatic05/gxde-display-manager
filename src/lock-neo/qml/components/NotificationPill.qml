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

Rectangle {
  id: pill

  property int count: 0

  implicitWidth: row.implicitWidth + 32
  implicitHeight: 36
  radius: height / 2
  color: "#b8121720"
  border.width: 1
  border.color: "#2effffff"

  Row {
    id: row

    anchors.centerIn: parent
    spacing: 6

    Image {
      anchors.verticalCenter: parent.verticalCenter
      width: 16
      height: 16
      source: "qrc:/lock/icons/notification.svg"
      sourceSize: Qt.size(32, 32)
    }

    Text {
      anchors.verticalCenter: parent.verticalCenter
      text: qsTr("%1 条新通知").arg(pill.count)
      color: "#f5f8ff"
      font.pixelSize: 14
      renderType: Text.NativeRendering
    }
  }
}
