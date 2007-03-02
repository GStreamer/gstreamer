include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/ext:$(top_builddir)/sys:$(GSTPB_PLUGINS_DIR):$(GST_PLUGINS_DIR)			\
	STATE_IGNORE_ELEMENTS="aasink cacasink"

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

clean-local: clean-local-check

if USE_ANNODEX
check_annodex = \
	elements/cmmldec \
	elements/cmmlenc
else
check_annodex = 
endif

if USE_TAGLIB
check_taglib = \
	elements/id3v2mux \
	elements/apev2mux
else
check_taglib =
endif

check_PROGRAMS = \
	generic/states \
	$(check_annodex) \
	elements/audiopanorama \
	elements/audioinvert \
	elements/audioamplify \
	elements/avimux \
	elements/level \
	elements/matroskamux \
	elements/icydemux \
	elements/videofilter
	$(check_taglib)
    
TESTS = $(check_PROGRAMS)

# these tests don't even pass
# autodetect: temporarily disabled because of broken videosinks in -bad
noinst_PROGRAMS = \
	elements/autodetect

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

# valgrind testing
VALGRIND_TESTS_DISABLE =

SUPPRESSIONS = $(top_srcdir)/common/gst.supp

elements_audiopanorama_LDADD = \
	$(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) \
	$(LDADD)

elements_audiopanorama_CFLAGS = \
	$(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) \
	$(CFLAGS) $(AM_CFLAGS)

elements_cmmldec_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS)
elements_cmmlenc_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS)
