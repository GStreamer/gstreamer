include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg
TEST_FILES_DIRECTORY = $(top_srcdir)/tests/files

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY_1_0=$(CHECK_REGISTRY)

if USE_OMX_TARGET_BELLAGIO
  OMX_CONFIG_DIR = $(top_srcdir)/config/bellagio
endif
if USE_OMX_TARGET_RPI
  OMX_CONFIG_DIR = $(top_srcdir)/config/rpi
endif
if USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  OMX_CONFIG_DIR = $(top_srcdir)/config/zynqultrascaleplus
endif
if USE_OMX_TARGET_TIZONIA
  OMX_CONFIG_DIR = $(top_srcdir)/config/tizonia
endif

AM_TESTS_ENVIRONMENT += \
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH_1_0=				\
	GST_PLUGIN_PATH_1_0=$(top_builddir)/omx:$(GSTPB_PLUGINS_DIR):$(GST_PLUGINS_DIR)			\
  GST_PLUGIN_LOADING_WHITELIST="gstreamer@$(GST_PLUGINS_DIR):gst-plugins-base@$(GSTPB_PLUGINS_DIR):gst-omx@$(top_builddir)" \
	GST_STATE_IGNORE_ELEMENTS="" \
	GST_OMX_CONFIG_DIR=$(OMX_CONFIG_DIR)

# the core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

clean-local: clean-local-check
distclean-local: distclean-local-orc

check_PROGRAMS = \
	generic/states

TESTS = $(check_PROGRAMS)

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS) \
	$(GST_OPTION_CFLAGS) $(GST_CFLAGS) -DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
	-DGST_CHECK_TEST_ENVIRONMENT_BEACON="\"GST_PLUGIN_LOADING_WHITELIST\"" \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS $(PTHREAD_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)
