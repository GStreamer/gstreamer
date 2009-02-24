
plugin_LTLIBRARIES = libgstidentity2.la

GOB_FILES_ID = gst-identity2.c gst-identity2.h gst-identity2-private.h

BUILT_SOURCES =                                         \
	$(GOB_FILES_ID)

libgstidentity2_la_SOURCES = gst-identity2.gob $(GOB_FILES_ID)
libgstidentity2_la_CFLAGS = $(GST_CFLAGS)
libgstidentity2_la_LIBADD =

%.c %.h %-private.h: %.gob
	gob $<

CLEANFILES = $(GOB_FILES_ID)

dist-hook:
	cd $(distdir); rm -f $(CLEANFILES)
