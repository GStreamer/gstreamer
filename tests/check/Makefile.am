include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        CK_DEFAULT_TIMEOUT=120                                  \
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(GST_PLUGINS_DIR)

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

clean-local: clean-local-check

$(CHECK_REGISTRY):
	$(TESTS_ENVIRONMENT)

TESTS = $(check_PROGRAMS)

if USE_ALSA
check_alsa = elements/alsa
else
check_alsa =
endif

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

check_PROGRAMS = \
	$(check_alsa) \
	$(check_vorbis) \
	$(check_theora) \
        elements/adder	\
	elements/audioconvert \
	elements/audioresample \
	elements/audiotestsrc \
	elements/multifdsink \
	elements/videorate \
	elements/videotestsrc \
	elements/volume \
	generic/clock-selection \
	generic/states \
	libs/audio \
	libs/cddabasesrc \
	libs/video \
        pipelines/simple-launch-lines

# TORTURE_TO_FIX = \
#       elements/adder

VALGRIND_TO_FIX = \
	elements/alsa \
	elements/audioresample \
	generic/states \
	libs/cddabasesrc \
	libs/video

# these tests don't even pass
noinst_PROGRAMS =

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

# valgrind testing
VALGRIND_TESTS_DISABLE = $(VALGRIND_TO_FIX)

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-base.supp

libs_audio_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
	$(LDADD)
libs_audio_CFLAGS = -I$(top_srcdir)/gst-libs $(CFLAGS) $(AM_CFLAGS)

libs_cddabasesrc_LDADD = \
	$(top_builddir)/gst-libs/gst/cdda/libgstcdda-@GST_MAJORMINOR@.la \
	$(LDADD)

libs_cddabasesrc_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(CFLAGS) $(AM_CFLAGS)

elements_alsa_LDADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(LDADD)

elements_alsa_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(CFLAGS) $(AM_CFLAGS)

elements_audioconvert_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
	$(LDADD)

elements_audioconvert_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(CFLAGS) $(AM_CFLAGS)

elements_volume_LDADD = \
	$(GST_BASE_LIBS) \
	$(LDADD)

elements_volume_CFLAGS = \
	$(GST_BASE_CFLAGS) \
	$(CFLAGS) $(AM_CFLAGS)

libs_video_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_MAJORMINOR@.la \
	$(LDADD)
libs_video_CFLAGS = -I$(top_srcdir)/gst-libs $(CFLAGS) $(AM_CFLAGS)

EXTRA_DIST = gst-plugins-base.supp
