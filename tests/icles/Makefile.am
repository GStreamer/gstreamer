if USE_GST_V4L2
V4L2_TESTS = v4l2src-test

v4l2src_test_SOURCES = v4l2src-test.c
v4l2src_test_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS)
v4l2src_test_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR)
v4l2src_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

else
V4L2_TESTS =
endif

if USE_X
X_TESTS = ximagesrc-test

ximagesrc_test_SOURCES = ximagesrc-test.c
ximagesrc_test_CFLAGS = $(GST_CFLAGS)
ximagesrc_test_LDFLAGS = $(GST_LIBS)
else
X_TESTS =
endif

videocrop_test_SOURCES = videocrop-test.c
videocrop_test_CFLAGS  = $(GST_CFLAGS)
videocrop_test_LDADD   = $(GST_LIBS)
videocrop_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_PROGRAMS = $(V4L2_TESTS) $(X_TESTS) videocrop-test

