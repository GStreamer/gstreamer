if USE_GST_V4L2 

GST_V4L2_TESTS = v4l2src-test

v4l2src_test_SOURCES = v4l2src-test.c
v4l2src_test_CFLAGS =   $(GST_PLUGINS_BASE_CFLAGS)
v4l2src_test_LDADD =   $(GST_PLUGINS_BASE_LIBS)

v4l2src_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lgstinterfaces-$(GST_MAJORMINOR)

else
GST_V4L2_TESTS =
endif

noinst_PROGRAMS = $(GST_V4L2_TESTS)

