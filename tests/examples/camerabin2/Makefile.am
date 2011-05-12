GST_CAMERABIN_UI_FILES = gst-camera2.ui

if HAVE_GTK

GST_CAMERABIN_GTK_EXAMPLES = gst-camera2

gst_camera2_SOURCES = gst-camera2.h gst-camera2.c
gst_camera2_CFLAGS  = \
        $(GST_PLUGINS_BAD_CFLAGS) \
        $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GTK_CFLAGS) \
	$(GMODULE_EXPORT_CFLAGS) \
        -DGST_USE_UNSTABLE_API
gst_camera2_LDADD   = \
        $(top_builddir)/gst-libs/gst/interfaces/libgstphotography-@GST_MAJORMINOR@.la \
        $(GST_PLUGINS_BASE_LIBS) \
        -lgstinterfaces-@GST_MAJORMINOR@ \
        $(GST_LIBS) \
	$(GTK_LIBS) \
	$(GMODULE_EXPORT_LIBS)

noinst_DATA = $(GST_CAMERABIN_UI_FILES)

INCLUDES = -DCAMERA_APPS_UIDIR=\""$(srcdir)"\"

else
GST_CAMERABIN_GTK_EXAMPLES =
endif

if HAVE_X11

GST_CAMERABIN_X11_EXAMPLES = gst-camerabin2-test

gst_camerabin2_test_SOURCES = gst-camerabin2-test.c
gst_camerabin2_test_CFLAGS  = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_PLUGINS_BAD_CFLAGS)
gst_camerabin2_test_LDADD   = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstphotography-@GST_MAJORMINOR@.la \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstinterfaces-@GST_MAJORMINOR@ \
	-lgstpbutils-@GST_MAJORMINOR@ \
	-lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(X11_LIBS)

else
GST_CAMERABIN_X11_EXAMPLES =
endif

noinst_PROGRAMS = $(GST_CAMERABIN_GTK_EXAMPLES) $(GST_CAMERABIN_X11_EXAMPLES)

EXTRA_DIST = $(GST_CAMERABIN_UI_FILES)

