include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg
TEST_FILES_DIRECTORY = $(top_srcdir)/tests/files

GST_TOOLS_DIR = $(top_builddir)/tools

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY_1_0=$(CHECK_REGISTRY)

# GST_PLUGINS_XYZ_DIR is only set in an uninstalled setup
AM_TESTS_ENVIRONMENT += \
	$(REGISTRY_ENVIRONMENT)                                 \
	GST_PLUGIN_SYSTEM_PATH_1_0=				\
	GST_PLUGIN_PATH_1_0=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(GST_PLUGINS_LIBAV_DIR):$(GST_PLUGINS_UGLY_DIR):$(GST_PLUGINS_GOOD_DIR):$(GST_PLUGINS_BASE_DIR):$(GST_PLUGINS_DIR) \
	GST_PLUGIN_LOADING_WHITELIST="gstreamer@$(GST_PLUGINS_DIR):gst-plugins-base@$(GSTPB_PLUGINS_DIR):gst-plugins-good:gst-plugins-ugly:gst-libav:gst-plugins-bad@$(top_builddir)" \
	GST_STATE_IGNORE_ELEMENTS="avdtpsrc camerabin dc1394src \
	    decklinksrc \
	    decklinksink dtlssrtpdec dtlssrtpenc dvbsrc dvbbasebin dfbvideosink festival fluidsynth \
	    kmssink \
	    rsndvdbin vcdsrc rfbsrc vdpauyuvvideo vdpauvideoyuv \
	    vdpaumpegdec vdpaumpeg4dec vdpauh264dec vdpauvideopostprocess vdpausink neonhttpsrc"

plugindir = $(libdir)/gstreamer-@GST_API_VERSION@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# the core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-bad.supp

clean-local: clean-local-check
distclean-local: distclean-local-orc

if USE_ASSRENDER
check_assrender = elements/assrender
else
check_assrender =
endif

if USE_DASH
check_dash = elements/dash_mpd elements/dash_isoff
check_dash_demux = elements/dash_demux
else
check_dash =
check_dash_demux =
endif

if USE_FAAC
check_faac = elements/faac
else
check_faac =
endif

if USE_FAAD
check_faad = elements/faad
else
check_faad =
endif

if USE_VOAACENC
check_voaacenc = elements/voaacenc
else
check_voaacenc =
endif

if USE_VOAMRWBENC
check_voamrwbenc = elements/voamrwbenc
else
check_voamrwbenc =
endif

if USE_EXIF
check_jifmux = elements/jifmux
else
check_jifmux =
endif

if USE_MPEG2ENC
check_mpeg2enc = elements/mpeg2enc
else
check_mpeg2enc =
endif

if USE_MPLEX
check_mplex = elements/mplex
else
check_mplex =
endif

if USE_SMOOTHSTREAMING
check_mssdemux = elements/mssdemux
else
check_mssdemux =
endif

if USE_NEON
check_neon = elements/neonhttpsrc
else
check_neon =
endif

if USE_OFA
check_ofa = elements/ofa
else
check_ofa =
endif

if USE_SCHRO
check_schro=elements/schroenc
else
check_schro=
endif

if USE_X265
check_x265enc=elements/x265enc
else
check_x265enc=
endif

if USE_KATE
check_kate=elements/kate
else
check_kate=
endif

if HAVE_ORC
check_orc = orc/bayer orc/audiomixer orc/compositor
else
check_orc =
endif

if USE_ZBAR
check_zbar = elements/zbar
else
check_zbar =
endif

if USE_OPENCV
check_opencv = elements/templatematch
else
check_opencv =
endif

if USE_SSH2
check_curl_sftp = elements/curlsftpsink
else
check_curl_sftp =
endif

if USE_HLS
check_hlsdemux_m3u8 = elements/hlsdemux_m3u8
check_hlsdemux = elements/hls_demux
else
check_hlsdemux_m3u8 =
check_hlsdemux =
endif

if USE_SRTP
check_srtp = elements/srtp
else
check_srtp =
endif

if WITH_GST_PLAYER_TESTS
check_player = libs/player
else
check_player =
endif

if USE_CURL
check_curl = elements/curlhttpsink \
	elements/curlfilesink \
	elements/curlftpsink \
	$(check_curl_sftp) \
	elements/curlsmtpsink
else
check_curl =
endif

if USE_UVCH264
check_uvch264=elements/uvch264demux
else
check_uvch264=
endif
uvch264_dist_data = elements/uvch264demux_data/valid_h264_jpg.mjpg \
			elements/uvch264demux_data/valid_h264_jpg.jpg \
			elements/uvch264demux_data/valid_h264_jpg.h264 \
			elements/uvch264demux_data/valid_h264_yuy2.mjpg \
			elements/uvch264demux_data/valid_h264_yuy2.h264 \
			elements/uvch264demux_data/valid_h264_yuy2.yuy2

