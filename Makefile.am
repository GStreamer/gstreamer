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

EXTRA_DIST = gstreamer.spec.in gstreamer-config.in gstreamer.m4 LICENSE REQUIREMENTS 

dist-hook:
	cp gstreamer.spec $(distdir)

rpm: dist
	rpm -ta $(distdir).tar.gz
