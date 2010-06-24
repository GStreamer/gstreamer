include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg
TEST_FILES_DIRECTORY = $(top_srcdir)/tests/files

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        CK_DEFAULT_TIMEOUT=120                                  \
        STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(GST_PLUGINS_DIR) \
	GST_PLUGIN_LOADING_WHITELIST="gstreamer:gst-plugins-base@$(top_builddir)"


# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

clean-local: clean-local-check clean-local-orc

$(CHECK_REGISTRY):
	$(TESTS_ENVIRONMENT)

# elements to ignore for the state tests
STATE_IGNORE_ELEMENTS = cdio cdparanoiasrc libvisual_ alsasrc alsamixer alsasink

TESTS = $(check_PROGRAMS)

if USE_ALSA
check_alsa = elements/alsa
else
check_alsa =
endif

if USE_GNOME_VFS
check_gnomevfs = elements/gnomevfssink
else
check_gnomevfs = 
endif

if USE_GIO
check_gio = pipelines/gio
else
check_gio =
endif

if USE_LIBVISUAL
check_libvisual = elements/libvisual
else
check_libvisual =
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
check_vorbis = elements/vorbisdec pipelines/vorbisenc pipelines/vorbisdec \
	elements/vorbistag
else
check_vorbis =
endif

if USE_THEORA
check_theora = pipelines/theoraenc
else
check_theora =
endif

if USE_PLUGIN_SUBPARSE
check_subparse = elements/subparse
else
check_subparse =
endif

if HAVE_ORC
check_orc = orc/adder orc/audioconvert orc/volume orc/videoscale orc/videotestsrc
else
check_orc =
endif

check_PROGRAMS = \
	$(check_alsa) \
	$(check_gnomevfs) \
	$(check_gio) \
	$(check_ogg) \
	$(check_pango) \
	$(check_vorbis) \
	$(check_theora) \
	elements/adder	\
	elements/appsink	\
	elements/audioconvert \
	elements/audiorate \
	elements/audioresample \
	elements/audiotestsrc \
	elements/decodebin \
	elements/decodebin2 \
	elements/ffmpegcolorspace \
	elements/gdpdepay \
	elements/gdppay \
	elements/multifdsink \
	elements/playbin \
	elements/playbin2 \
	$(check_subparse) \
	elements/videorate \
	elements/videoscale \
	elements/videotestsrc \
	elements/volume \
	generic/clock-selection \
	generic/states \
	gst/typefindfunctions \
	libs/audio \
	libs/cddabasesrc \
	libs/fft \
	libs/mixer \
	libs/navigation \
	libs/netbuffer \
	libs/pbutils \
	libs/rtp \
	libs/tag \
	libs/video \
	$(check_orc) \
	pipelines/simple-launch-lines \
	pipelines/streamheader \
	pipelines/basetime \
	pipelines/capsfilter-renegotiation

# TORTURE_TO_FIX = \
#       elements/adder

# ffmpegcolorspace takes too long, so disabled for now
VALGRIND_TO_FIX = \
	elements/ffmpegcolorspace \
	libs/video

# these tests don't even pass
noinst_PROGRAMS = $(check_libvisual)

AM_CFLAGS = $(GST_CFLAGS) $(GST_CHECK_CFLAGS) \
	-DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\""
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

libs_fft_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_fft_LDADD = \
	$(top_builddir)/gst-libs/gst/fft/libgstfft-@GST_MAJORMINOR@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_mixer_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
        $(GST_BASE_CFLAGS) \
        $(AM_CFLAGS)

libs_mixer_LDADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
        $(GST_BASE_LIBS) \
        $(LDADD)

libs_navigation_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
        $(GST_BASE_CFLAGS) \
        $(AM_CFLAGS)

libs_navigation_LDADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
        $(GST_BASE_LIBS) \
        $(LDADD)

libs_netbuffer_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_netbuffer_LDADD = \
	$(top_builddir)/gst-libs/gst/netbuffer/libgstnetbuffer-@GST_MAJORMINOR@.la \
	$(LDADD)

libs_rtp_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_rtp_LDADD = \
	$(top_builddir)/gst-libs/gst/rtp/libgstrtp-@GST_MAJORMINOR@.la $(LDADD)

