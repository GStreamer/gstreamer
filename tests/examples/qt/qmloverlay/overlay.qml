import QtQuick 2.4

import org.freedesktop.gstreamer.GLVideoItem 1.0

Item {
    /* render upside down for GStreamer */
    transform: Scale { origin.x : 0; origin.y : height / 2.; yScale : -1 }

    GstGLVideoItem {
        id: video
        objectName: "inputVideoItem"
        anchors.centerIn: parent
        width: parent.width
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
