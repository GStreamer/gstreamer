if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

if GST_0_6
INTERFACESDIR =
else
INTERFACESDIR = gstinterfaces
endif

UNCONDDIRS = gstreamer pkgconfig examples testsuite
SUBDIRS = $(UNCONDDIRS) $(SUBDIRS_DOCS) $(INTERFACESDIR)
DIST_SUBDIRS = $(UNCONDDIRS) docs gstinterfaces

EXTRA_DIST = \
	gst-python.spec.in gst-python.spec README-docs

snap:
	$(MAKE) dist distdir=$(PACKAGE)-`date +"%Y%m%d"`
