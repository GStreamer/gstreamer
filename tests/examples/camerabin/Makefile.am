GST_CAMERABIN_UI_FILES = gst-camera.ui

if HAVE_GTK

GST_CAMERABIN_GTK_EXAMPLES = gst-camera

gst_camera_SOURCES = gst-camera.h gst-camera.c
gst_camera_CFLAGS  = \
        -I$(top_builddir)/gst-libs \
        $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GTK_CFLAGS) \
	$(GMODULE_EXPORT_CFLAGS) \
        -DGST_USE_UNSTABLE_API
gst_camera_LDADD   = \
        $(top_builddir)/gst-libs/gst/interfaces/libgstphotography-@GST_MAJORMINOR@.la \
        $(GST_PLUGINS_BASE_LIBS) \
        -lgstinterfaces-@GST_MAJORMINOR@ \
        $(GST_LIBS) \
	$(GTK_LIBS) \
	$(GMODULE_EXPORT_LIBS)

uidir = $(datadir)/gstreamer-@GST_MAJORMINOR@/camera-apps
ui_DATA = $(GST_CAMERABIN_UI_FILES)

INCLUDES = -DCAMERA_APPS_UIDIR=\""$(uidir)"\"

else
GST_CAMERABIN_GTK_EXAMPLES =
endif

gst_camera_perf_SOURCES = gst-camera-perf.c
gst_camera_perf_CFLAGS  = $(GST_CFLAGS)
gst_camera_perf_LDADD = $(GST_LIBS) 

if HAVE_X11

GST_CAMERABIN_X11_EXAMPLES = gst-camerabin-test

gst_camerabin_test_SOURCES = gst-camerabin-test.c
gst_camerabin_test_CFLAGS  = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
gst_camerabin_test_LDADD = -lgstinterfaces-@GST_MAJORMINOR@ $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) $(X11_LIBS)

else
GST_CAMERABIN_X11_EXAMPLES =
endif

noinst_PROGRAMS = gst-camera-perf $(GST_CAMERABIN_X11_EXAMPLES) $(GST_CAMERABIN_GTK_EXAMPLES)

EXTRA_DIST = $(GST_CAMERABIN_UI_FILES)

