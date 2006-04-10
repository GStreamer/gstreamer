include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        CK_DEFAULT_TIMEOUT=20                                   \
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(GST_PLUGINS_DIR)

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

clean-local: clean-local-check

$(CHECK_REGISTRY):
	$(TESTS_ENVIRONMENT)

TESTS = $(check_PROGRAMS)

if USE_VORBIS
check_vorbis = elements/vorbisdec pipelines/vorbisenc
else
check_vorbis =
endif

if USE_THEORA
check_theora = pipelines/theoraenc
else
check_theora =
endif

check_PROGRAMS = $(check_vorbis) \
	$(check_theora) \
	elements/audioconvert \
	elements/audioresample \
	elements/audiotestsrc \
	elements/videotestsrc \
	elements/volume \
	generic/clock-selection \
	generic/states \
	libs/cddabasesrc \
        pipelines/simple-launch-lines

VALGRIND_TO_FIX = \
	elements/audioresample \
	generic/states \
	libs/cddabasesrc \
        pipelines/simple-launch-lines

# these tests don't even pass
noinst_PROGRAMS =

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

# valgrind testing
VALGRIND_TESTS_DISABLE = $(VALGRIND_TO_FIX)

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-base.supp

libs_cddabasesrc_LDADD = \
	$(top_builddir)/gst-libs/gst/cdda/libgstcdda-@GST_MAJORMINOR@.la \
	$(LDADD)

libs_cddabasesrc_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(CFLAGS) $(AM_CFLAGS)

EXTRA_DIST = gst-plugins-base.supp
