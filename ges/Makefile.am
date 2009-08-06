
lib_LTLIBRARIES = libges-@GST_MAJORMINOR@.la

CLEANFILES = $(BUILT_SOURCES)

libges_@GST_MAJORMINOR@_la_SOURCES = 		\
	ges.c					\
	ges-simple-timeline-layer.c		\
	ges-timeline.c				\
	ges-timeline-layer.c			\
	ges-timeline-object.c			\
	ges-timeline-pipeline.c			\
	ges-timeline-source.c			\
	ges-timeline-transition.c		\
	ges-track.c				\
	ges-track-object.c

libges_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/ges/
libges_@GST_MAJORMINOR@include_HEADERS = 	\
	ges.h					\
	ges-simple-timeline-layer.h		\
	ges-timeline.h				\
	ges-timeline-layer.h			\
	ges-timeline-object.h			\
	ges-timeline-pipeline.h			\
	ges-timeline-source.h			\
	ges-timeline-transition.h		\
	ges-track.h				\
	ges-track-object.h


libges_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libges_@GST_MAJORMINOR@_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) 
libges_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

