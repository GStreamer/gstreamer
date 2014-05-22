include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg
TEST_FILES_DIRECTORY = $(top_srcdir)/tests/files

GST_TOOLS_DIR = $(top_builddir)/tools

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY_1_0=$(CHECK_REGISTRY)

# GST_PLUGINS_XYZ_DIR is only set in an uninstalled setup
TESTS_ENVIRONMENT = \
	$(REGISTRY_ENVIRONMENT)                                 \
	GST_PLUGIN_SYSTEM_PATH_1_0=				\
	GST_PLUGIN_PATH_1_0=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(GST_PLUGINS_LIBAV_DIR):$(GST_PLUGINS_UGLY_DIR):$(GST_PLUGINS_GOOD_DIR):$(GST_PLUGINS_BASE_DIR):$(GST_PLUGINS_DIR) \
	GST_PLUGIN_LOADING_WHITELIST="gstreamer@$(GST_PLUGINS_DIR):gst-plugins-base@$(GSTPB_PLUGINS_DIR):gst-plugins-good:gst-plugins-ugly:gst-libav:gst-plugins-bad@$(top_builddir)" \
	GST_STATE_IGNORE_ELEMENTS="apexsink avdtpsrc camerabin dc1394src \
	    dccpclientsrc dccpclientsink dccpserversrc dccpserversink decklinksrc \
	    decklinksink dvbsrc dvbbasebin dfbvideosink festival fluidsynth gsettingsvideosrc \
	    gsettingsvideosink gsettingsaudiosrc gsettingsaudiosink linsyssdisrc linsyssdisink nassink \
	    rsndvdbin sdlaudiosink sdlvideosink vcdsrc rfbsrc vdpauyuvvideo vdpauvideoyuv \
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

if USE_MPG123
check_mpg123 = elements/mpg123audiodec
else
check_mpg123 =
endif

if USE_MPLEX
check_mplex = elements/mplex
else
check_mplex =
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

if USE_TIMIDITY
check_timidity=elements/timidity
else
check_timidity=
endif

if USE_KATE
check_kate=elements/kate
else
check_kate=
endif

if USE_MIMIC
check_mimic=pipelines/mimic
else
check_mimic=
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

if USE_OPUS
check_opus = elements/opus
else
check_opus =
endif

if USE_SSH2
check_curl_sftp = elements/curlsftpsink
else
check_curl_sftp =
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
    libs/gstglupload
else
check_gl=
endif

VALGRIND_TO_FIX = \
	elements/mpeg2enc \
	elements/mplex    \
	elements/zbar     \
	pipeline/colorspace

# valgrind testing
VALGRIND_TESTS_DISABLE = \
	$(VALGRIND_TO_FIX)

# these tests don't even pass
# neon: too flaky (almost always fails 'the first time')
# colorspace: bad memory accesses in orc code for odd width buffers - https://bugzilla.gnome.org/show_bug.cgi?id=663248
noinst_PROGRAMS = \
	pipelines/colorspace \
	pipelines/streamheader \
	$(check_neon)

check_PROGRAMS = \
	generic/states \
	$(check_assrender) \
	$(check_faac)  \
	$(check_faad)  \
	$(check_voaacenc) \
	$(check_voamrwbenc) \
	$(check_mpeg2enc)  \
	$(check_mplex)     \
	$(check_ofa)        \
	$(check_timidity)  \
	$(check_kate)  \
	$(check_opus)  \
	$(check_curl) \
	$(check_shm) \
	elements/aiffparse \
	elements/autoconvert \
	elements/autovideoconvert \
	elements/audiomixer \
	elements/asfmux \
	elements/baseaudiovisualizer \
	elements/camerabin \
	elements/dataurisrc \
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
	$(check_mpg123) \
	elements/mxfdemux \
	elements/mxfmux \
	elements/id3mux \
	pipelines/mxf \
	$(check_mimic) \
	libs/mpegvideoparser \
	libs/mpegts \
	libs/h264parser \
	libs/vp8parser \
	libs/aggregator \
	$(check_uvch264) \
	libs/vc1parser \
	$(check_schro) \
	elements/viewfinderbin \
	$(check_zbar) \
	$(check_orc) \
	libs/insertbin \
	$(check_gl) \
	$(EXPERIMENTAL_CHECKS)

