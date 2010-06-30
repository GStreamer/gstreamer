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
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(top_builddir)/../gst-ffmpeg/ext/ffmpeg:$(top_builddir)/../gst-plugins-good/gst:$(top_builddir)/../gst-plugins-good/sys:$(top_builddir)/../gst-plugins-good/ext:$(top_builddir)/../gst-plugins-ugly/gst:$(top_builddir)/../gst-plugins-ugly/sys:$(top_builddir)/../gst-plugins-ugly/ext:$(GSTPB_PLUGINS_DIR):$(GST_PLUGINS_DIR) \
	GST_PLUGIN_LOADING_WHITELIST="gstreamer@$(GST_PLUGINS_DIR):gst-plugins-base@$(GSTPB_PLUGINS_DIR):gst-plugins-good:gst-plugins-ugly:gst-plugins-bad@$(top_builddir)" \
	STATE_IGNORE_ELEMENTS="alsaspdifsink apexsink camerabin cdaudio dc1394src dccpclientsrc dccpclientsink dccpserversrc dccpserversink dvbsrc dvbbasebin dfbvideosink festival nassink rsndvdbin sdlaudiosink sdlvideosink vcdsrc rfbsrc vdpauyuvvideo vdpauvideoyuv vdpaumpegdec neonhttpsrc"

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# the core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-bad.supp

clean-local: clean-local-check clean-local-orc

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

if USE_EXIF
check_jifmux = elements/jifmux
else
check_jifmux =
endif

if USE_METADATA
check_metadata = pipelines/metadata
else
check_metadata =
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
check_orc = orc/cog
else
check_orc =
endif


VALGRIND_TO_FIX = \
	elements/mpeg2enc \
	elements/mplex

# valgrind testing
VALGRIND_TESTS_DISABLE = \
	$(VALGRIND_TO_FIX)

# these tests don't even pass
# camerabin: temporarily disabled because it fails with current core -git
# (which will be release before the next -bad release though, and chances are
# changes to fix this are needed in the camerabin test)
# neon: too flaky (almost always fails 'the first time')
noinst_PROGRAMS = \
	$(check_neon)

check_PROGRAMS = \
	generic/states \
	$(check_assrender) \
	$(check_faac)  \
	$(check_faad)  \
	$(check_mpeg2enc)  \
	$(check_mplex)     \
	$(check_ofa)        \
	$(check_timidity)  \
	$(check_kate)  \
	elements/aacparse \
	elements/amrparse \
	elements/autoconvert \
	elements/asfmux \
	elements/camerabin \
	elements/dataurisrc \
	elements/legacyresample \
        $(check_jifmux) \
	elements/jpegparse \
	elements/qtmux \
	elements/selector \
	elements/mxfdemux \
	elements/mxfmux \
	pipelines/mxf \
	$(check_metadata) \
	$(check_mimic) \
	elements/rtpmux \
	$(check_orc) \
        pipelines/tagschecking

noinst_HEADERS = elements/mxfdemux.h elements/amrparse_data.h elements/aacparse_data.h

TESTS = $(check_PROGRAMS)

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS) $(GST_OPTION_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

elements_camerabin_CFLAGS = \
        -I$(top_builddir)/gst-libs \
        $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
        $(GST_PLUGINS_BAD_CFLAGS) $(GST_CHECK_CFLAGS) \
        -DGST_USE_UNSTABLE_API
elements_camerabin_LDADD = \
        $(top_builddir)/gst-libs/gst/interfaces/libgstphotography-@GST_MAJORMINOR@.la \
        $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(GST_CHECK_LIBS) \
         -lgstinterfaces-@GST_MAJORMINOR@
elements_camerabin_SOURCES = elements/camerabin.c

elements_jifmux_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS) $(GST_OPTION_CFLAGS) $(AM_CFLAGS) $(EXIF_CFLAGS)
elements_jifmux_LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS) $(EXIF_LIBS) $(LDADD)
elements_jifmux_SOURCES = elements/jifmux.c

elements_timidity_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_timidity_LDADD = $(GST_BASE_LIBS) $(LDADD)

elements_kate_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_kate_LDADD = $(GST_BASE_LIBS) $(LDADD)

elements_rtpmux_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_rtpmux_LDADD = $(GST_BASE_LIBS) $(LDADD) -lgstrtp-0.10

elements_assrender_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
elements_assrender_LDADD = $(GST_BASE_LIBS) $(LDADD) -lgstvideo-0.10 -lgstapp-0.10

EXTRA_DIST = gst-plugins-bad.supp

orc_cog_CFLAGS = $(ORC_CFLAGS)
orc_cog_LDADD = $(ORC_LIBS) -lorc-test-0.4

orc/cog.c: $(top_srcdir)/ext/cog/gstcogorc.orc
	$(MKDIR_P) orc
	$(ORCC) --test -o $@ $<

clean-local-orc:
	rm -rf orc

