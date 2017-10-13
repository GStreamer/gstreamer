TEMPLATE = lib

TARGET = gstqmlgl

QT += qml quick widgets gui

QT_CONFIG -= no-pkg-config
CONFIG += link_pkgconfig debug plugin
PKGCONFIG = \
    gstreamer-1.0 \
    gstreamer-video-1.0 \
    gstreamer-gl-1.0

DEFINES += \
    GST_USE_UNSTABLE_API \
    HAVE_QT_WIN32

SOURCES += \
    gstplugin.cc \
    gstqtglutility.cc \
    gstqsgtexture.cc \
    gstqtsink.cc \
    gstqtsrc.cc \
    qtwindow.cc \
    qtitem.cc

HEADERS += \
    gstqsgtexture.h \
    gstqtgl.h \
    gstqtglutility.h \
    gstqtsink.h \
    gstqtsrc.h \
    qtwindow.h \
    qtitem.h

INCLUDEPATH += \
    $$(GSTREAMER_ROOT)/include \
    $$[QT_INSTALL_PREFIX]/include/QtGui/$$[QT_VERSION]/QtGui/

    