if USE_SHM
check_shm=elements/shm
else
check_shm=
endif

if USE_GL
check_gl=libs/gstglcontext \
    libs/gstglmemory \
    libs/gstglupload \
    libs/gstglcolorconvert \
    libs/gstglquery \
    libs/gstglsl \
    elements/glimagesink \
    pipelines/simple-launch-lines
else
check_gl=
endif

VALGRIND_TO_FIX = \
	elements/mpeg2enc \
	elements/mplex    \
	elements/zbar

# valgrind testing
VALGRIND_TESTS_DISABLE = \
	$(VALGRIND_TO_FIX)

# these tests don't even pass
# neon: too flaky (almost always fails 'the first time')
# dashdemux: fails because it uses random data streams but dashdemux nowadays
#            actually looks at the data and doesn't like randomness
noinst_PROGRAMS = \
	pipelines/streamheader \
	$(check_dash_demux) \
	$(check_neon)

check_PROGRAMS = \
	generic/states \
	$(check_assrender) \
	$(check_dash) \
	$(check_faac)  \
	$(check_faad)  \
	$(check_voaacenc) \
	$(check_voamrwbenc) \
	$(check_mpeg2enc)  \
	$(check_mplex)     \
	$(check_mssdemux) \
	$(check_ofa)        \
	$(check_kate)  \
	$(check_opencv) \
	$(check_curl) \
	$(check_shm) \
	elements/aiffparse \
	elements/videoframe-audiolevel \
	elements/autoconvert \
	elements/autovideoconvert \
	elements/audiointerleave \
	elements/audiomixer \
	elements/asfmux \
	elements/camerabin \
	elements/gdppay \
	elements/gdpdepay \
	elements/compositor \
	$(check_jifmux) \
	elements/jpegparse \
	elements/h263parse \
	elements/h264parse \
	elements/mpegtsmux \
	elements/mpegvideoparse \
	elements/mpeg4videoparse \
	elements/mxfdemux \
	elements/mxfmux \
	elements/netsim \
	elements/pcapparse \
	elements/pnm \
	elements/rtponvifparse \
	elements/rtponviftimestamp \
	elements/id3mux \
	pipelines/mxf \
	libs/mpegvideoparser \
	libs/mpegts \
	libs/h264parser \
	libs/vp8parser \
	libs/aggregator \
	$(check_uvch264) \
	libs/vc1parser \
	$(check_schro) \
	$(check_x265enc) \
	elements/viewfinderbin \
	$(check_zbar) \
	$(check_orc) \
	libs/insertbin \
	$(check_gl) \
	$(check_hlsdemux_m3u8) \
	$(check_hlsdemux) \
	$(check_srtp) \
	$(check_player) \
	$(EXPERIMENTAL_CHECKS)

noinst_HEADERS = elements/mxfdemux.h elements/dash_isoff.h

TESTS = $(check_PROGRAMS)

AM_CFLAGS = $(GST_CFLAGS) $(GST_CHECK_CFLAGS) $(GST_OPTION_CFLAGS) \
	-DGST_CHECK_TEST_ENVIRONMENT_BEACON="\"GST_PLUGIN_LOADING_WHITELIST\"" \
	-DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS $(PTHREAD_CFLAGS)
LDADD = $(GST_CHECK_LIBS)

generic_states_CFLAGS = $(AM_CFLAGS) $(GMODULE_NO_EXPORT_CFLAGS)
generic_states_LDADD = $(LDADD) $(GMODULE_NO_EXPORT_LIBS)

elements_audiomixer_LDADD = $(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) -lgstbase-@GST_API_VERSION@ $(LDADD)
elements_audiomixer_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) $(AM_CFLAGS)

elements_audiointerleave_LDADD = $(GST_BASE_LIBS) -lgstbase-@GST_API_VERSION@ $(GST_AUDIO_LIBS) $(LDADD)
elements_audiointerleave_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)

elements_pnm_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_pnm_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)
#
# parser unit test convenience lib
noinst_LTLIBRARIES = libparser.la
libparser_la_SOURCES = elements/parser.c elements/parser.h
libparser_la_CFLAGS = \
	-I$(top_srcdir)/tests/check \
	$(GST_CFLAGS) $(GST_CHECK_CFLAGS) $(GST_OPTION_CFLAGS)

elements_mpegvideoparse_LDADD = libparser.la $(LDADD)

elements_mpeg4videoparse_LDADD = libparser.la $(LDADD)

elements_h263parse_LDADD = libparser.la $(LDADD)

elements_h264parse_LDADD = libparser.la $(LDADD)

elements_pcapparse_LDADD = libparser.la $(LDADD)

