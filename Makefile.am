SUBDIRS = gst plugins test editor tools docs

bin_SCRIPTS = gstreamer-config

m4datadir = $(datadir)/aclocal
m4data_DATA = gstreamer.m4

EXTRA_DIST = gstreamer.spec.in gstreamer-config.in

rpm: dist
	rpm -ta $(distdir).tar.gz
