SUBDIRS = gst libs plugins test editor tools docs tests

bin_SCRIPTS = gstreamer-config

m4datadir = $(datadir)/aclocal
m4data_DATA = gstreamer.m4

EXTRA_DIST = gstreamer.spec.in gstreamer-config.in gstreamer.m4 LICENSE

dist-hook:
	cp gstreamer.spec $(distdir)

rpm: dist
	rpm -ta $(distdir).tar.gz