noinst_HEADERS = elements/mxfdemux.h

TESTS = $(check_PROGRAMS)

AM_CFLAGS = $(GST_CFLAGS) $(GST_CHECK_CFLAGS) $(GST_OPTION_CFLAGS) \
		-DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
		-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS
LDADD = $(GST_CHECK_LIBS)

elements_audiomixer_LDADD = $(GST_BASE_LIBS) -lgstbase-@GST_API_VERSION@ $(LDADD)
elements_audiomixer_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)

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
	$(GST_CFLAGS)

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
	$(GST_PLUGINS_BAD_LIBS) -lgstcodecparsers-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

elements_faad_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_faad_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD) \
	-lgstaudio-@GST_API_VERSION@

elements_faac_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_faac_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD) \
	-lgstaudio-@GST_API_VERSION@

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
	-lgstaudio-@GST_API_VERSION@

elements_baseaudiovisualizer_SOURCES = elements/baseaudiovisualizer.c
elements_baseaudiovisualizer_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_baseaudiovisualizer_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@  \
	-lgstvideo-@GST_API_VERSION@ 	$(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) \
	$(GST_LIBS) $(LDADD)

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

elements_timidity_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_timidity_LDADD = $(GST_BASE_LIBS) $(LDADD)

elements_kate_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_kate_LDADD = $(GST_BASE_LIBS) $(LDADD)

elements_assrender_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_assrender_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

elements_mpegtsmux_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_mpegtsmux_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

elements_mpg123audiodec_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_mpg123audiodec_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD) \
	-lgstaudio-@GST_API_VERSION@ -lgstfft-@GST_API_VERSION@ -lgstapp-@GST_API_VERSION@

elements_uvch264demux_CFLAGS = -DUVCH264DEMUX_DATADIR="$(srcdir)/elements/uvch264demux_data" \
				$(AM_CFLAGS)

pipelines_streamheader_CFLAGS = $(GIO_CFLAGS) $(AM_CFLAGS)
pipelines_streamheader_LDADD = $(GIO_LIBS) $(LDADD)

libs_insertbin_LDADD = \
	$(top_builddir)/gst-libs/gst/insertbin/libgstinsertbin-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)
libs_insertbin_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)


EXTRA_DIST = gst-plugins-bad.supp $(uvch264_dist_data)

orc_bayer_CFLAGS = $(ORC_CFLAGS)
orc_bayer_LDADD = $(ORC_LIBS) -lorc-test-0.4

orc/bayer.c: $(top_srcdir)/gst/bayer/gstbayerorc.orc
	$(MKDIR_P) orc
	$(ORCC) --test -o $@ $<

orc_audiomixer_CFLAGS = $(ORC_CFLAGS)
orc_audiomixer_LDADD = $(ORC_LIBS) -lorc-test-0.4

orc/audiomixer.c: $(top_srcdir)/gst/audiomixer/gstaudiomixerorc.orc
	$(MKDIR_P) orc
	$(ORCC) --test -o $@ $<

libs_gstglcontext_LDADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

libs_gstglcontext_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

libs_gstglmemory_LDADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

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
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

libs_aggregator_LDADD = \
	$(top_builddir)/gst-libs/gst/base/libgstbadbase-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LDADD)

libs_aggregator_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)

elements_compositor_LDADD = $(LDADD)  $(GST_BASE_LIBS)
elements_compositor_CFLAGS = $(GST_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

orc_compositor_CFLAGS = $(ORC_CFLAGS)
orc_compositor_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_compositor_SOURCES = orc/compositor.c
orc_videobox_CFLAGS = $(ORC_CFLAGS)

orc/compositor.c: $(top_srcdir)/gst/compositor/compositororc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<


distclean-local-orc:
	rm -rf orc

