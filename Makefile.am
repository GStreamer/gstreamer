PACKAGE = @PACKAGE@
VERSION = @VERSION@

# if libglade and gnome1 are present, build the player and editor
# if libglade and gnome2, only the player
# otherwise none

if HAVE_LIBGLADE_GNOME
if HAVE_GNOME
SUBDIRS_LGG = gstplay editor
else
SUBDIRS_LGG = gstplay
endif
else
SUBDIRS_LGG =
endif
 
if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

if BUILD_TESTS
SUBDIRS_TESTS = test tests testsuite
else
SUBDIRS_TESTS =
endif

if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

SUBDIRS = include gst libs plugins tools $(SUBDIRS_TESTS) $(SUBDIRS_EXAMPLES) \
$(SUBDIRS_LGG) $(SUBDIRS_DOCS)

# These are all the possible subdirs
DIST_SUBDIRS = include gst libs plugins tools test tests testsuite examples gstplay editor docs debian


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gstreamer.pc

EXTRA_DIST = gstreamer.spec.in \
	     gstreamer.pc.in gstreamer-uninstalled.pc.in \
	     configure.ac autogen.sh depcomp \
	     LICENSE REQUIREMENTS ABOUT-NLS \
	     idiottest.mak

dist-hook:
	cp gstreamer.spec $(distdir)

rpm: dist
	rpm -ta $(distdir).tar.gz

deb:
	dpkg-buildpacakge -rfakeroot -us -uc

deb-maint:
	debuild

include $(top_srcdir)/idiottest.mak

