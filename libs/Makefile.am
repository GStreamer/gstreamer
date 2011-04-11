SUBDIRS = gst

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer \
	 -:SUBDIR libs/gst \
	> $@
