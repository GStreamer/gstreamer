PACKAGE = @PACKAGE@
VERSION = @VERSION@

# if libglade is present, build the player and editor
if HAVE_LIBGLADE_GNOME
SUBDIRS_LGG = gstplay editor
else
SUBDIRS_LGG =
endif

if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

SUBDIRS = include gst libs plugins tools test tests testsuite examples \
$(SUBDIRS_LGG) $(SUBDIRS_DOCS)

# These are all the possible subdirs
DIST_SUBDIRS = include gst libs plugins tools test tests testsuite examples gstplay editor docs debian


bin_SCRIPTS = gstreamer-config

m4datadir = $(datadir)/aclocal
m4data_DATA = gstreamer.m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gstreamer.pc

man_MANS = gstreamer-config.1

EXTRA_DIST = gstreamer.spec.in gstreamer-config.in gstreamer.m4 \
	     gstreamer.pc.in gstreamer-uninstalled.pc.in \
	     LICENSE REQUIREMENTS ABOUT-NLS $(man_MANS)

dist-hook:
	cp gstreamer.spec $(distdir)

rpm: dist
	rpm -ta $(distdir).tar.gz

# Idiot test to stop the installing of versions with plugin srcdir enabled
install-exec-hook:
if PLUGINS_USE_SRCDIR
	@echo "*** ERROR: Cannot install:" 2>&1
	@echo "GStreamer was configured using the --enable-plugin-srcdir option." 2>&1
	@echo
	@echo "This option is for development purposes only: binaries built with" 2>&1
	@echo "it should be used with code in the build tree only.  To build an"
	@echo "installable version, use ./configure without the --enable-plugin-srcdir"
	@echo "option.  Note that the autogen.sh script supplies the plugin srcdir"
	@echo "option automatically - it cannot be used to configure installable builds"
	@echo
	@/bin/false
endif
