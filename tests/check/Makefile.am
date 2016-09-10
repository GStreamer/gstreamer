include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg
TEST_FILES_DIRECTORY = $(top_srcdir)/tests/files

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY_1_0=$(CHECK_REGISTRY)

AM_TESTS_ENVIRONMENT += \
	GST_STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH_1_0=				\
	GST_PLUGIN_PATH_1_0=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(GST_PLUGINS_DIR) \
	GST_PLUGIN_LOADING_WHITELIST="gstreamer:gst-plugins-base@$(top_builddir)" \
	GST_TAG_LICENSE_TRANSLATIONS_DICT="$(top_srcdir)/gst-libs/gst/tag/license-translations.dict"


# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

clean-local: clean-local-check

distclean-local: distclean-local-orc

$(CHECK_REGISTRY):
	$(AM_TESTS_ENVIRONMENT)

# elements to ignore for the state tests
STATE_IGNORE_ELEMENTS = cdio cdparanoiasrc libvisual_ alsasrc alsasink

TESTS = $(check_PROGRAMS)

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

if USE_OPUS
check_opus = elements/opus
else
check_opus =
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

if USE_PLUGIN_AUDIOTESTSRC
check_audiotestsrc = elements/audiotestsrc
else
check_audiotestsrc =
endif

if USE_PLUGIN_AUDIOCONVERT
check_audioconvert = elements/audioconvert
else
check_audioconvert =
endif

if USE_PLUGIN_PLAYBACK
check_playback = elements/decodebin elements/playbin \
    elements/playbin-complex elements/streamsynchronizer \
    elements/playsink
else
check_playback =
endif

if USE_THEORA
check_theora = pipelines/theoraenc
else
check_theora =
endif

if USE_VORBIS
if USE_THEORA
check_encodebin = elements/encodebin
else
check_encodebin =
endif
else
check_encodebin =
endif

if USE_PLUGIN_TYPEFIND
check_typefind = gst/typefindfunctions
else
check_typefind =
endif

if USE_PLUGIN_SUBPARSE
check_subparse = elements/subparse
else
check_subparse =
endif

if USE_PLUGIN_VIDEOCONVERT
check_videoconvert = elements/videoconvert
else
check_videoconvert =
endif

if USE_PLUGIN_VIDEORATE
check_videorate = elements/videorate
else
check_videorate =
endif

if USE_PLUGIN_VIDEOSCALE
check_videoscale = elements/videoscale elements/videoscale-1 \
        elements/videoscale-2 elements/videoscale-3 elements/videoscale-4 \
        elements/videoscale-5 elements/videoscale-6
else
check_videoscale =
endif

if USE_PLUGIN_VIDEOTESTSRC
check_videotestsrc = elements/videotestsrc
else
check_videotestsrc =
endif

if USE_PLUGIN_VOLUME
check_volume = elements/volume
else
check_volume =
endif

if USE_PLUGIN_ADDER
check_adder = elements/adder
else
check_adder =
endif

if HAVE_ORC
check_orc = orc/video orc/audio orc/adder orc/volume orc/videotestsrc
else
check_orc =
endif

if USE_PLUGIN_GIO
check_gio = pipelines/gio
else
check_gio =
endif

if USE_PLUGIN_APP
check_app = elements/appsink elements/appsrc
else
check_app =
endif

if USE_PLUGIN_AUDIORATE
check_audiorate = elements/audiorate
else
check_audiorate =
endif

if USE_PLUGIN_TCP
if USE_PLUGIN_APP
check_tcp = elements/multifdsink elements/multisocketsink pipelines/tcp
else
check_tcp = elements/multifdsink elements/multisocketsink
endif
else
check_tcp =
endif

if USE_PLUGIN_AUDIORESAMPLE
check_audioresample = elements/audioresample
else
check_audioresample =
endif

if HAVE_CXX
cxx_checks = libs/gstlibscpp
else
cxx_checks =
endif

