bin_SCRIPTS = gst-launch-ext gst-visualise

man_MANS = gst-launch-ext.1 gst-visualise.1

all: all-am chmod

EXTRA_DIST = $(bin_SCRIPTS) $(man_MANS) gst-launch-ext.in

chmod: $(bin_SCRIPTS)
	chmod +x $^
