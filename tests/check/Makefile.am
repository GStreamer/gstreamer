include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg
TEST_FILES_DIRECTORY = $(top_srcdir)/tests/files

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        CK_DEFAULT_TIMEOUT=120                                  \
        GST_STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(GST_PLUGINS_DIR):$(GSTPB_PLUGINS_DIR):$(GSTPG_PLUGINS_DIR)	\
	GST_PLUGIN_LOADING_WHITELIST="gstreamer:gst-plugins-base:gst-plugins-good"


# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

clean-local: clean-local-check

$(CHECK_REGISTRY):
	$(TESTS_ENVIRONMENT)

TESTS = $(check_PROGRAMS)

check_PROGRAMS = \
	gst/rtspserver

# these tests don't even pass
noinst_PROGRAMS =

AM_CFLAGS = $(GST_CFLAGS) $(GST_CHECK_CFLAGS) \
	-DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS
AM_CXXFLAGS = $(GST_CXXFLAGS) $(GST_CHECK_CFLAGS) \
	-DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS
LDADD = $(GST_LIBS) $(GST_CHECK_LIBS) $(GST_RTSP_SERVER_LIBS)

SUPPRESSIONS = $(top_srcdir)/common/gst.supp

gst_rtspserver_SOURCES = gst/rtspserver.c \
	$(top_srcdir)/gst/rtsp-server/rtsp-server.h

gst_rtspserver_CFLAGS = \
	-I$(top_srcdir)/gst/rtsp-server \
	$(GST_PLUGINS_GOOD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

gst_rtspserver_LDADD = \
	$(top_builddir)/gst/rtsp-server/libgstrtspserver-@GST_API_VERSION@.la \
	$(GST_PLUGINS_GOOD_LIBS) \
	$(GST_BASE_LIBS) -lgstrtsp-@GST_API_VERSION@ -lgstsdp-@GST_API_VERSION@ \
	$(LDADD)
