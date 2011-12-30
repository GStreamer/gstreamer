SUBDIRS = elements

DIST_SUBDIRS = elements

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer \
	 -:SUBDIR $(patsubst %,plugins/%, $(SUBDIRS)) \
	> $@
