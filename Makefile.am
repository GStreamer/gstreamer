SUBDIRS = gstreamer pkgconfig examples docs

EXTRA_DIST = \
  gst-python.spec.in gst-python.spec

snap:
	$(MAKE) dist distdir=$(PACKAGE)-`date +"%Y%m%d"`
