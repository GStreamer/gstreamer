if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

SUBDIRS = gstreamer pkgconfig examples $(SUBDIRS_DOCS)

DIST_SUBDIRS = gstreamer pkgconfig examples docs

EXTRA_DIST = \
  gst-python.spec.in gst-python.spec

CLEANFILES = README NEWS TODO AUTHORS

snap:
	$(MAKE) dist distdir=$(PACKAGE)-`date +"%Y%m%d"`
