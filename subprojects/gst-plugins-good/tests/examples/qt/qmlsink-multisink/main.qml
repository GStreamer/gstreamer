// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (C) 2015, Matthew Waters <matthew@centricular.com>
// Copyright (C) 2021, Dmitry Shusharin <pmdvsh@gmail.com>
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
import QtQuick.Controls 1.1

import "videoitem"

ApplicationWindow {
    visible: true

    minimumWidth: videowall.cellWidth * Math.sqrt(videowall.model.length) + videowall.leftMargin
    minimumHeight: videowall.cellHeight * Math.sqrt(videowall.model.length) + 32
    maximumWidth: minimumWidth
    maximumHeight: minimumHeight

    GridView {
        id: videowall
        leftMargin: 10
        model: patterns
        anchors.fill: parent
        cellWidth: 500
        cellHeight: 500
        delegate: Rectangle {
            border.color: "darkgray"
            width: videowall.cellWidth - 10
            height: videowall.cellHeight - 10
            radius: 3
            Label {
                anchors.centerIn: parent
                text: "No signal"
            }
            Loader {
                active: playing.checked
                anchors.fill: parent
                anchors.margins: 1
                sourceComponent: VideoItem {
                    id: player
                    source: playing.checked ? modelData : ""
                }
            }
            Row {
                anchors.margins: 1
                id: controls
                height: 32
                spacing: 10
                Button {
                    id: playing
                    checkable: true
                    checked: true
                    width: height
                    height: controls.height
                    text: index
                }
                Label {
                    verticalAlignment: Qt.AlignVCenter
                    height: controls.height
                    text: modelData
                }
            }
        }
    }
}
