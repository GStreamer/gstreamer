if USE_ALSA
ALSA_DIR=alsa
else
ALSA_DIR=
endif

SUBDIRS=spider $(ALSA_DIR) #seeking
DIST_SUBDIRS=spider alsa

GST_PLUGIN_PATH=$(shell cd $(top_builddir) && pwd)

#$(TESTS):
#	@echo -e '\nrunning gst-register...\n'
#	$(GST_TOOLS_DIR)/gst-register --gst-plugin-path=$(GST_PLUGIN_PATH)

#TESTS=$(GST_TOOLS_DIR)/gst-compprep

.PHONY: $(TESTS)
