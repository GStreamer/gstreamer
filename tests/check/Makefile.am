include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/ext:$(top_builddir)/sys:$(GSTPB_PLUGINS_DIR):$(GST_PLUGINS_DIR)  \
	GST_PLUGIN_LOADING_WHITELIST="gstreamer@$(GST_PLUGINS_DIR):gst-plugins-base@$(GSTPB_PLUGINS_DIR):gst-plugins-ugly@$(top_builddir)" \
	GST_STATE_IGNORE_ELEMENTS="siddec dvdreadsrc cdiocddasrc"

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

clean-local: clean-local-check

TESTS = $(check_PROGRAMS)

if USE_AMRNB
AMRNB = elements/amrnbenc
else
AMRNB =
endif

if USE_LAME
LAME = pipelines/lame
else
LAME =
endif

if USE_MPEG2DEC
MPEG2DEC = elements/mpeg2dec
else
MPEG2DEC =
endif

if USE_X264
check_x264enc=elements/x264enc
else
check_x264enc=
endif

# generic/index
check_PROGRAMS = \
	generic/states \
	$(AMRNB) \
	$(LAME) \
	$(MPEG2DEC) \
	$(check_x264enc) \
	elements/xingmux

# these tests don't even pass
noinst_PROGRAMS =

noinst_HEADERS = elements/xingmux_testdata.h

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS) \
		-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS \
		-DGST_USE_UNSTABLE_API
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

# valgrind testing
VALGRIND_TESTS_DISABLE = \
	elements/x264enc

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-ugly.supp

elements_amrnbenc_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(AM_CFLAGS)
elements_amrnbenc_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)

elements_cmmldec_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(AM_CFLAGS)
elements_cmmlenc_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(AM_CFLAGS)

EXTRA_DIST = gst-plugins-ugly.supp
