include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg
TEST_FILES_DIRECTORY = $(top_srcdir)/tests/files

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY_1_0=$(CHECK_REGISTRY)

AM_TESTS_ENVIRONMENT += \
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH_1_0=				\
	GST_PLUGIN_PATH_1_0=$(top_builddir)/gst:$(top_builddir)/ext:$(top_builddir)/sys:$(GSTPB_PLUGINS_DIR):$(GST_PLUGINS_DIR)			\
	GST_PLUGIN_LOADING_WHITELIST="gstreamer@$(GST_PLUGINS_DIR):gst-plugins-base@$(GSTPB_PLUGINS_DIR):gst-plugins-good@$(top_builddir)"	\
	GST_STATE_IGNORE_ELEMENTS="aasink autoaudiosrc autoaudiosink autovideosrc autovideosink \
		cacasink cairotextoverlay \
		halaudiosrc halaudiosink jackaudiosrc jackaudiosink \
		osssrc osssink osxaudiosink osxaudiosrc osxvideosrc osxvideosink \
		pulsesink pulsesrc pulsemixer v4l2src" \
	GSETTINGS_BACKEND="memory"

# fake device drivers: we could run hardware element tests against dummy drivers
# v4l2: vivo (part of normal kernel)
#   modprobe vivo;
#   gst-launch v4l2src device="/dev/video1" ! xvimagesink;
#   rmmod vivo
#
# alsa: snd-dummy (part of normal alsa, not removable)
#   modprobe snd-dummy;
#   gst-launch alsasrc device="hw:2" ! fakesink
#   gst-launch fakesrc ! alsasink device="hw:2"
#
# need a way to figure out value for the device property

# the core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

clean-local: clean-local-check
distclean-local: distclean-local-orc

if USE_PLUGIN_ALPHA
check_alpha = \
	elements/alphacolor \
	elements/alpha
else
check_alpha =
endif

if USE_PLUGIN_AUDIOFX
check_audiofx = \
	elements/audioamplify \
	elements/audiochebband \
	elements/audiocheblimit \
	elements/audiodynamic \
	elements/audioecho \
	elements/audiofirfilter \
	elements/audioiirfilter \
	elements/audioinvert \
	elements/audiopanorama \
	elements/audiowsincband \
	elements/audiowsinclimit
else
check_audiofx =
endif

if USE_PLUGIN_AUDIOPARSERS
check_audioparsers = \
	elements/aacparse \
	elements/ac3parse \
	elements/amrparse \
	elements/flacparse \
	elements/mpegaudioparse \
	elements/wavpackparse
else
check_audioparsers =
endif

if USE_PLUGIN_AUTODETECT
check_autodetect = elements/autodetect
else
check_autodetect =
endif

if USE_PLUGIN_AVI
check_avi = \
  elements/avimux \
  elements/avisubtitle
else
check_avi =
endif

if USE_PLUGIN_DEBUGUTILS
check_debugutils = elements/capssetter
else
check_debugutils =
endif

if USE_PLUGIN_DEINTERLACE
check_deinterlace = elements/deinterlace
else
check_deinterlace =
endif

if USE_PLUGIN_DTMF
check_dtmf = elements/dtmf
else
check_dtmf =
endif

if USE_PLUGIN_EFFECTV
check_effectv = pipelines/effectv
else
check_effectv =
endif

if USE_PLUGIN_EQUALIZER
check_equalizer = elements/equalizer
else
check_equalizer =
endif

if USE_FLAC
check_flac = pipelines/flacdec
else
check_flac =
endif

if USE_PLUGIN_FLV
check_flv = \
        elements/flvdemux \
        elements/flvmux
else
check_flv =
endif

if USE_PLUGIN_ISOMP4
check_isomp4 = \
	elements/qtmux \
	elements/qtdemux
else
check_isomp4 =
endif

if USE_PLUGIN_LAW
check_law = \
	elements/mulawdec \
	elements/mulawenc
else
check_law =
endif

if USE_GDK_PIXBUF
check_gdkpixbuf = elements/gdkpixbufsink elements/gdkpixbufoverlay
else
check_gdkpixbuf =
endif

if USE_PLUGIN_ICYDEMUX
check_icydemux = elements/icydemux
else
check_icydemux =
endif

