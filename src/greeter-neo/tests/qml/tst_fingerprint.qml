import QtQuick
import QtQuick.Window
import QtTest
import TransitionTest 1.0

TestCase {
    id: testCase
    name: "FingerprintLogin"
    when: windowShown

    TransitionBackend { id: backend }
    TransitionScreenManager { id: manager }
    property var root
    property var card
    property var win

    readonly property var alice: ({ username: "alice", fullName: "Alice" })

    function init() {
        backend.fingerprintCalls = []
        backend.fingerprintListening = false
        backend.fingerprintGaveUp = false
        const component = Qt.createComponent("qrc:/transition-test/MainWindow.qml")
        compare(component.status, Component.Ready, component.errorString())
        root = component.createObject(testCase, {
            width: 640, height: 480, greeterBackend: backend,
            screenManager: manager,
            userModel: [alice],
            sessionModel: [
                { id: "x11/a.desktop", name: "A", type: "x11", file: "a.desktop" },
                { id: "x11/b.desktop", name: "B", type: "x11", file: "b.desktop" }
            ]
        })
        verify(root !== null)
        win = root.screenWindow.createObject(root, {visible: true})
        manager.currentWindow = win
        card = findChild(root, "passwordCard")
        verify(card !== null)
        wait(50)
        backend.fingerprintCalls = []
    }
    function cleanup() {
        manager.currentWindow = null
        win.destroy()
        root.destroy()
        wait(20)
    }

    function test_listens_while_card_open_for_listed_user() {
        root.openPasswordDialog(0, alice)
        tryCompare(card, "opened", true)
        compare(backend.fingerprintCalls, ["alice|x11/a.desktop"])

        backend.fingerprintListening = true
        backend.FingerprintChanged()
        verify(card.fingerprintListening)

        // Another session: restart for it.
        root.selectedSessionIndex = 1
        compare(backend.fingerprintCalls[1], "alice|x11/b.desktop")

        card.close()
        tryCompare(card, "opened", false)
        compare(backend.fingerprintCalls[backend.fingerprintCalls.length - 1],
                "stop")
    }

    function test_manual_user_name_does_not_listen() {
        root.openManualLoginDialog()
        tryCompare(card, "opened", true)
        wait(20)
        verify(backend.fingerprintCalls.indexOf("alice|x11/a.desktop") < 0)
        for (const call of backend.fingerprintCalls) compare(call, "stop")
    }

    function test_rejection_and_give_up_messages() {
        root.openPasswordDialog(0, alice)
        tryCompare(card, "opened", true)
        backend.FingerprintRejected("指纹匹配失败")
        compare(card.errorText, "指纹匹配失败")

        backend.fingerprintGaveUp = true
        backend.FingerprintChanged()
        compare(card.errorText, "指纹多次不匹配，请输入密码")
    }

    function test_password_submit_stops_listening() {
        root.openPasswordDialog(0, alice)
        tryCompare(card, "opened", true)
        card.busy = true
        compare(backend.fingerprintCalls[backend.fingerprintCalls.length - 1],
                "stop")
        // A failed password re-arms the reader.
        card.busy = false
        compare(backend.fingerprintCalls[backend.fingerprintCalls.length - 1],
                "alice|x11/a.desktop")
    }
}
