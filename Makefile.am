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

all-local: gst-element-check-@GST_MAJORMINOR@.m4

gst-element-check-@GST_MAJORMINOR@.m4: gst-element-check.m4
	cp $(srcdir)/gst-element-check.m4 gst-element-check-@GST_MAJORMINOR@.m4

ACLOCAL_AMFLAGS = -I common/m4

aclocaldir = $(datadir)/aclocal
aclocal_DATA = gst-element-check-@GST_MAJORMINOR@.m4

SUBDIRS = include gst libs tools \
	$(SUBDIRS_TESTS) $(SUBDIRS_EXAMPLES) \
	pkgconfig po \
	$(SUBDIRS_DOCS)

# These are all the possible subdirs
DIST_SUBDIRS = include libs gst \
	tools \
	tests testsuite \
	examples \
	pkgconfig \
	po \
	docs

EXTRA_DIST = gstreamer.spec.in gst-element-check.m4 \
	     configure.ac autogen.sh depcomp \
	     REQUIREMENTS ABOUT-NLS RELEASE DOCBUILDING DEVEL \
	     idiottest.mak common

CLEANFILES = gst-element-check-@GST_MAJORMINOR@.m4

dist-hook:
	cp gstreamer.spec $(distdir)
	rm -rf `find $(distdir)/common -name CVS`

rpm: dist
	rpm -ta $(distdir).tar.gz

include $(top_srcdir)/idiottest.mak

