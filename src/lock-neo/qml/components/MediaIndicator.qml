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
  id: card

  property var media: null
  property var audio: null
  readonly property bool hasMedia: media !== null && media.available
  readonly property bool hasAudio: audio !== null && audio.available
  property bool volumeExpanded: false
  property bool tapped: false
  property bool hovered: false
  readonly property bool controlsShown: hasMedia
    && (hovered || tapped)
  readonly property bool volumeShown: hasAudio
    && (!hasMedia || (controlsShown && volumeExpanded))

  function collapse() {
    volumeExpanded = false
    tapped = false
  }

  onControlsShownChanged: if (!controlsShown) volumeExpanded = false

  HoverHandler {
    id: hoverHandler
    onHoveredChanged: {
      if (hovered) {
        leaveTimer.stop()
        card.hovered = true
      } else {
        leaveTimer.restart()
      }
    }
  }

  Timer {
    id: leaveTimer
    interval: 300
    onTriggered: card.hovered = false
  }
  readonly property real progress: hasMedia && media.length > 0
    ? Math.max(0, Math.min(1, media.position / media.length)) : 0

  function volumeIcon(audio) {
    if (!audio || audio.muted || audio.volume <= 0)
      return "qrc:/lock/icons/volume-mute.svg"
    if (audio.volume < 0.34) return "qrc:/lock/icons/volume-low.svg"
    if (audio.volume < 0.67) return "qrc:/lock/icons/volume-medium.svg"
    return "qrc:/lock/icons/volume-high.svg"
  }

  function formatTime(ms) {
    const total = Math.max(0, Math.floor(ms / 1000))
    const hours = Math.floor(total / 3600)
    const minutes = Math.floor(total / 60) % 60
    const seconds = String(total % 60).padStart(2, "0")
    return hours > 0
      ? hours + ":" + String(minutes).padStart(2, "0") + ":" + seconds
      : minutes + ":" + seconds
  }

  implicitWidth: 320
  implicitHeight: content.implicitHeight + 32
  radius: 8
  color: "#b8121720"
  border.width: 1
  border.color: "#2effffff"

  MouseArea {
    anchors.fill: parent
    acceptedButtons: Qt.AllButtons
    onClicked: card.tapped = true
  }

  component Track: Rectangle {
    property real value: 0

    height: 4
    radius: 2
    color: "#2effffff"

    Rectangle {
      width: parent.width * parent.value
      height: parent.height
      radius: parent.radius
      color: "#f5f8ff"
    }
  }

  component SmallText: Text {
    color: "#a6c9d5e8"
    font.pixelSize: 11
    font.features: { "tnum": 1 }
    renderType: Text.NativeRendering
  }

  Column {
    id: content

    x: 16
    y: 16
    width: parent.width - 32
    spacing: 12

    Row {
      width: parent.width
      spacing: 12
      visible: card.hasMedia

      Rectangle {
        id: cover

        width: 64
        height: 64
        radius: 6
        color: "#1affffff"
        clip: true

        Image {
          id: art

          anchors.fill: parent
          source: card.media ? card.media.artUrl : ""
          sourceSize: Qt.size(128, 128)
          fillMode: Image.PreserveAspectCrop
          asynchronous: true
          visible: status === Image.Ready
        }

        Text {
          anchors.centerIn: parent
          visible: !art.visible
          text: "♪"
          color: "#80ffffff"
          font.pixelSize: 26
          renderType: Text.NativeRendering
        }
      }

      Column {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - cover.width - parent.spacing
        spacing: 3

        Text {
          width: parent.width
          text: card.media && card.media.title.length > 0
            ? card.media.title : qsTr("未知曲目")
          color: "#f5f8ff"
          font.pixelSize: 14
          font.weight: Font.DemiBold
          wrapMode: Text.Wrap
          maximumLineCount: 2
          elide: Text.ElideRight
          lineHeight: 1.1
          renderType: Text.NativeRendering
        }

        Text {
          width: parent.width
          visible: text.length > 0
          text: card.media
            ? [card.media.artist, card.media.album]
                .filter(function(part) { return part.length > 0 })
                .join(" · ")
            : ""
          color: "#a6c9d5e8"
          font.pixelSize: 12
          elide: Text.ElideRight
          renderType: Text.NativeRendering
        }
      }
    }

    Item {
      id: progressRow

      readonly property real lineCenter: 8

      width: parent.width
      height: 12
      visible: card.hasMedia && card.media.length > 0

      SmallText {
        id: elapsed

        anchors.left: parent.left
        anchors.verticalCenter: parent.top
        anchors.verticalCenterOffset: progressRow.lineCenter
        width: 34
        text: card.formatTime(card.media ? card.media.position : 0)
      }

      Item {
        id: progressBar

        objectName: "mediaProgress"
        anchors.left: elapsed.right
        anchors.right: total.left
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.top
        anchors.verticalCenterOffset: progressRow.lineCenter
        height: 16

        Track {
          anchors.verticalCenter: parent.verticalCenter
          width: parent.width
          value: card.progress
        }

        Rectangle {
          x: parent.width * card.progress - width / 2
          anchors.verticalCenter: parent.verticalCenter
          width: 10
          height: 10
          radius: 5
          color: "#ffffff"
          visible: seekArea.enabled && seekArea.containsMouse
        }

        MouseArea {
          id: seekArea

          anchors.fill: parent
          enabled: card.media !== null && card.media.canSeek
          hoverEnabled: true
          cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
          onClicked: (mouse) => card.media.Seek(
            Math.max(0, Math.min(1, mouse.x / width)) * card.media.length)
        }
      }

      SmallText {
        id: total

        anchors.right: parent.right
        anchors.verticalCenter: parent.top
        anchors.verticalCenterOffset: progressRow.lineCenter
        width: 34
        horizontalAlignment: Text.AlignRight
        text: card.formatTime(card.media ? card.media.length : 0)
      }
    }

    Item {
      objectName: "mediaControls"
      width: parent.width
      height: card.controlsShown ? 40 : 0
      visible: height > 0
      opacity: card.controlsShown ? 1.0 : 0.0
      clip: true

      Behavior on height {
        NumberAnimation {
          duration: 140
          easing.type: Easing.OutCubic
        }
      }

      Behavior on opacity {
        NumberAnimation {
          duration: 140
          easing.type: Easing.OutCubic
        }
      }

      Row {
        anchors.centerIn: parent
        spacing: 24

        IconButton {
          anchors.verticalCenter: parent.verticalCenter
          objectName: "mediaPrevious"
          implicitWidth: 36
          implicitHeight: 36
          padding: 6
          iconSize: 24
          source: "qrc:/lock/icons/media-previous.svg"
          accessibleName: qsTr("上一首")
          enabled: card.media !== null && card.media.canGoPrevious
          onClicked: card.media.Previous()
        }

        IconButton {
          id: playPause

          anchors.verticalCenter: parent.verticalCenter
          objectName: "mediaPlayPause"
          implicitWidth: 38
          implicitHeight: 38
          iconSize: 24
          source: card.media && card.media.playing
            ? "qrc:/lock/icons/media-pause.svg"
            : "qrc:/lock/icons/media-play.svg"
          padding: 7
          tint: "#1b2029"
          accessibleName: card.media && card.media.playing
            ? qsTr("暂停") : qsTr("播放")
          enabled: card.media !== null && card.media.canPlayPause
          onClicked: card.media.PlayPause()

          background: Rectangle {
            radius: width / 2
            color: playPause.pressed ? "#c9d0db"
              : playPause.hovered ? "#ffffff" : "#e6ebf2"
          }
        }

        IconButton {
          anchors.verticalCenter: parent.verticalCenter
          objectName: "mediaNext"
          implicitWidth: 36
          implicitHeight: 36
          padding: 6
          iconSize: 24
          source: "qrc:/lock/icons/media-next.svg"
          accessibleName: qsTr("下一首")
          enabled: card.media !== null && card.media.canGoNext
          onClicked: card.media.Next()
        }
      }

      IconButton {
        anchors.right: parent.right
        anchors.rightMargin: -6
        anchors.verticalCenter: parent.verticalCenter
        objectName: "volumeToggle"
        visible: card.hasAudio
        implicitWidth: 36
        implicitHeight: 36
        padding: 9
        iconSize: 18
        source: card.volumeIcon(card.audio)
        accessibleName: qsTr("音量")
        opacity: card.volumeShown || hovered ? 1.0 : 0.6
        onClicked: {
          card.volumeExpanded = !card.volumeShown
        }
      }
    }

    Item {
      objectName: "volumeRow"
      width: parent.width
      height: card.volumeShown ? 24 : 0
      visible: height > 0
      opacity: card.volumeShown ? 1.0 : 0.0
      clip: true

      Behavior on height {
        NumberAnimation {
          duration: 140
          easing.type: Easing.OutCubic
        }
      }

      Behavior on opacity {
        NumberAnimation {
          duration: 140
          easing.type: Easing.OutCubic
        }
      }

      IconButton {
        id: muteButton

        anchors.left: parent.left
        anchors.leftMargin: -6
        anchors.verticalCenter: parent.verticalCenter
        objectName: "volumeMute"
        visible: !card.hasMedia
        implicitHeight: 24
        padding: 3
        iconSize: 18
        source: card.volumeIcon(card.audio)
        accessibleName: card.audio && card.audio.muted
          ? qsTr("取消静音") : qsTr("静音")
        onClicked: if (card.audio) card.audio.ToggleMute()
      }

      IconButton {
        id: volumeDown

        readonly property real current: card.audio && !card.audio.muted
          ? card.audio.volume : 0

        anchors.horizontalCenter: parent.left
        anchors.horizontalCenterOffset: (card.hasMedia ? 0 : 32) + 17
        anchors.verticalCenter: parent.verticalCenter
        objectName: "volumeDown"
        implicitWidth: 28
        implicitHeight: 24
        label: "\u2212"
        tint: "#c9d5e8"
        accessibleName: qsTr("减小音量")
        autoRepeat: true
        enabled: current > 0
        onClicked: card.audio.SetVolume(Math.max(0, current - 0.05))
      }

      IconButton {
        id: volumeUp

        anchors.horizontalCenter: parent.right
        anchors.horizontalCenterOffset: -17
        anchors.verticalCenter: parent.verticalCenter
        objectName: "volumeUp"
        implicitWidth: 28
        implicitHeight: 24
        label: "+"
        tint: "#c9d5e8"
        accessibleName: qsTr("增大音量")
        autoRepeat: true
        enabled: card.audio !== null && volumeDown.current < card.audio.maxVolume
        onClicked: card.audio.SetVolume(
          Math.min(card.audio.maxVolume, volumeDown.current + 0.05))
      }

      Slider {
        id: slider

        objectName: "volumeSlider"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: card.hasMedia ? 40 : 32 + 40
        anchors.rightMargin: 40
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height
        padding: 0
        from: 0
        to: card.audio ? card.audio.maxVolume : 1
        stepSize: 0.01
        focusPolicy: Qt.NoFocus
        onMoved: if (card.audio) card.audio.SetVolume(value)

        Binding on value {
          when: !slider.pressed
          value: card.audio && !card.audio.muted ? card.audio.volume : 0
        }

        background: Track {
          y: (slider.height - height) / 2
          width: slider.width
          value: slider.visualPosition
        }

        handle: Rectangle {
          x: slider.visualPosition * (slider.width - width)
          y: (slider.height - height) / 2
          width: 12
          height: 12
          radius: 6
          color: slider.pressed ? "#c9d0db" : "#ffffff"
        }
      }
    }
  }
}
