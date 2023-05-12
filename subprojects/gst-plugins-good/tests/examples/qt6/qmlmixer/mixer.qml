import QtQuick 6.0

import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0

Item {
    /* render upside down for GStreamer */
    transform: Scale { origin.x : 0; origin.y : height / 2.; yScale : -1 }

    GstGLQt6VideoItem {
        id: video0
        objectName: "inputVideoItem0"
        anchors.left: parent.left
        anchors.right: parent.horizontalCenter
        width: parent.width / 2
        height: parent.height
    }

    GstGLQt6VideoItem {
        id: video1
        objectName: "inputVideoItem1"
        anchors.left: parent.horizontalCenter
        anchors.right: parent.right
        width: parent.width / 2
        height: parent.height
    }

    Text {
        id: rotatingText
        anchors.centerIn: parent
        text: "Qt Quick\nrendered to\na texture"
        font.pointSize: 20
        color: "black"
        style: Text.Outline
        styleColor: "white"

        RotationAnimator {
            target: rotatingText;
            from: 0;
            to: 360;
            duration: 5000
            running: true
            loops: Animation.Infinite
        }
    }

    Text {
        property int elapsedTime: 0

        id: time
        anchors.top: rotatingText.bottom
        anchors.horizontalCenter: rotatingText.horizontalCenter
        font.pointSize: 12
        style: Text.Outline
        styleColor: "black"
        color: "white"

        Timer {
            interval: 1000
            running: true
            repeat: true
            onTriggered: {
                parent.elapsedTime += interval / 1000
                parent.text = "overlay: " + parent.elapsedTime.toString() + " seconds"
            }
        }
    }
}
