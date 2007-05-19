include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

GST_TOOLS_DIR = $(top_builddir)/tools

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

# FIXME: the PLUGIN_PATH has a hack to reach good for now
# decide if it's worth it to add a .pc file for this, plus a dependancy on it
TESTS_ENVIRONMENT = \
        $(REGISTRY_ENVIRONMENT)                                 \
        GST_PLUGIN_SYSTEM_PATH=					\
        GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(GSTPB_PLUGINS_DIR):$(GST_PLUGINS_DIR)

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# the core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-bad.supp

clean-local: clean-local-check

if USE_MPEG2ENC
check_mpeg2enc = elements/mpeg2enc
else
check_mpeg2enc =
endif

if USE_NEON
check_neon = elements/neonhttpsrc
else
check_neon = 
endif

if USE_WAVPACK
check_wavpack = \
       elements/wavpackparse \
       elements/wavpackdec \
       elements/wavpackenc
else
check_wavpack =
endif

VALGRIND_TO_FIX = \
	elements/mpeg2enc

# valgrind testing
# videocrop disabled since it takes way too long in valgrind
VALGRIND_TESTS_DISABLE = \
	elements/videocrop \
	$(VALGRIND_TO_FIX)

check_PROGRAMS = \
	elements/deinterleave \
	$(check_mpeg2enc)  \
	$(check_neon)      \
	elements/rganalysis \
	elements/rglimiter \
	elements/rgvolume \
	elements/videocrop \
	$(check_wavpack)   \
	elements/y4menc

TESTS = $(check_PROGRAMS)

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)


elements_videocrop_LDADD = $(LDADD) $(GST_BASE_LIBS)
elements_videocrop_CFLAGS = $(CFLAGS) $(AM_CFLAGS) $(GST_BASE_CFLAGS)

elements_deinterleave_LDADD = $(LDADD) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR)
elements_deinterleave_CFLAGS = $(CFLAGS) $(AM_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