check_PROGRAMS = \
	generic/clock-selection \
	generic/states \
	libs/libsabi \
	libs/allocators \
	libs/audio \
	libs/audiocdsrc \
	libs/audiodecoder \
	libs/audioencoder \
	libs/baseaudiovisualizer \
	libs/discoverer \
	libs/fft \
	libs/navigation \
	libs/pbutils \
	libs/profile \
	libs/mikey \
	libs/rtp \
	libs/rtpbasedepayload \
	libs/rtpbasepayload \
	libs/rtsp \
	libs/rtspconnection \
	libs/sdp \
	libs/tag \
	libs/video \
	libs/videodecoder \
	libs/videoencoder \
	libs/videotimecode \
	libs/xmpwriter \
	pipelines/simple-launch-lines \
	pipelines/basetime \
	pipelines/capsfilter-renegotiation \
	pipelines/streamsynchronizer \
	$(check_adder) \
	$(check_app) \
	$(check_audioconvert) \
	$(check_audiorate) \
	$(check_audioresample) \
	$(check_audiotestsrc) \
	$(check_encodebin) \
	$(check_gio) \
	$(check_ogg) \
	$(check_opus) \
	$(check_pango) \
	$(check_playback) \
	$(check_subparse) \
	$(check_tcp) \
	$(check_theora) \
	$(check_typefind) \
	$(check_videoconvert) \
	$(check_videorate) \
	$(check_videoscale) \
	$(check_videotestsrc) \
	$(check_volume) \
	$(check_vorbis) \
	$(cxx_checks) \
	$(check_orc)

# TORTURE_TO_FIX = \
#       elements/adder

# videoconvert takes too long, so disabled for now
VALGRIND_TO_FIX = \
	$(check_orc)

# these tests don't even pass
noinst_PROGRAMS = $(check_libvisual)

noinst_HEADERS = \
	libs/struct_arm.h     \
	libs/struct_i386.h     \
	libs/struct_i386_osx.h \
	libs/struct_ppc32.h \
	libs/struct_ppc64.h \
	libs/struct_x86_64.h

AM_CFLAGS = -I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_CFLAGS) $(GST_CHECK_CFLAGS) \
	-DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
	-DGST_CHECK_TEST_ENVIRONMENT_BEACON="\"GST_PLUGIN_LOADING_WHITELIST\"" \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS $(PTHREAD_CFLAGS)
AM_CXXFLAGS = -I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_CXXFLAGS) $(GST_CHECK_CFLAGS) \
	-DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
	-DGST_CHECK_TEST_ENVIRONMENT_BEACON="\"GST_PLUGIN_LOADING_WHITELIST\"" \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS $(PTHREAD_CFLAGS)
LDADD = $(GST_LIBS) $(GST_CHECK_LIBS)

# valgrind testing
VALGRIND_TESTS_DISABLE = $(VALGRIND_TO_FIX)

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-base.supp

libs_libsabi_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_libsabi_LDADD = \
	$(top_builddir)/gst-libs/gst/app/libgstapp-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/fft/libgstfft-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/rtp/libgstrtp-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/rtsp/libgstrtsp-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_allocators_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_allocators_LDADD = \
	$(top_builddir)/gst-libs/gst/allocators/libgstallocators-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_audio_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_audio_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_audiodecoder_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_audiodecoder_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_audioencoder_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_audioencoder_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_audiocdsrc_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_audiocdsrc_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_baseaudiovisualizer_SOURCES = libs/baseaudiovisualizer.c
libs_baseaudiovisualizer_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) \
        $(GST_CFLAGS) $(AM_CFLAGS)
libs_baseaudiovisualizer_LDADD = \
        $(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
        $(GST_LIBS) $(LDADD)

libs_discoverer_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS) \
	-DGST_TEST_FILE="\"$(abs_top_srcdir)/tests/files/partialframe.mjpeg\""
libs_discoverer_LDADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

