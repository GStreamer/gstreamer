if USE_ALSA
ALSA_DIR=alsa
else
ALSA_DIR=
endif

if HAVE_GTK
EMBED_DIR=embed
else
EMBED_DIR=
endif

SUBDIRS = \
	$(EMBED_DIR) \
	$(ALSA_DIR) #seeking
DIST_SUBDIRS = \
	embed \
	alsa

GST_PLUGIN_PATH=$(shell cd $(top_builddir) && pwd)

#$(TESTS):
#	@echo -e '\nrunning gst-register...\n'
#	$(GST_TOOLS_DIR)/gst-register --gst-plugin-path=$(GST_PLUGIN_PATH)

#TESTS=$(GST_TOOLS_DIR)/gst-compprep

.PHONY: $(TESTS)