if USE_PLUGIN_ID3DEMUX
check_id3demux = elements/id3demux
else
check_id3demux =
endif

if USE_PLUGIN_IMAGEFREEZE
check_imagefreeze = elements/imagefreeze
else
check_imagefreeze =
endif

if USE_PLUGIN_INTERLEAVE
check_interleave = \
        elements/deinterleave \
        elements/interleave
else
check_interleave =
endif

if USE_JPEG
check_jpeg = \
        elements/jpegdec \
        elements/jpegenc
else
check_jpeg =
endif

if USE_PLUGIN_LEVEL
check_level = elements/level
else
check_level =
endif

if USE_PLUGIN_MATROSKA
check_matroska = \
	elements/matroskademux \
	elements/matroskamux \
	elements/matroskaparse
else
check_matroska =
endif

if USE_PLUGIN_MULTIFILE
check_multifile = elements/multifile elements/splitmux
else
check_multifile =
endif

if USE_PLUGIN_REPLAYGAIN
check_replaygain = \
	elements/rganalysis \
	elements/rglimiter \
	elements/rgvolume
else
check_replaygain =
endif

if USE_PLUGIN_RTP
check_rtp = \
	elements/rtp-payloading \
	elements/rtph261 \
	elements/rtph263 \
	elements/rtpvp9
else
check_rtp =
endif

if USE_PLUGIN_RTPMANAGER
check_rtpmanager = \
	elements/rtpaux \
	elements/rtpbin \
	elements/rtpbin_buffer_list \
	elements/rtpbundle \
	elements/rtpcollision \
	elements/rtpjitterbuffer \
	elements/rtpmux \
	elements/rtprtx \
	elements/rtpsession
else
check_rtpmanager =
endif

if USE_SOUP
check_soup = elements/souphttpsrc
else
check_soup =
endif

if USE_PLUGIN_SPECTRUM
check_spectrum = elements/spectrum
else
check_spectrum =
endif

if USE_SUNAUDIO
check_sunaudio = elements/sunaudio
else
check_sunaudio =
endif

if USE_PLUGIN_SHAPEWIPE
check_shapewipe = elements/shapewipe
else
check_shapewipe =
endif

if USE_TAGLIB
check_taglib = \
	elements/id3v2mux \
	elements/apev2mux
else
check_taglib =
endif

if USE_PLUGIN_UDP
check_udp = \
        elements/udpsink \
        elements/udpsrc
else
check_udp =
endif

if USE_PLUGIN_VIDEOBOX
check_videobox = elements/videobox
else
check_videobox =
endif

if USE_PLUGIN_VIDEOCROP
check_videocrop = \
	elements/aspectratiocrop \
	elements/videocrop
else
check_videocrop =
endif

if USE_PLUGIN_VIDEOFILTER
check_videofilter = elements/videofilter
else
check_videofilter =
endif

if USE_PLUGIN_VIDEOMIXER
check_videomixer = elements/videomixer
else
check_videomixer =
endif

if USE_VPX
check_vpx=elements/vp8enc elements/vp8dec elements/vp9enc
else
check_vpx=
endif

if USE_PLUGIN_WAVENC
check_wavenc = pipelines/wavenc
else
check_wavenc =
endif

if USE_WAVPACK
check_wavpack = \
       elements/wavpackdec \
       elements/wavpackenc \
       pipelines/wavpack
else
check_wavpack =
endif

if USE_PLUGIN_WAVPARSE
check_wavparse = elements/wavparse
else
check_wavparse =
endif

if USE_PLUGIN_Y4M
check_y4m = elements/y4menc
else
check_y4m =
endif

if HAVE_ORC
check_orc = orc/deinterlace orc/videomixer orc/videobox
else
check_orc =
endif