libs_fft_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_fft_LDADD = \
	$(top_builddir)/gst-libs/gst/fft/libgstfft-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_navigation_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
        $(GST_BASE_CFLAGS) \
        $(AM_CFLAGS)

libs_navigation_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
        $(GST_BASE_LIBS) \
        $(LDADD)

libs_sdp_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_sdp_LDADD = \
	$(top_builddir)/gst-libs/gst/sdp/libgstsdp-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

libs_rtp_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_rtp_LDADD = \
	$(top_builddir)/gst-libs/gst/rtp/libgstrtp-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

libs_mikey_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_mikey_LDADD = \
	$(top_builddir)/gst-libs/gst/sdp/libgstsdp-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

libs_rtpbasepayload_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_rtpbasepayload_LDADD = \
	$(top_builddir)/gst-libs/gst/rtp/libgstrtp-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

libs_rtpbasedepayload_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_rtpbasedepayload_LDADD = \
	$(top_builddir)/gst-libs/gst/rtp/libgstrtp-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

libs_rtsp_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_rtsp_LDADD = \
	$(top_builddir)/gst-libs/gst/rtsp/libgstrtsp-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

libs_rtspconnection_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GIO_CFLAGS) \
	$(AM_CFLAGS)
libs_rtspconnection_LDADD = \
	$(top_builddir)/gst-libs/gst/rtsp/libgstrtsp-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GIO_LIBS) $(LDADD)

libs_tag_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_tag_LDADD = \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

libs_pbutils_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_pbutils_LDADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

libs_profile_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)
libs_profile_LDADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la $(LDADD)

libs_xmpwriter_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
        $(GST_BASE_CFLAGS) \
        $(AM_CFLAGS)

libs_xmpwriter_LDADD = \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
        $(GST_BASE_LIBS) \
        $(LDADD)


libs_gstlibscpp_SOURCES = libs/gstlibscpp.cc

elements_appsink_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_appsink_LDADD = \
	$(top_builddir)/gst-libs/gst/app/libgstapp-@GST_API_VERSION@.la \
	$(LDADD)

elements_appsrc_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_appsrc_LDADD = \
	$(top_builddir)/gst-libs/gst/app/libgstapp-@GST_API_VERSION@.la \
	$(LDADD)

elements_audioconvert_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_audioconvert_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

elements_audiorate_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)
elements_audiorate_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

elements_libvisual_LDADD =  $(LDADD)
elements_libvisual_CFLAGS = $(CFLAGS) $(AM_CFLAGS)

elements_playbin_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_playbin_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_playbin_complex_LDADD = $(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la $(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la $(GST_BASE_LIBS) $(LDADD)
elements_playbin_complex_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_decodebin_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_decodebin_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_encodebin_LDADD = $(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la $(GST_BASE_LIBS) $(LDADD)
elements_encodebin_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_subparse_LDADD =  $(LDADD)
elements_subparse_CFLAGS = $(CFLAGS) $(AM_CFLAGS)

elements_adder_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_adder_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_audioresample_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_audioresample_LDADD = \
	$(top_builddir)/gst-libs/gst/fft/libgstfft-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

elements_textoverlay_LDADD = $(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la $(GST_BASE_LIBS) $(LDADD)
elements_textoverlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_volume_LDADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
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
	$(VORBIS_LIBS) \
	$(VORBISENC_LIBS)

elements_vorbisdec_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS) \
	$(VORBIS_CFLAGS) \
	$(CFLAGS)

elements_vorbistag_LDADD = \
	$(LDADD) \
	$(VORBIS_LIBS) \
	$(VORBISENC_LIBS)

elements_vorbistag_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS) \
	$(VORBIS_CFLAGS) \
	$(CFLAGS)

elements_videoconvert_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)
elements_videoconvert_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

elements_videoscale_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)
elements_videoscale_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(LDADD)

