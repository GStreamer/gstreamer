SUBDIRS = include gst libs plugins tools test tests examples docs

# if libglade is present, build the player and editor
if HAVE_LIBGLADE_GNOME
SUBDIRS += gstplay editor
endif

# These are all the possible subdirs
DIST_SUBDIRS = include gst libs plugins tools test tests examples docs gstplay editor


bin_SCRIPTS = gstreamer-config

m4datadir = $(datadir)/aclocal
m4data_DATA = gstreamer.m4

EXTRA_DIST = gstreamer.spec.in gstreamer-config.in gstreamer.m4 LICENSE

dist-hook:
	cp gstreamer.spec $(distdir)

rpm: dist
	rpm -ta $(distdir).tar.gz
