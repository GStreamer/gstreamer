GOB_FILES	= \
		bonobo-media-gstreamer.gob bonobo-media-gstreamervideo.gob

GOB_SOURCE	= \
		bonobo-media-gstreamer.h bonobo-media-gstreamer-private.h bonobo-media-gstreamer.c \
		bonobo-media-gstreamervideo.h bonobo-media-gstreamervideo.c bonobo-media-gstreamervideo-private.h

OAF_FILES	= bonobo-media-gstreamer.oafinfo

INCLUDES	= \
		-I. \
		-I$(srcdir) -I$(top_srcdir) -I$(top_builddir) \
		-I$(top_srcdir)/bonobo-media \
		-I$(includedir) \
		-I$(GNOME_CFLAGS) $(GNOME_INCLUDEDIR) \
		-I$(EXTRA_BONOBO_CFLAGS) $(EXTRA_WARNING_CFLAGS) \
		$(GSTREAMER_CFLAGS) 

if STREAM_PROVIDERS_GSTREAMER
bin_PROGRAMS	= bonobo-media-gstreamer
endif

bonobo_media_gstreamer_SOURCES = \
			$(GOB_FILES) $(GOB_SOURCE) \
			bonobo-media-gstreamer-factory.c

bonobo_media_gstreamer_LDADD = \
			$(EXTRA_GNOME_LIBS) $(EXTRA_BONOBO_LIBS) \
			-lpthread \
			-lgstmediaplay -lgst\
			$(top_builddir)/bonobo-media/libbonobo-media.la

.c %.h %-private.h: $(srcdir)/%.gob
	@GOB@ $<

oafdir			= $(datadir)/oaf
oaf_DATA		= $(OAF_FILES)



BUILT_SOURCES           = $(GOB_SOURCE)
CLEANFILES              += $(BUILT_SOURCES)

EXTRA_DIST		= $(OAF_FILES)
