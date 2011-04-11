SUBDIRS = elements indexers

DIST_SUBDIRS = elements indexers

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer \
	 -:SUBDIR $(patsubst %,plugins/%, $(SUBDIRS)) \
	> $@
