if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

SUBDIRS = gstreamer pkgconfig examples $(SUBDIRS_DOCS) testsuite

DIST_SUBDIRS = gstreamer pkgconfig examples docs testsuite

EXTRA_DIST = \
	gst-python.spec.in gst-python.spec README-docs

snap:
	$(MAKE) dist distdir=$(PACKAGE)-`date +"%Y%m%d"`
