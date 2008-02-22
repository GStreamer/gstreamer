include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg
TEST_FILES_DIRECTORY = $(top_srcdir)/tests/files

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/ext:$(top_builddir)/sys:$(GSTPB_PLUGINS_DIR):$(GST_PLUGINS_DIR)			\
	GST_TEST_FILES_PATH=$(TEST_FILES_DIRECTORY) \
	STATE_IGNORE_ELEMENTS="aasink autovideosink cacasink cairotextoverlay gconfaudiosrc gconfvideosrc v4l2src"

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

clean-local: clean-local-check

if USE_ANNODEX
check_annodex = \
	elements/cmmldec \
	elements/cmmlenc
else
check_annodex = 
endif

if USE_SUNAUDIO
check_sunaudio = elements/sunaudio
else
check_sunaudio = 
endif

if USE_TAGLIB
check_taglib = \
	elements/id3v2mux \
	elements/apev2mux
else
check_taglib =
endif

if USE_WAVPACK
check_wavpack = \
       elements/wavpackparse \
       elements/wavpackdec \
       elements/wavpackenc
else
check_wavpack =
endif

check_PROGRAMS = \
	generic/states \
	$(check_annodex) \
	elements/alphacolor \
	elements/audiopanorama \
	elements/audioinvert \
	elements/audiochebband \
	elements/audiocheblimit \
	elements/audioamplify \
	elements/audiodynamic \
	elements/audiowsincband \
	elements/audiowsinclimit \
	elements/avimux \
	elements/avisubtitle \
	elements/equalizer \
	elements/icydemux \
	elements/id3demux \
	elements/level \
	elements/matroskamux \
	elements/multifile \
	elements/rtp-payloading \
	elements/spectrum \
	elements/videocrop \
	elements/videofilter \
	pipelines/simple-launch-lines \
	$(check_sunaudio) \
	$(check_taglib) \
	$(check_wavpack)

VALGRIND_TO_FIX = \
	elements/rtp-payloading

TESTS = $(check_PROGRAMS)

# these tests don't even pass
# autodetect: temporarily disabled because of broken videosinks in -bad
noinst_PROGRAMS = \
	elements/autodetect

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS) $(GST_OPTION_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

# valgrind testing
# videocrop disabled since it takes way too long in valgrind
VALGRIND_TESTS_DISABLE = \
	elements/videocrop \
	$(VALGRIND_TO_FIX)

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-good.supp

elements_audiopanorama_LDADD = \
	$(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) \
	$(LDADD)

elements_audiopanorama_CFLAGS = \
	$(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) \
	$(CFLAGS) $(AM_CFLAGS)

elements_cmmldec_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
elements_cmmlenc_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

elements_alphacolor_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

elements_sunaudio_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(CFLAGS) \
	$(AM_CFLAGS)
elements_sunaudio_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-@GST_MAJORMINOR@ \
	$(LDADD)

elements_videocrop_LDADD = $(LDADD) $(GST_BASE_LIBS)
elements_videocrop_CFLAGS = $(GST_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

