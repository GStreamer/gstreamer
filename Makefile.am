PACKAGE = @PACKAGE@
VERSION = @VERSION@

if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

if BUILD_TESTS
SUBDIRS_TESTS = tests testsuite
else
SUBDIRS_TESTS =
endif

if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

SUBDIRS = include gst libs tools \
	$(SUBDIRS_TESTS) $(SUBDIRS_EXAMPLES) $(SUBDIRS_DOCS)

# These are all the possible subdirs
DIST_SUBDIRS = include libs gst tools tests testsuite examples docs

all-local: gstreamer-@GST_MAJORMINOR@.pc gstreamer-@GST_MAJORMINOR@-uninstalled.pc

gstreamer-@GST_MAJORMINOR@.pc: gstreamer.pc gstreamer.pc.in
	cp gstreamer.pc gstreamer-@GST_MAJORMINOR@.pc
gstreamer-@GST_MAJORMINOR@-uninstalled.pc: gstreamer-uninstalled.pc gstreamer-uninstalled.pc.in
	cp gstreamer-uninstalled.pc gstreamer-@GST_MAJORMINOR@-uninstalled.pc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gstreamer-@GST_MAJORMINOR@.pc

EXTRA_DIST = gstreamer.spec.in \
	     gstreamer.pc.in \
	     gstreamer-uninstalled.pc.in \
	     configure.ac autogen.sh depcomp \
	     REQUIREMENTS ABOUT-NLS RELEASE DOCBUILDING DEVEL \
	     idiottest.mak common

dist-hook:
	cp gstreamer.spec $(distdir)
	rm -rf `find $(distdir)/common -name CVS`

rpm: dist
	rpm -ta $(distdir).tar.gz

include $(top_srcdir)/idiottest.mak

