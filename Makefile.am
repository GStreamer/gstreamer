
if HAVE_LIBGLADE_GNOME
# The following line mustn't be indented - automake is _very_ fussy..
gstplay_subdir=gstplay
else
# The following line mustn't be indented - automake is _very_ fussy..
gstplay_subdir=
endif

# These are the subdirs which might get used.
SUBDIRS = gst libs plugins . $(gstplay_subdir) test editor tools docs # tests

# These are all the possible subdirs
DIST_SUBDIRS = gst libs plugins . gstplay test editor tools docs tests

bin_SCRIPTS = gstreamer-config

m4datadir = $(datadir)/aclocal
m4data_DATA = gstreamer.m4

EXTRA_DIST = gstreamer.spec.in gstreamer-config.in gstreamer.m4 LICENSE

dist-hook:
	cp gstreamer.spec $(distdir)

rpm: dist
	rpm -ta $(distdir).tar.gz
