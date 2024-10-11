// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (C) 2020, Matthew Waters <matthew@centricular.com>
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// a) Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// b) Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.

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
