if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

UNCONDDIRS = gst pkgconfig testsuite
SUBDIRS = \
	$(UNCONDDIRS) \
	$(SUBDIRS_DOCS)
DIST_SUBDIRS = $(UNCONDDIRS) docs

EXTRA_DIST = \
	gst-python.spec.in \
	gst-python.spec \
	README-docs \
	RELEASE

include $(top_srcdir)/common/release.mak
