SUBDIRS=
DIST_SUBDIRS=

$(TESTS):
	$(GST_TOOLS_DIR)/gst-register --gst-plugin-path=$(top_builddir)

TESTS=$(GST_TOOLS_DIR)/gst-compprep

.PHONY: $(TESTS)
