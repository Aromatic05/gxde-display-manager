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
import QtQuick.Effects

FocusScope {
  id: root

  property var model: []
  property alias currentIndex: userList.currentIndex
  property color foregroundColor: "white"
  property color avatarFallbackColor: "#b9bdc2"
  property color selectionColor: "#ffffff"
  property int avatarSize: 68
  property int avatarRadius: 14
  property int delegateWidth: 96
  property int delegateSpacing: 8
  property int navigationSize: 32
  property int navigationGutter: 40
  property int namePixelSize: 13
  property int avatarHeadroom: 4
  property bool revealed: true
  property real revealProgress: revealed ? 1.0 : 0.0
  property bool showNotListed: true
  property string notListedText: qsTr("未列出？")
  property url previousIconSource: "qrc:/icons/prev.svg"
  property url nextIconSource: "qrc:/icons/next.svg"
  property real pendingWheelAngle: 0
  property bool notListedSelected: false

  signal userConfirmed(int index, var user)
  signal notListedRequested()

  Behavior on revealProgress {
    NumberAnimation {
      duration: root.revealed ? 900 : 240
      easing.type: Easing.Linear
    }
  }

  implicitWidth: 560
  implicitHeight: carousel.height + (showNotListed ? 30 : 0)
  width: implicitWidth
  height: implicitHeight

  function modelCount() {
    if (root.model === null || root.model === undefined) {
      return 0
    }
    if (typeof root.model.count === "number") {
      return root.model.count
    }
    return typeof root.model.length === "number" ? root.model.length : 0
  }

  function userAt(index) {
    if (index < 0 || index >= modelCount()) {
      return ({})
    }
    if (typeof root.model.get === "function") {
      return root.model.get(index)
    }
    return root.model[index]
  }

  function fullNameFor(user) {
    if (!user) {
      return ""
    }
    return user.fullName || user.displayName || user.name || user.username || ""
  }

  function avatarFor(user) {
    if (!user) {
      return ""
    }
    return user.avatar || user.avatarUrl || user.icon || ""
  }

  function clamp01(value) {
    return Math.max(0, Math.min(1, value))
  }

  function easeOutCubic(t) {
    return 1 - Math.pow(1 - t, 3)
  }

  function revealAt(slot) {
    return clamp01((root.revealProgress - Math.min(slot, 6) * 0.05) / 0.45)
  }

  function confirmUser(index) {
    if (index < 0 || index >= modelCount()) {
      return
    }
    root.notListedSelected = false
    userList.currentIndex = index
    root.userConfirmed(index, userAt(index))
  }

  function moveSelection(step) {
    const count = modelCount()
    const last = count - 1 + (root.showNotListed ? 1 : 0)
    if (last < 0) return
    let index = root.notListedSelected ? count : userList.currentIndex
    if (index < 0) {
      index = step > 0 ? 0 : Math.max(0, count - 1)
    } else {
      index = Math.max(0, Math.min(last, index + step))
    }
    if (index >= count) {
      root.notListedSelected = true
      userList.currentIndex = -1
    } else {
      root.notListedSelected = false
      userList.currentIndex = index
      ensureVisible(index)
    }
  }

  function confirmSelection() {
    if (root.notListedSelected) {
      root.notListedRequested()
      return true
    }
    if (userList.currentIndex < 0) return false
    confirmUser(userList.currentIndex)
    return true
  }

  function ensureVisible(index) {
    const stride = root.delegateWidth + root.delegateSpacing
    const left = index * stride
    const right = left + root.delegateWidth
    const base = contentAnimation.running ? contentAnimation.to
      : userList.contentX
    if (left < base) {
      scrollTo(left)
    } else if (right > base + userList.width) {
      scrollTo(right - userList.width)
    }
  }

  onRevealedChanged: if (!revealed) root.notListedSelected = false

  function boundedContentX(value) {
    return Math.max(0, Math.min(userList.maximumContentX, value))
  }

  function scrollTo(value) {
    userList.cancelFlick()
    contentAnimation.stop()
    contentAnimation.to = boundedContentX(value)
    contentAnimation.start()
  }

  function scrollBy(delta) {
    const base = contentAnimation.running ? contentAnimation.to
      : userList.contentX
    scrollTo(base + delta)
  }

  function scrollPage(direction) {
    const stride = root.delegateWidth + root.delegateSpacing
    const visibleItems = Math.max(1, Math.floor(userList.width / stride))
    scrollBy(direction * visibleItems * stride)
  }

  function handleWheel(event) {
    if (!userList.hasOverflow) {
      event.accepted = false
      return
    }

    const pixelDelta = Math.abs(event.pixelDelta.x)
      > Math.abs(event.pixelDelta.y)
        ? event.pixelDelta.x : event.pixelDelta.y
    if (event.phase !== Qt.NoScrollPhase && pixelDelta !== 0) {
      root.pendingWheelAngle = 0
      contentAnimation.stop()
      userList.cancelFlick()
      userList.contentX = boundedContentX(userList.contentX - pixelDelta)
      event.accepted = true
      return
    }

    const angleDelta = Math.abs(event.angleDelta.x)
      > Math.abs(event.angleDelta.y)
        ? event.angleDelta.x : event.angleDelta.y
    if (angleDelta === 0) {
      event.accepted = false
      return
    }
    if (root.pendingWheelAngle * angleDelta < 0) {
      root.pendingWheelAngle = 0
    }
    root.pendingWheelAngle += angleDelta
    const steps = Math.trunc(root.pendingWheelAngle / 120)
    if (steps !== 0) {
      root.pendingWheelAngle -= steps * 120
      scrollBy(-steps * (root.delegateWidth + root.delegateSpacing))
    }
    event.accepted = true
  }

  WheelHandler {
    target: null
    blocking: true
    onWheel: (event) => root.handleWheel(event)
  }

  Item {
    id: carousel

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    height: root.avatarHeadroom + root.avatarSize + 30

    ListView {
      id: userList

      readonly property real maximumContentX: Math.max(0, contentWidth - width)
      readonly property bool hasOverflow: contentWidth > width + 0.5

      anchors.horizontalCenter: parent.horizontalCenter
      anchors.top: parent.top
      width: Math.min(Math.max(0, parent.width - root.navigationGutter * 2),
        Math.max(root.delegateWidth, contentWidth))
      height: parent.height
      model: root.model
      currentIndex: -1
      orientation: ListView.Horizontal
      spacing: root.delegateSpacing
      clip: true
      interactive: hasOverflow
      boundsBehavior: Flickable.DragAndOvershootBounds
      boundsMovement: Flickable.FollowBoundsBehavior
      flickDeceleration: 2600
      maximumFlickVelocity: 3600
      reuseItems: true
      keyNavigationEnabled: true

      rebound: Transition {
        NumberAnimation {
          properties: "x,y"
          duration: 240
          easing.type: Easing.OutCubic
        }
      }

      Keys.onLeftPressed: root.scrollPage(-1)
      Keys.onRightPressed: root.scrollPage(1)
      Keys.onReturnPressed: root.confirmUser(currentIndex)
      Keys.onEnterPressed: root.confirmUser(currentIndex)

      delegate: Item {
        id: userDelegate

        required property int index
        readonly property var userData: root.userAt(index)
        readonly property string fullName: root.fullNameFor(userData)
        readonly property url avatarSource: root.avatarFor(userData)
        readonly property bool selected: userList.currentIndex === index
        readonly property bool hovered: avatarHover.hovered
        readonly property bool pressed: avatarTap.pressed
        readonly property real stride: root.delegateWidth + root.delegateSpacing
        readonly property real viewX: x - userList.contentX
        readonly property real entrance:
          root.revealAt(Math.max(0, Math.round(viewX / stride)))
        readonly property real edgeFocus: {
          if (!userList.hasOverflow) {
            return 1.0
          }
          const zone = root.delegateWidth * 0.9
          const center = viewX + width / 2
          const leftRoom = root.clamp01(userList.contentX / zone)
          const rightRoom = root.clamp01(
            (userList.maximumContentX - userList.contentX) / zone)
          const left = 1 - leftRoom * (1 - root.clamp01(center / zone))
          const right = 1 - rightRoom
            * (1 - root.clamp01((userList.width - center) / zone))
          return Math.min(left, right)
        }

        width: root.delegateWidth
        height: userList.height

        Item {
          id: body

          anchors.fill: parent
          opacity: root.easeOutCubic(userDelegate.entrance)
            * (0.35 + 0.65 * userDelegate.edgeFocus)

          Item {
            id: avatarHolder

            anchors.horizontalCenter: parent.horizontalCenter
            y: root.avatarHeadroom
            width: root.avatarSize
            height: root.avatarSize

            Rectangle {
              id: shadowShape

              anchors.fill: parent
              radius: root.avatarRadius
              color: "black"
              visible: false
            }

            MultiEffect {
              anchors.fill: shadowShape
              source: shadowShape
              shadowEnabled: true
              shadowColor: "black"
              shadowBlur: 0.8
              blurMax: 16
              shadowVerticalOffset: 3
              shadowOpacity: 0.2
            }

            Rectangle {
              id: selectionRing

              anchors.centerIn: parent
              width: parent.width + 6
              height: parent.height + 6
              radius: root.avatarRadius + 3
              color: "transparent"
              antialiasing: true
              border.width: 2
              border.color: root.selectionColor
              opacity: userDelegate.selected ? 1.0
                : userDelegate.hovered ? 0.35 : 0.0

              Behavior on opacity {
                NumberAnimation {
                  duration: 160
                  easing.type: Easing.OutCubic
                }
              }
            }

            UserAvatar {
              id: avatarFrame

              anchors.fill: parent
              radius: root.avatarRadius
              source: userDelegate.avatarSource
              name: userDelegate.fullName
              fallbackColor: root.avatarFallbackColor

              Rectangle {
                anchors.fill: parent
                radius: avatarFrame.radius
                antialiasing: true
                color: userDelegate.pressed ? "#26000000"
                  : userDelegate.hovered ? "#14ffffff" : "#00ffffff"

                Behavior on color {
                  ColorAnimation {
                    duration: 120
                  }
                }
              }
            }
          }

          Text {
            anchors.top: avatarHolder.bottom
            anchors.topMargin: 7
            anchors.horizontalCenter: avatarHolder.horizontalCenter
            width: parent.width
            text: userDelegate.fullName
            color: root.foregroundColor
            opacity: userDelegate.selected || userDelegate.hovered ? 1.0 : 0.78
            font.pixelSize: root.namePixelSize
            font.weight: Font.Normal
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            maximumLineCount: 1
            renderType: Text.NativeRendering

            Behavior on opacity {
              NumberAnimation {
                duration: 160
                easing.type: Easing.OutCubic
              }
            }
          }
        }

        TapHandler {
          id: avatarTap
          onTapped: root.confirmUser(userDelegate.index)
        }

        HoverHandler {
          id: avatarHover
          cursorShape: Qt.PointingHandCursor
        }
      }

      WheelHandler {
        target: null
        blocking: true
        onWheel: (event) => root.handleWheel(event)
      }
    }

    ToolButton {
      id: previousButton

      anchors.left: parent.left
      y: root.avatarHeadroom + (root.avatarSize - height) / 2
      width: root.navigationSize
      height: root.navigationSize
      padding: 4
      focusPolicy: Qt.NoFocus
      hoverEnabled: true
      readonly property bool shown: userList.hasOverflow && userList.contentX > 0.5
      opacity: (shown ? 1.0 : 0.0)
        * root.clamp01((root.revealProgress - 0.45) / 0.55)
      visible: opacity > 0
      enabled: shown

      Behavior on opacity {
        NumberAnimation {
          duration: 200
          easing.type: Easing.OutCubic
        }
      }
      icon.source: root.previousIconSource
      icon.width: 24
      icon.height: 24
      icon.color: root.foregroundColor
      display: AbstractButton.IconOnly
      onClicked: root.scrollPage(-1)

      background: Rectangle {
        radius: 4
        color: previousButton.down ? "#38000000"
          : previousButton.hovered ? "#24000000" : "transparent"
      }

      HoverHandler {
        cursorShape: Qt.PointingHandCursor
      }
    }

    ToolButton {
      id: nextButton

      anchors.right: parent.right
      y: root.avatarHeadroom + (root.avatarSize - height) / 2
      width: root.navigationSize
      height: root.navigationSize
      padding: 4
      focusPolicy: Qt.NoFocus
      hoverEnabled: true
      readonly property bool shown: userList.hasOverflow
        && userList.contentX < userList.maximumContentX - 0.5
      opacity: (shown ? 1.0 : 0.0)
        * root.clamp01((root.revealProgress - 0.45) / 0.55)
      visible: opacity > 0
      enabled: shown

      Behavior on opacity {
        NumberAnimation {
          duration: 200
          easing.type: Easing.OutCubic
        }
      }
      icon.source: root.nextIconSource
      icon.width: 24
      icon.height: 24
      icon.color: root.foregroundColor
      display: AbstractButton.IconOnly
      onClicked: root.scrollPage(1)

      background: Rectangle {
        radius: 4
        color: nextButton.down ? "#38000000"
          : nextButton.hovered ? "#24000000" : "transparent"
      }

      HoverHandler {
        cursorShape: Qt.PointingHandCursor
      }
    }

    NumberAnimation {
      id: contentAnimation

      target: userList
      property: "contentX"
      duration: 280
      easing.type: Easing.OutCubic
    }
  }

  Text {
    id: notListedAction

    anchors.top: carousel.bottom
    anchors.topMargin: 2
    anchors.horizontalCenter: parent.horizontalCenter
    visible: root.showNotListed
    text: root.notListedText
    color: root.foregroundColor
    opacity: (notListedHover.hovered || root.notListedSelected ? 1.0 : 0.78)
      * root.clamp01((root.revealProgress - 0.5) / 0.5)
    transform: Translate {
      y: (1 - root.clamp01((root.revealProgress - 0.5) / 0.5)) * 14
    }

    Rectangle {
      anchors.top: parent.baseline
      anchors.topMargin: 3
      anchors.horizontalCenter: parent.horizontalCenter
      width: notListedHover.hovered || root.notListedSelected
        ? parent.width : 0
      height: 1
      color: root.foregroundColor

      Behavior on width {
        NumberAnimation {
          duration: 240
          easing.type: Easing.OutCubic
        }
      }
    }
    font.pixelSize: 13
    font.weight: Font.Normal
    renderType: Text.NativeRendering

    TapHandler {
      onTapped: root.notListedRequested()
    }

    HoverHandler {
      id: notListedHover
      cursorShape: Qt.PointingHandCursor
    }
  }
}
