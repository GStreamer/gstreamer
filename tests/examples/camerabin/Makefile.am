GST_CAMERABIN_GLADE_FILES = gst-camera.glade

if HAVE_GLADE
if HAVE_GTK

GST_CAMERABIN_GTK_EXAMPLES = gst-camera

gst_camera_SOURCES = gst-camera.c
gst_camera_CFLAGS  = \
        -I$(top_builddir)/gst-libs \
        $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GLADE_CFLAGS) \
        -DGST_USE_UNSTABLE_API
gst_camera_LDADD   = \
        $(top_builddir)/gst-libs/gst/interfaces/libgstphotography-@GST_MAJORMINOR@.la \
        $(GST_PLUGINS_BASE_LIBS) \
        -lgstinterfaces-@GST_MAJORMINOR@ \
        $(GST_LIBS) \
        $(GLADE_LIBS) 

gladedir = $(datadir)/gstreamer-@GST_MAJORMINOR@/camera-apps
glade_DATA = $(GST_CAMERABIN_GLADE_FILES)

INCLUDES = -DCAMERA_APPS_GLADEDIR=\""$(gladedir)"\" 

else
GST_CAMERABIN_GTK_EXAMPLES =
endif
else
GST_CAMERABIN_GTK_EXAMPLES =
endif

gst_camera_perf_SOURCES = gst-camera-perf.c
gst_camera_perf_CFLAGS  = $(GST_CFLAGS)
gst_camera_perf_LDADD = $(GST_LIBS) 

bin_PROGRAMS = gst-camera-perf $(GST_CAMERABIN_GTK_EXAMPLES)

EXTRA_DIST = $(GST_CAMERABIN_GLADE_FILES)

