if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

#PLAYDIR = gstplay
PLAYDIR =

UNCONDDIRS = gst pkgconfig examples testsuite
SUBDIRS = \
	$(UNCONDDIRS) \
	$(PLAYDIR) \
	$(SUBDIRS_DOCS)
DIST_SUBDIRS = $(UNCONDDIRS) docs $(PLAYDIR)

EXTRA_DIST = \
	gst-python.spec.in gst-python.spec README-docs

snap:
	$(MAKE) dist distdir=$(PACKAGE)-`date +"%Y%m%d"`
