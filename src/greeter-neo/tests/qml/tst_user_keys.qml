import QtQuick
import QtTest
import TransitionTest 1.0

TestCase {
    id: testCase
    name: "UserKeyboardSelection"
    when: windowShown
    width: 900
    height: 700

    TransitionBackend { id: backend }
    property var root
    property var selecter
    property var card

    readonly property var users: [
        { username: "alice", fullName: "Alice" },
        { username: "bob", fullName: "Bob" },
        { username: "carol", fullName: "Carol" }
    ]

    function init() {
        const component = Qt.createComponent("qrc:/transition-test/MainWindow.qml")
        compare(component.status, Component.Ready, component.errorString())
        // No screen windows: the scene stays in this test's window, so key
        // events reach the greeter.
        root = component.createObject(testCase, {
            width: 900, height: 700, greeterBackend: backend,
            userModel: users,
            sessionModel: [{ id: "x11/a.desktop", name: "A", type: "x11",
                             file: "a.desktop" }]
        })
        verify(root !== null)
        selecter = findChild(root, "userSelecter")
        card = findChild(root, "passwordCard")
        verify(selecter !== null && card !== null)
        wait(50)
        keyClick(Qt.Key_Space)          // clock -> user list
        tryCompare(selecter, "revealed", true)
        compare(selecter.currentIndex, -1)
    }
    function cleanup() {
        root.destroy()
        wait(20)
    }

    function test_arrows_walk_users_then_not_listed() {
        keyClick(Qt.Key_Right)
        compare(selecter.currentIndex, 0)
        keyClick(Qt.Key_Down)
        compare(selecter.currentIndex, 1)
        keyClick(Qt.Key_Right)
        compare(selecter.currentIndex, 2)
        keyClick(Qt.Key_Right)          // past the last user
        compare(selecter.notListedSelected, true)
        compare(selecter.currentIndex, -1)
        keyClick(Qt.Key_Right)          // stops at the end
        compare(selecter.notListedSelected, true)
        keyClick(Qt.Key_Left)
        compare(selecter.notListedSelected, false)
        compare(selecter.currentIndex, 2)
        keyClick(Qt.Key_Up)
        keyClick(Qt.Key_Up)
        keyClick(Qt.Key_Up)             // stops at the first user
        compare(selecter.currentIndex, 0)
        verify(!card.opened)
    }

    function test_first_left_picks_last_user() {
        keyClick(Qt.Key_Left)
        compare(selecter.currentIndex, 2)
    }

    function test_enter_and_space_open_selected_user() {
        keyClick(Qt.Key_Right)
        keyClick(Qt.Key_Right)
        keyClick(Qt.Key_Return)
        tryCompare(card, "opened", true)
        compare(card.askUserName, false)
        compare(card.user.username, "bob")

        keyClick(Qt.Key_Escape)         // card closes, list keeps the keys
        tryCompare(card, "visible", false)
        wait(20)
        keyClick(Qt.Key_Right)
        compare(selecter.currentIndex, 2)
        keyClick(Qt.Key_Space)
        tryCompare(card, "opened", true)
        compare(card.user.username, "carol")
    }

    function test_enter_without_selection_selects_first() {
        keyClick(Qt.Key_Enter)
        compare(selecter.currentIndex, 0)
        verify(!card.opened)
        keyClick(Qt.Key_Enter)
        tryCompare(card, "opened", true)
        compare(card.user.username, "alice")
    }

    function test_space_on_not_listed_opens_manual_login() {
        for (let i = 0; i < 4; ++i) keyClick(Qt.Key_Right)
        verify(selecter.notListedSelected)
        keyClick(Qt.Key_Space)
        tryCompare(card, "opened", true)
        compare(card.askUserName, true)
    }

    function test_escape_hides_list_and_clears_not_listed() {
        for (let i = 0; i < 4; ++i) keyClick(Qt.Key_Right)
        verify(selecter.notListedSelected)
        keyClick(Qt.Key_Escape)
        tryCompare(selecter, "revealed", false)
        compare(selecter.notListedSelected, false)
    }
}