elements_videoscale_1_SOURCES = elements/videoscale.c
elements_videoscale_1_CFLAGS = $(elements_videoscale_CFLAGS) -DVSCALE_TEST_GROUP=1
elements_videoscale_1_LDADD = $(elements_videoscale_LDADD)
elements_videoscale_2_SOURCES = elements/videoscale.c
elements_videoscale_2_CFLAGS = $(elements_videoscale_CFLAGS) -DVSCALE_TEST_GROUP=2
elements_videoscale_2_LDADD = $(elements_videoscale_LDADD)
elements_videoscale_3_SOURCES = elements/videoscale.c
elements_videoscale_3_CFLAGS = $(elements_videoscale_CFLAGS) -DVSCALE_TEST_GROUP=3
elements_videoscale_3_LDADD = $(elements_videoscale_LDADD)
elements_videoscale_4_SOURCES = elements/videoscale.c
elements_videoscale_4_CFLAGS = $(elements_videoscale_CFLAGS) -DVSCALE_TEST_GROUP=4
elements_videoscale_4_LDADD = $(elements_videoscale_LDADD)
elements_videoscale_5_SOURCES = elements/videoscale.c
elements_videoscale_5_CFLAGS = $(elements_videoscale_CFLAGS) -DVSCALE_TEST_GROUP=5
elements_videoscale_5_LDADD = $(elements_videoscale_LDADD)
elements_videoscale_6_SOURCES = elements/videoscale.c
elements_videoscale_6_CFLAGS = $(elements_videoscale_CFLAGS) -DVSCALE_TEST_GROUP=6
elements_videoscale_6_LDADD = $(elements_videoscale_LDADD)

gst_typefindfunctions_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)
gst_typefindfunctions_LDADD = $(GST_BASE_LIBS) $(LDADD)

libs_video_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_video_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_videodecoder_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_videodecoder_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_videoencoder_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_videoencoder_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_videotimecode_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(AM_CFLAGS)

libs_videotimecode_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

elements_multisocketsink_CFLAGS = $(GIO_CFLAGS) $(AM_CFLAGS)
elements_multisocketsink_LDADD = $(GIO_LIBS) $(LDADD)

if USE_GIO_UNIX_2_0
GIO_UNIX_2_0_DEFINED=-DHAVE_GIO_UNIX_2_0=1
endif

pipelines_tcp_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_NET_CFLAGS) $(GIO_CFLAGS) $(GIO_UNIX_2_0_CFLAGS) $(GIO_UNIX_2_0_DEFINED) $(AM_CFLAGS)
pipelines_tcp_LDADD = $(GST_PLUGINS_BASE_LIBS) $(GST_NET_LIBS) $(GIO_LIBS) $(LDADD) \
    $(GIO_UNIX_2_0_LIBS) $(top_builddir)/gst-libs/gst/app/libgstapp-@GST_API_VERSION@.la

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
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

orc_video_CFLAGS = $(ORC_CFLAGS)
orc_video_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_video_SOURCES = orc/video.c
orc_audio_CFLAGS = $(ORC_CFLAGS)
orc_audio_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_audio_SOURCES = orc/audio.c
orc_adder_CFLAGS = $(ORC_CFLAGS)
orc_adder_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_adder_SOURCES = orc/adder.c
orc_volume_CFLAGS = $(ORC_CFLAGS)
orc_volume_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_volume_SOURCES = orc/volume.c
orc_videotestsrc_CFLAGS = $(ORC_CFLAGS)
orc_videotestsrc_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_videotestsrc_SOURCES = orc/videotestsrc.c

orc/video.c: $(top_srcdir)/gst-libs/gst/video/video-orc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/audio.c: $(top_srcdir)/gst-libs/gst/audio/gstaudiopack.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/adder.c: $(top_srcdir)/gst/adder/gstadderorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/volume.c: $(top_srcdir)/gst/volume/gstvolumeorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/videotestsrc.c: $(top_srcdir)/gst/videotestsrc/gstvideotestsrcorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<


distclean-local-orc:
	rm -rf orc

EXTRA_DIST = gst-plugins-base.supp
