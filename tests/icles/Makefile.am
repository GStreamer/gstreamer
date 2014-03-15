if HAVE_GTK
GTK_TESTS = gdkpixbufsink-test
gdkpixbufsink_test_SOURCES = gdkpixbufsink-test.c
gdkpixbufsink_test_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
gdkpixbufsink_test_LDADD   = $(GST_LIBS) $(GTK_LIBS)
else
GTK_TESTS =
endif

V4L2_TESTS = v4l2src-test

v4l2src_test_SOURCES = v4l2src-test.c
v4l2src_test_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
v4l2src_test_LDADD = $(GST_PLUGINS_BASE_LIBS) \
		     -lgstvideo-$(GST_API_VERSION)

if USE_OSS4
OSS4_TESTS=test-oss4

test_oss4_SOURCES = test-oss4.c
test_oss4_CFLAGS  = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
test_oss4_LDADD   = $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS)
test_oss4_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
else
OSS4_TESTS=
endif

if USE_X
X_TESTS = ximagesrc-test

ximagesrc_test_SOURCES = ximagesrc-test.c
ximagesrc_test_CFLAGS = $(GST_CFLAGS)
ximagesrc_test_LDADD = $(GST_LIBS)
else
X_TESTS =
endif

equalizer_test_SOURCES = equalizer-test.c
equalizer_test_CFLAGS  = $(GST_CFLAGS)
equalizer_test_LDADD   = $(GST_LIBS)

videocrop_test_SOURCES = videocrop-test.c
videocrop_test_CFLAGS  = $(GST_CFLAGS)
videocrop_test_LDADD   = $(GST_LIBS)

videobox_test_SOURCES = videobox-test.c
videobox_test_CFLAGS  = $(GST_CFLAGS)
videobox_test_LDADD   = $(GST_LIBS)

videocrop2_test_SOURCES = videocrop2-test.c
videocrop2_test_CFLAGS  = $(GST_CFLAGS)
videocrop2_test_LDADD   = $(GST_LIBS)

noinst_PROGRAMS = $(GTK_TESTS) $(OSS4_TESTS) $(V4L2_TESTS) $(X_TESTS) equalizer-test videocrop-test videobox-test videocrop2-test

