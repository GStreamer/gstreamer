include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg

GST_TOOLS_DIR = $(top_builddir)/tools

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

# FIXME: the PLUGIN_PATH has a hack to reach good for now
# decide if it's worth it to add a .pc file for this, plus a dependancy on it
TESTS_ENVIRONMENT = \
        $(REGISTRY_ENVIRONMENT)                                 \
        GST_PLUGIN_SYSTEM_PATH=					\
        GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(GSTPB_PLUGINS_DIR):$(GST_PLUGINS_DIR) \
        STATE_IGNORE_ELEMENTS="vcdsrc nassink glimagesink"

EXTRA_DIST = test-cert.pem test-key.pem

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# the core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

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

if USE_SOUP
check_soup = elements/souphttpsrc
else
check_soup = 
endif

if USE_TIMIDITY
check_timidity=elements/timidity
else
check_timidity=
endif


VALGRIND_TO_FIX = \
	elements/mpeg2enc

# valgrind testing
VALGRIND_TESTS_DISABLE = \
	$(VALGRIND_TO_FIX)

check_PROGRAMS = \
	generic/states \
	$(check_mpeg2enc)  \
	$(check_neon)      \
	$(check_soup)      \
	$(check_timidity)  \
	elements/interleave \
	elements/multifile \
	elements/rganalysis \
	elements/rglimiter \
	elements/rgvolume \
	elements/y4menc

noinst_HEADERS = 

TESTS = $(check_PROGRAMS)

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS)  $(GST_OPTION_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

elements_timidity_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_timidity_LDADD = $(GST_BASE_LIBS) $(LDADD)

elements_souphttpsrc_CFLAGS = $(SOUP_CFLAGS) $(AM_CFLAGS)
elements_souphttpsrc_LDADD = $(SOUP_LIBS) $(LDADD)
