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
DIST_SUBDIRS = include gst libs tools m4 tests testsuite examples docs


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gstreamer.pc

EXTRA_DIST = gstreamer.spec.in \
	     gstreamer.pc.in gstreamer-uninstalled.pc.in \
	     configure.ac autogen.sh depcomp \
	     LICENSE REQUIREMENTS ABOUT-NLS RELEASE \
	     idiottest.mak

dist-hook:
	cp gstreamer.spec $(distdir)

rpm: dist
	rpm -ta $(distdir).tar.gz

include $(top_srcdir)/idiottest.mak

