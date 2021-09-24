TEMPLATE = app

QT += qml quick widgets

CONFIG += c++11

DEFINES += GST_USE_UNSTABLE_API

RESOURCES += qml.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Default rules for deployment.
include(deployment.pri)

# not tested (yet)
unix:!macx {
QT_CONFIG -= no-pkg-config
CONFIG += link_pkgconfig
PKGCONFIG = \
    gstreamer-1.0 \
    gstreamer-player-1.0 \
    gstreamer-tag-1.0
}

macx {
    QMAKE_MAC_SDK = macosx10.9
    INCLUDEPATH += /Library/Frameworks/GStreamer.framework/Headers

    LIBS += \
        -framework AppKit \
        -F/Library/Frameworks -framework GStreamer
}

HEADERS +=

SOURCES += main.cpp

DISTFILES +=