libs_tag_CFLAGS = \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_tag_LDADD = \
	$(GST_BASE_LIBS) \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_MAJORMINOR@.la $(LDADD)

libs_pbutils_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_pbutils_LDADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la $(LDADD)

elements_appsink_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_appsink_LDADD = \
	$(top_builddir)/gst-libs/gst/app/libgstapp-@GST_MAJORMINOR@.la \
	$(LDADD)

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

elements_libvisual_LDADD =  $(LDADD)
elements_libvisual_CFLAGS = $(CFLAGS) $(AM_CFLAGS)

elements_gdpdepay_LDADD = $(GST_GDP_LIBS) $(LDADD)
elements_gdppay_LDADD = $(GST_GDP_LIBS) $(LDADD)

elements_playbin_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_playbin_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_playbin2_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_playbin2_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_decodebin_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_decodebin_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_decodebin2_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_decodebin2_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_subparse_LDADD =  $(LDADD)
elements_subparse_CFLAGS = $(CFLAGS) $(AM_CFLAGS)

elements_audioresample_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_audioresample_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

elements_textoverlay_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_textoverlay_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_volume_LDADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_CONTROLLER_LIBS) \
	$(GST_BASE_LIBS) \
	$(LDADD)

elements_volume_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CONTROLLER_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_vorbisdec_LDADD = \
	$(LDADD) \
	$(VORBIS_LIBS)

elements_vorbisdec_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS) \
	$(VORBIS_CFLAGS) \
	$(CFLAGS)

elements_vorbistag_LDADD = \
	$(LDADD) \
	$(VORBIS_LIBS)

elements_vorbistag_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS) \
	$(VORBIS_CFLAGS) \
	$(CFLAGS)

gst_typefindfunctions_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)
gst_typefindfunctions_LDADD = $(GST_BASE_LIBS) $(LDADD)

libs_video_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_video_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_MAJORMINOR@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

pipelines_gio_CFLAGS = $(GIO_CFLAGS) $(AM_CFLAGS)
pipelines_gio_LDADD = $(GIO_LIBS) $(LDADD)

pipelines_vorbisenc_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) \
        $(AM_CFLAGS)

# this seemingly useless CFLAGS line is here only to avoid
# vorbisdec.$(OBJEXT) by triggering creation of pipelines_vorbisdec.$(OBJEXT)
# instead
pipelines_vorbisdec_CFLAGS = $(AM_CFLAGS)

pipelines_oggmux_LDADD = $(LDADD) $(OGG_LIBS)
pipelines_oggmux_CFLAGS = $(AM_CFLAGS) $(OGG_CFLAGS)

pipelines_theoraenc_CFLAGS = $(AM_CFLAGS) $(THEORA_CFLAGS)
pipelines_theoraenc_LDADD = $(LDADD) $(THEORA_LIBS)

pipelines_simple_launch_lines_CFLAGS = \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

pipelines_simple_launch_lines_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

orc_adder_CFLAGS = $(ORC_CFLAGS)
orc_adder_LDADD = $(ORC_LIBS) -lorc-test-0.4
orc_audioconvert_CFLAGS = $(ORC_CFLAGS)
orc_audioconvert_LDADD = $(ORC_LIBS) -lorc-test-0.4
orc_volume_CFLAGS = $(ORC_CFLAGS)
orc_volume_LDADD = $(ORC_LIBS) -lorc-test-0.4
orc_videoscale_CFLAGS = $(ORC_CFLAGS)
orc_videoscale_LDADD = $(ORC_LIBS) -lorc-test-0.4
orc_videotestsrc_CFLAGS = $(ORC_CFLAGS)
orc_videotestsrc_LDADD = $(ORC_LIBS) -lorc-test-0.4

orc/adder.c: $(top_srcdir)/gst/adder/gstadderorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/audioconvert.c: $(top_srcdir)/gst/audioconvert/gstaudioconvertorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/volume.c: $(top_srcdir)/gst/volume/gstvolumeorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/videoscale.c: $(top_srcdir)/gst/videoscale/gstvideoscaleorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/videotestsrc.c: $(top_srcdir)/gst/videotestsrc/gstvideotestsrcorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<


clean-local-orc:
	rm -rf orc

EXTRA_DIST = gst-plugins-base.supp
