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
  id: clock

  property date now: new Date()
  property real referenceHeight: 720
  property real compact: 0
  readonly property real timeInkTop: time.inkTop

  function dateText(when) {
    const locale = Qt.locale()
    if (locale.name.startsWith("zh") || locale.name.startsWith("ja")) {
      return Qt.formatDate(when, "M月d日") + " "
        + locale.dayName(when.getDay(), Locale.LongFormat)
    }
    return locale.dayName(when.getDay(), Locale.LongFormat) + ", "
      + locale.monthName(when.getMonth(), Locale.LongFormat) + " "
      + when.getDate()
  }

  implicitWidth: Math.max(time.width * time.scale, date.width)
  implicitHeight: date.y + date.height
  width: implicitWidth
  height: implicitHeight

  Timer {
    interval: 1000
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: clock.now = new Date()
  }

  ShadowText {
    id: time

    anchors.horizontalCenter: parent.horizontalCenter
    text: Qt.formatTime(clock.now, "H:mm")
    pixelSize: Math.max(64, Math.min(144, Math.round(clock.referenceHeight * 0.13)))
    weight: Font.DemiBold
    shadowOpacity: 0.24
    shadowOffset: 3
    transformOrigin: Item.Top
    scale: 1.0 - 0.4 * clock.compact
  }

  ShadowText {
    id: date

    anchors.horizontalCenter: parent.horizontalCenter
    y: Math.round((time.inkBottom + time.pixelSize * 0.18) * time.scale - inkTop)
    text: clock.dateText(clock.now)
    pixelSize: Math.max(16, Math.min(28, Math.round(time.pixelSize * 0.2)))
    weight: Font.Medium
    shadowOpacity: 0.22
  }
}
