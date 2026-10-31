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

ToolButton {
  id: iconButton

  property url source
  property color tint: "white"
  property string label: ""
  property string accessibleName: label
  property int iconSize: 24

  implicitWidth: label.length > 0 ? Math.max(32, implicitContentWidth + 8) : 32
  implicitHeight: 32
  padding: 4
  focusPolicy: Qt.NoFocus
  hoverEnabled: true
  display: label.length > 0 ? AbstractButton.TextOnly : AbstractButton.IconOnly
  text: label
  font.pixelSize: 15
  font.weight: Font.DemiBold
  palette.buttonText: iconButton.tint
  icon.source: iconButton.source
  icon.width: iconButton.iconSize
  icon.height: iconButton.iconSize
  icon.color: iconButton.tint
  opacity: !enabled ? 0.35 : hovered ? 1.0 : 0.86
  Accessible.name: accessibleName

  background: Item {
  }

  HoverHandler {
    cursorShape: Qt.PointingHandCursor
  }
}