libs_mpegvideoparser_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_mpegvideoparser_LDADD = \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

libs_mpegts_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS) \
	$(PTHREAD_CFLAGS)

libs_mpegts_LDADD = \
	$(top_builddir)/gst-libs/gst/mpegts/libgstmpegts-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) $(LDADD)

libs_h264parser_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_h264parser_LDADD = \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

libs_vc1parser_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_vc1parser_LDADD = \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

libs_vp8parser_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_vp8parser_LDADD = \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

elements_videoframe_audiolevel_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_videoframe_audiolevel_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD) \
	$(GST_AUDIO_LIBS)

elements_faad_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_faad_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD) \
	$(GST_AUDIO_LIBS)

elements_faac_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_faac_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD) \
	$(GST_AUDIO_LIBS)

elements_gdppay_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_gdppay_LDADD =  $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

elements_gdpdepay_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_gdpdepay_LDADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

elements_voaacenc_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_voaacenc_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD) \
	$(GST_AUDIO_LIBS)

elements_camerabin_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS) -DGST_USE_UNSTABLE_API
elements_camerabin_LDADD = \
        $(top_builddir)/gst-libs/gst/interfaces/libgstphotography-@GST_API_VERSION@.la \
        $(top_builddir)/gst-libs/gst/basecamerabinsrc/libgstbasecamerabinsrc-@GST_API_VERSION@.la \
        $(GST_PLUGINS_BASE_LIBS) -lgstpbutils-$(GST_API_VERSION) \
        $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)
elements_camerabin_SOURCES = elements/camerabin.c

elements_jifmux_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(EXIF_CFLAGS) $(AM_CFLAGS)
elements_jifmux_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) $(GST_CHECK_LIBS) $(EXIF_LIBS) $(LDADD)
elements_jifmux_SOURCES = elements/jifmux.c

elements_kate_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_kate_LDADD = $(GST_BASE_LIBS) $(LDADD)

elements_assrender_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_assrender_LDADD = $(GST_PLUGINS_BASE_LIBS) $(GST_VIDEO_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

elements_mpegtsmux_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_mpegtsmux_LDADD = $(GST_PLUGINS_BASE_LIBS) $(GST_VIDEO_LIBS) $(GST_BASE_LIBS) $(LDADD)

elements_uvch264demux_CFLAGS = -DUVCH264DEMUX_DATADIR="$(srcdir)/elements/uvch264demux_data" \
				$(AM_CFLAGS)

elements_dash_mpd_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS) $(GST_PLUGINS_BAD_CFLAGS) $(LIBXML2_CFLAGS) \
	-DDASH_MPD_DATADIR="$(srcdir)/elements/dash_mpd_data"
elements_dash_mpd_LDADD = $(GST_BASE_LIBS) $(LDADD) $(LIBXML2_LIBS) \
	$(top_builddir)/gst-libs/gst/uridownloader/libgsturidownloader-@GST_API_VERSION@.la
elements_dash_mpd_SOURCES = elements/dash_mpd.c

elements_dash_isoff_CFLAGS = $(AM_CFLAGS) $(GST_BASE_CFLAGS)
elements_dash_isoff_LDADD = $(LDADD) $(GST_BASE_LIBS)
elements_dash_isoff_SOURCES = elements/dash_isoff.c

elements_dash_demux_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(AM_CFLAGS) $(LIBXML2_CFLAGS)
elements_dash_demux_LDADD = \
	$(top_builddir)/gst-libs/gst/uridownloader/libgsturidownloader-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/adaptivedemux/libgstadaptivedemux-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) -lgstapp-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(LIBXML2_LIBS) $(LDADD)

elements_dash_demux_SOURCES = elements/test_http_src.c elements/test_http_src.h elements/adaptive_demux_engine.c elements/adaptive_demux_engine.h elements/adaptive_demux_common.c elements/adaptive_demux_common.h elements/dash_demux.c

elements_neonhttpsrc_CFLAGS = $(AM_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)

elements_mssdemux_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(AM_CFLAGS) $(LIBXML2_CFLAGS)
elements_mssdemux_LDADD = \
	$(top_builddir)/gst-libs/gst/uridownloader/libgsturidownloader-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/adaptivedemux/libgstadaptivedemux-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) -lgstapp-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(LIBXML2_LIBS) $(LDADD)

elements_mssdemux_SOURCES = elements/test_http_src.c elements/test_http_src.h elements/adaptive_demux_engine.c elements/adaptive_demux_engine.h elements/adaptive_demux_common.c elements/adaptive_demux_common.h elements/mssdemux.c

pipelines_streamheader_CFLAGS = $(GIO_CFLAGS) $(AM_CFLAGS)
pipelines_streamheader_LDADD = $(GIO_LIBS) $(LDADD)