check_PROGRAMS = \
	generic/states \
	pipelines/simple-launch-lines \
	pipelines/tagschecking \
	$(check_alpha) \
	$(check_audiofx) \
	$(check_audioparsers) \
	$(check_autodetect) \
	$(check_avi) \
	$(check_debugutils) \
	$(check_deinterlace) \
	$(check_dtmf) \
	$(check_effectv) \
	$(check_equalizer) \
	$(check_flac) \
	$(check_flv) \
	$(check_gdkpixbuf) \
	$(check_isomp4) \
	$(check_icydemux) \
	$(check_id3demux) \
	$(check_imagefreeze) \
	$(check_interleave) \
	$(check_jpeg) \
	$(check_law) \
	$(check_level) \
	$(check_matroska) \
	$(check_multifile) \
	$(check_replaygain) \
	$(check_rtp) \
	$(check_rtpmanager) \
	$(check_shapewipe) \
	$(check_soup) \
	$(check_spectrum) \
	$(check_sunaudio) \
	$(check_taglib) \
	$(check_udp) \
	$(check_videobox) \
	$(check_videocrop) \
	$(check_videofilter) \
	$(check_videomixer) \
	$(check_vpx) \
	$(check_wavenc) \
	$(check_wavpack) \
	$(check_wavparse) \
	$(check_y4m) \
	$(check_orc)

VALGRIND_TO_FIX = \
	elements/rtp-payloading

TESTS = $(check_PROGRAMS)

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS) \
	$(GST_OPTION_CFLAGS) $(GST_CFLAGS) -DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
	-DGST_CHECK_TEST_ENVIRONMENT_BEACON="\"GST_PLUGIN_LOADING_WHITELIST\"" \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS $(PTHREAD_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

# valgrind testing
# videocrop disabled since it takes way too long in valgrind
VALGRIND_TESTS_DISABLE = \
	elements/videocrop \
	$(VALGRIND_TO_FIX)

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-good.supp

# parser unit test convenience lib
noinst_LTLIBRARIES = libparser.la
libparser_la_SOURCES = elements/parser.c elements/parser.h
libparser_la_CFLAGS = \
	-I$(top_srcdir)/tests/check \
	$(GST_CHECK_CFLAGS) $(GST_OPTION_CFLAGS) -DGST_USE_UNSTABLE_API

elements_aacparse_LDADD = libparser.la $(LDADD)

elements_ac3parse_LDADD = libparser.la $(LDADD)

elements_amrparse_LDADD = libparser.la $(LDADD)

elements_flacparse_LDADD = libparser.la $(LDADD)

elements_flvdemux_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_flvdemux_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) $(LDADD)

elements_mpegaudioparse_LDADD = libparser.la $(LDADD)

elements_aspectratiocrop_LDADD = $(LDADD)
elements_aspectratiocrop_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

elements_audioamplify_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_audioamplify_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)

elements_audiochebband_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_audiochebband_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD) $(LIBM)

elements_audiocheblimit_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_audiocheblimit_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD) $(LIBM)

elements_audiodynamic_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_audiodynamic_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)

elements_audioecho_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_audioecho_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)

elements_audioinvert_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_audioinvert_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)

elements_audiopanorama_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(GST_BASE_LIBS) \
	$(GST_CONTROLLER_LIBS) $(LDADD)

elements_audiopanorama_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) \
	$(CFLAGS) $(AM_CFLAGS)

elements_audiowsincband_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_audiowsincband_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD) $(LIBM)

elements_audiowsinclimit_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_audiowsinclimit_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD) $(LIBM)

elements_autodetect_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_autodetect_LDADD = $(GST_BASE_LIBS) $(LDADD)

elements_equalizer_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_equalizer_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD) $(LIBM)

elements_rganalysis_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_rganalysis_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)
elements_rglimiter_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_rglimiter_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)
elements_rgvolume_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_rgvolume_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD) $(LIBM)

elements_rtp_payloading_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_rtp_payloading_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

elements_spectrum_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_spectrum_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)

elements_alphacolor_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_alpha_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

elements_deinterlace_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_deinterlace_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(LDADD)

elements_dtmf_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_dtmf_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_API_VERSION@ \
    $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

elements_deinterleave_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_deinterleave_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)
elements_interleave_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_interleave_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)

elements_level_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_level_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)

elements_imagefreeze_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_imagefreeze_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

elements_jpegdec_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GIO_CFLAGS) $(AM_CFLAGS)
elements_jpegdec_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) -lgstpbutils-$(GST_API_VERSION) $(GST_BASE_LIBS) $(GIO_LIBS) $(LDADD)

elements_jpegenc_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_jpegenc_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

elements_matroskamux_LDADD = $(GST_BASE_LIBS) $(LDADD) $(LIBM)

elements_mulawdec_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

elements_mulawenc_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

