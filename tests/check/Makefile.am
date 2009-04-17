include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/ext:$(GSTPB_PLUGINS_DIR):$(GST_PLUGINS_DIR)

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

clean-local: clean-local-check

check_PROGRAMS = \
	generic/plugin-test \
	generic/libavcodec-locking

VALGRIND_TO_FIX = \
	generic/plugin-test \
	generic/libavcodec-locking

TESTS = $(check_PROGRAMS)

# these tests don't even pass
noinst_PROGRAMS =

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

# valgrind testing
VALGRIND_TESTS_DISABLE = $(VALGRIND_TO_FIX)

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-ffmpeg.supp

EXTRA_DIST = gst-ffmpeg.supp