libs_insertbin_LDADD = \
	$(top_builddir)/gst-libs/gst/insertbin/libgstinsertbin-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)
libs_insertbin_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_player_SOURCES = libs/player.c

libs_player_LDADD = \
	$(top_builddir)/gst-libs/gst/player/libgstplayer-@GST_API_VERSION@.la \
	$(GST_VIDEO_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) \
	$(LDADD)
libs_player_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS) \
	-DTEST_PATH=\"$(builddir)/media\"

if WITH_GST_PLAYER_TESTS
PLAYER_MEDIA_FILES = \
	media/audio.ogg \
	media/audio-video.ogg \
	media/audio-short.ogg \
	media/audio-video-short.ogg \
	media/sintel.mkv \
	media/test_sub.srt

$(PLAYER_MEDIA_FILES):
	$(MKDIR_P) media
	$(WGET) -c http://gstreamer.freedesktop.org/data/media/small/$(subst media/,,$@) -O media/$(subst media/,,$@)

libs/player_dummy.c: $(PLAYER_MEDIA_FILES)
	touch libs/player_dummy.c

nodist_libs_player_SOURCES = libs/player_dummy.c

CLEANFILES += $(PLAYER_MEDIA_FILES) libs/player_dummy.c
endif

elements_rtponvifparse_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_rtponvifparse_LDADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) -lgstrtp-$(GST_API_VERSION) $(LDADD)

elements_rtponviftimestamp_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_rtponviftimestamp_LDADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) -lgstrtp-$(GST_API_VERSION) $(LDADD)

EXTRA_DIST = gst-plugins-bad.supp $(uvch264_dist_data)

orc_bayer_CFLAGS = $(ORC_CFLAGS)
orc_bayer_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_bayer_SOURCES = orc/bayer.c

orc/bayer.c: $(top_srcdir)/gst/bayer/gstbayerorc.orc
	$(MKDIR_P) orc
	$(ORCC) --test -o $@ $<

orc_audiomixer_CFLAGS = $(ORC_CFLAGS)
orc_audiomixer_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_audiomixer_SOURCES = orc/audiomixer.c

orc/audiomixer.c: $(top_srcdir)/gst/audiomixer/gstaudiomixerorc.orc
	$(MKDIR_P) orc
	$(ORCC) --test -o $@ $<

libs_gstglcontext_LDADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_VIDEO_LIBS) $(GST_BASE_LIBS) \
	$(GST_LIBS) $(LDADD)

libs_gstglcontext_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_gstglmemory_LDADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_VIDEO_LIBS) $(GST_BASE_LIBS) \
	$(GST_LIBS) $(LDADD)

libs_gstglmemory_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_gstglupload_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_gstglupload_LDADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_VIDEO_LIBS) $(GST_BASE_LIBS) \
	$(GST_LIBS) $(LDADD)

libs_gstglcolorconvert_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_gstglcolorconvert_LDADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_VIDEO_LIBS) $(GST_BASE_LIBS) \
	$(GST_LIBS) $(LDADD)

libs_gstglquery_LDADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

libs_gstglquery_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_gstglsl_LDADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

libs_gstglsl_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

elements_glimagesink_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

elements_glimagesink_LDADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_VIDEO_LIBS) $(GST_BASE_LIBS) \
	$(GST_LIBS) $(LDADD)

pipelines_simple_launch_lines_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

pipelines_simple_launch_lines_LDADD = \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

libs_aggregator_LDADD = \
	$(top_builddir)/gst-libs/gst/base/libgstbadbase-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

libs_aggregator_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

elements_compositor_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_VIDEO_LIBS) $(GST_BASE_LIBS) $(LDADD)
elements_compositor_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

elements_hlsdemux_m3u8_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS) -I$(top_srcdir)/ext/hls
elements_hlsdemux_m3u8_LDADD = $(GST_BASE_LIBS) $(LDADD)
elements_hlsdemux_m3u8_SOURCES = elements/hlsdemux_m3u8.c

elements_hls_demux_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_hls_demux_LDADD = \
	$(top_builddir)/gst-libs/gst/adaptivedemux/libgstadaptivedemux-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) -lgstapp-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(LDADD)
elements_hls_demux_SOURCES = elements/test_http_src.c elements/test_http_src.h elements/adaptive_demux_engine.c elements/adaptive_demux_engine.h elements/adaptive_demux_common.c elements/adaptive_demux_common.h elements/hls_demux.c

orc_compositor_CFLAGS = $(ORC_CFLAGS)
orc_compositor_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_compositor_SOURCES = orc/compositor.c
orc_videobox_CFLAGS = $(ORC_CFLAGS)

orc/compositor.c: $(top_srcdir)/gst/compositor/compositororc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<


distclean-local-orc:
	rm -rf orc

