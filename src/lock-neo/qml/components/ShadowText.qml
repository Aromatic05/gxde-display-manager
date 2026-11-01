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

Item {
  id: shadowText

  property alias text: label.text
  property int pixelSize: 24
  property int weight: Font.Normal
  property real shadowOpacity: 0.25
  property real shadowOffset: 2
  readonly property alias font: label.font
  readonly property rect inkBounds: metrics.tightBoundingRect
  readonly property real inkTop: label.baselineOffset + inkBounds.y
  readonly property real inkBottom: inkTop + inkBounds.height

  implicitWidth: label.implicitWidth + shadowOffset * 2
  implicitHeight: label.implicitHeight + shadowOffset * 2
  width: implicitWidth
  height: implicitHeight

  TextMetrics {
    id: metrics
    font: label.font
    text: label.text
  }

  Text {
    x: shadowText.shadowOffset
    y: shadowText.shadowOffset
    text: label.text
    color: Qt.rgba(0, 0, 0, shadowText.shadowOpacity)
    font: label.font
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
