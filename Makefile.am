if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

if GST_0_6
INTERFACESDIR =
PLAYDIR =
else
INTERFACESDIR = gstinterfaces
PLAYDIR = gstplay
endif

UNCONDDIRS = gstreamer pkgconfig examples testsuite
SUBDIRS = \
	$(UNCONDDIRS) \
	$(INTERFACESDIR) \
	$(PLAYDIR) \
	$(SUBDIRS_DOCS)
DIST_SUBDIRS = $(UNCONDDIRS) docs gstinterfaces gstplay

EXTRA_DIST = \
	gst-python.spec.in gst-python.spec README-docs

snap:
	$(MAKE) dist distdir=$(PACKAGE)-`date +"%Y%m%d"`