elements_multifile_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_multifile_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(GST_LIBS) $(LDADD) $(LIBM)

elements_splitmux_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_splitmux_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_LIBS) $(LDADD) $(LIBM)

elements_qtmux_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_qtmux_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstpbutils-@GST_API_VERSION@ \
             $(GST_BASE_LIBS) $(GST_LIBS) $(GST_CHECK_LIBS)

elements_rtpbin_buffer_list_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(WARNING_CFLAGS) $(ERROR_CFLAGS) $(GST_CHECK_CFLAGS) $(AM_CFLAGS)
elements_rtpbin_buffer_list_LDADD = $(GST_PLUGINS_BASE_LIBS) \
             -lgstrtp-$(GST_API_VERSION) \
             $(GST_BASE_LIBS) $(GST_LIBS) $(GST_CHECK_LIBS) $(LDADD)
elements_rtpbin_buffer_list_SOURCES = elements/rtpbin_buffer_list.c

elements_rtph261_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_rtph261_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

elements_rtph263_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_rtph263_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

elements_rtpmux_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_rtpmux_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

elements_souphttpsrc_CFLAGS = $(SOUP_CFLAGS) $(AM_CFLAGS)
elements_souphttpsrc_LDADD = $(SOUP_LIBS) $(LDADD)

elements_sunaudio_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) \
	$(AM_CFLAGS)
elements_sunaudio_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(LDADD)

elements_udpsrc_CFLAGS = $(AM_CFLAGS) $(GIO_CFLAGS)
elements_udpsrc_LDADD = $(LDADD) $(GIO_LIBS)

elements_videocrop_LDADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(LDADD)
elements_videocrop_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

elements_videofilter_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_videofilter_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(LDADD)

elements_rtpjitterbuffer_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_rtpjitterbuffer_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-$(GST_API_VERSION) $(LDADD)

elements_rtprtx_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_rtprtx_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-$(GST_API_VERSION) $(LDADD)

elements_rtpsession_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_rtpsession_LDADD = $(GST_PLUGINS_BASE_LIBS) $(GST_NET_LIBS) -lgstrtp-$(GST_API_VERSION) $(GIO_LIBS) $(LDADD)

elements_rtpcollision_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_rtpcollision_LDADD = $(GST_PLUGINS_BASE_LIBS) $(GST_NET_LIBS) -lgstrtp-$(GST_API_VERSION) $(GIO_LIBS) $(LDADD)

elements_rtpaux_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_rtpaux_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-$(GST_API_VERSION) $(LDADD)

elements_rtpbundle_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_rtpbundle_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-$(GST_API_VERSION) $(LDADD)

# FIXME: configure should check for gdk-pixbuf not gtk
# only need video.h header, not the lib
elements_gdkpixbufsink_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS) $(GDK_PIXBUF_CFLAGS)
elements_gdkpixbufsink_LDADD = \
	$(LDADD) $(GDK_PIXBUF_LIBS)

elements_videomixer_LDADD = $(LDADD)  $(GST_BASE_LIBS)
elements_videomixer_CFLAGS = $(GST_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

pipelines_flacdec_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
pipelines_flacdec_LDADD  = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)

elements_vp9enc_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_vp9enc_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(LDADD)

pipelines_wavenc_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
pipelines_wavenc_LDADD  = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(LDADD)

pipelines_wavpack_LDADD = $(LDADD) $(GST_BASE_LIBS)
pipelines_wavpack_CFLAGS = $(GST_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

orc_deinterlace_CFLAGS = $(ORC_CFLAGS)
orc_deinterlace_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_deinterlace_SOURCES = orc/deinterlace.c
orc_videomixer_CFLAGS = $(ORC_CFLAGS)
orc_videomixer_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_videomixer_SOURCES = orc/videomixer.c
orc_videobox_CFLAGS = $(ORC_CFLAGS)
orc_videobox_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_videobox_SOURCES = orc/videobox.c

orc/deinterlace.c: $(top_srcdir)/gst/deinterlace/tvtime.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/videomixer.c: $(top_srcdir)/gst/videomixer/videomixerorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/videobox.c: $(top_srcdir)/gst/videobox/gstvideoboxorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

distclean-local-orc:
	rm -rf orc

EXTRA_DIST = \
	gst-plugins-good.supp	\
	elements/qtdemux.h
