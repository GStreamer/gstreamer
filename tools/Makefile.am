
templatefiles=\
	element-templates/audiofilter \
	element-templates/audiosink \
	element-templates/audiosrc \
	element-templates/base \
	element-templates/baseaudiosink \
	element-templates/baseaudiosrc \
	element-templates/basertpdepayload \
	element-templates/basertppayload \
	element-templates/basesink \
	element-templates/basesrc \
	element-templates/basetransform \
	element-templates/cddabasesrc \
	element-templates/element \
	element-templates/gobject \
	element-templates/pushsrc \
	element-templates/sinkpad \
	element-templates/sinkpad-simple \
	element-templates/srcpad \
	element-templates/srcpad-simple \
	element-templates/tagdemux \
	element-templates/videofilter2 \
	element-templates/videosink

EXTRA_DIST = \
	gst-element-maker \
	gst-app-maker \
	$(templatefiles) \
	gst-element-maker-test.sh

TESTS_ENVIRONMENT = \
	SRC_DIR=$(abs_srcdir) \
	TEMPLATE_FILES="$(templatefiles)"

TESTS = gst-element-maker-test.sh

