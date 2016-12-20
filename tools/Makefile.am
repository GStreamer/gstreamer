
templatefiles=\
	element-templates/audiodecoder \
	element-templates/audioencoder \
	element-templates/audiofilter \
	element-templates/audiosink \
	element-templates/audiosrc \
	element-templates/baseparse \
	element-templates/basesink \
	element-templates/basesrc \
	element-templates/basetransform \
	element-templates/element \
	element-templates/sinkpad-audio \
	element-templates/sinkpad-template \
	element-templates/sinkpad-template-video \
	element-templates/srcpad-audio \
	element-templates/srcpad-template \
	element-templates/srcpad-template-video \
	element-templates/videodecoder \
	element-templates/videoencoder \
	element-templates/videofilter \
	element-templates/videosink

templatedeps = \
	element-templates/gobject \
	element-templates/sinkpad \
	element-templates/sinkpad-simple \
	element-templates/srcpad \
	element-templates/srcpad-simple

EXTRA_DIST = \
	gst-element-maker \
	gst-app-maker \
	gst-project-maker \
	$(templatefiles) \
	$(templatedeps) \
	gst-element-maker-test.sh

# Disabled since it doesn't work in an uninstalled setup:
#
#TESTS_ENVIRONMENT = \
#	SRC_DIR=$(abs_srcdir) \
#	TEMPLATE_FILES="$(templatefiles)" \
#	CPPFLAGS="-I$(abs_srcdir)"
#
#TESTS = gst-element-maker-test.sh

