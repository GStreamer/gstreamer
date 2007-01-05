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

if USE_OGG
check_ogg = pipelines/oggmux
else
check_ogg = 
endif

if USE_PANGO
check_pango = elements/textoverlay
else
check_pango = 
endif

if USE_VORBIS
check_vorbis = elements/vorbisdec pipelines/vorbisenc elements/vorbistag
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
	$(check_ogg) \
	$(check_pango) \
	$(check_vorbis) \
	$(check_theora) \
	elements/adder	\
	elements/audioconvert \
	elements/audiorate \
	elements/audioresample \
	elements/audiotestsrc \
	elements/decodebin \
	elements/ffmpegcolorspace \
	elements/gdpdepay \
	elements/gdppay \
	elements/multifdsink \
	elements/playbin \
	elements/subparse \
	elements/videorate \
	elements/videotestsrc \
	elements/volume \
	generic/clock-selection \
	generic/states \
	libs/audio \
	libs/cddabasesrc \
	libs/tag \
	libs/video \
	pipelines/simple-launch-lines

# TORTURE_TO_FIX = \
#       elements/adder

# ffmpegcolorspace takes too long, so disabled for now
VALGRIND_TO_FIX = \
	elements/alsa \
	elements/audioresample \
	elements/ffmpegcolorspace \
	generic/states \
	libs/video

# these tests don't even pass
noinst_PROGRAMS =

AM_CFLAGS = $(GST_CFLAGS) $(GST_CHECK_CFLAGS)
LDADD = $(GST_LIBS) $(GST_CHECK_LIBS)

# valgrind testing
VALGRIND_TESTS_DISABLE = $(VALGRIND_TO_FIX)

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-base.supp

libs_audio_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_audio_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_cddabasesrc_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_cddabasesrc_LDADD = \
	$(top_builddir)/gst-libs/gst/cdda/libgstcdda-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_MAJORMINOR@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_tag_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_tag_LDADD = \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_MAJORMINOR@.la $(LDADD)

elements_alsa_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_alsa_LDADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(LDADD)

elements_audioconvert_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_audioconvert_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

elements_audiorate_LDADD =  $(LDADD)
elements_audiorate_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

elements_gdpdepay_LDADD = $(GST_GDP_LIBS) $(LDADD)
elements_gdppay_LDADD = $(GST_GDP_LIBS) $(LDADD)

elements_playbin_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_playbin_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_decodebin_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_decodebin_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_subparse_LDADD =  $(LDADD)
elements_subparse_CFLAGS = $(CFLAGS) $(AM_CFLAGS)

elements_textoverlay_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_textoverlay_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_volume_LDADD = \
	$(GST_BASE_LIBS) \
	$(LDADD)

elements_volume_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_vorbistag_LDADD = \
	$(VORBIS_LIBS) \
	$(LDADD)

elements_vorbistag_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(VORBIS_CFLAGS) \
	$(CFLAGS) $(AM_CFLAGS)

libs_video_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_video_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_MAJORMINOR@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

pipelines_oggmux_LDADD = $(LDADD) $(OGG_LIBS)
pipelines_oggmux_CFLAGS = $(AM_CFLAGS) $(OGG_CFLAGS)

pipelines_simple_launch_lines_CFLAGS = \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

pipelines_simple_launch_lines_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

EXTRA_DIST = gst-plugins-base.supp
