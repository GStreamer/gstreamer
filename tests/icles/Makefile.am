if HAVE_GTK
GTK_TESTS = gdkpixbufsink-test
gdkpixbufsink_test_SOURCES = gdkpixbufsink-test.c
gdkpixbufsink_test_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
gdkpixbufsink_test_LDADD   = $(GST_LIBS) $(GTK_LIBS)
gdkpixbufsink_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
else
GTK_TESTS =
endif

if USE_GST_V4L2
V4L2_TESTS = v4l2src-test

v4l2src_test_SOURCES = v4l2src-test.c
v4l2src_test_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
v4l2src_test_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR) $(GST_LIBS)
v4l2src_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

else
V4L2_TESTS =
endif

if USE_X
X_TESTS = ximagesrc-test

ximagesrc_test_SOURCES = ximagesrc-test.c
ximagesrc_test_CFLAGS = $(GST_CFLAGS)
ximagesrc_test_LDADD = $(GST_LIBS)
else
X_TESTS =
endif

videocrop_test_SOURCES = videocrop-test.c
videocrop_test_CFLAGS  = $(GST_CFLAGS)
videocrop_test_LDADD   = $(GST_LIBS)
videocrop_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

videobox_test_SOURCES = videobox-test.c
videobox_test_CFLAGS  = $(GST_CFLAGS)
videobox_test_LDADD   = $(GST_LIBS)
videobox_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

videocrop2_test_SOURCES = videocrop2-test.c
videocrop2_test_CFLAGS  = $(GST_CFLAGS)
videocrop2_test_LDADD   = $(GST_LIBS)
videocrop2_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_PROGRAMS = $(GTK_TESTS) $(V4L2_TESTS) $(X_TESTS) videocrop-test videobox-test videocrop2-test

