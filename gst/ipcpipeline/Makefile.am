plugin_LTLIBRARIES = libgstipcpipeline.la

libgstipcpipeline_la_SOURCES = \
	gstipcpipeline.c \
	gstipcpipelinecomm.c  \
	gstipcpipelinesink.c \
	gstipcpipelinesrc.c \
	gstipcslavepipeline.c

noinst_HEADERS = \
	gstipcpipelinecomm.h  \
	gstipcpipelinesink.h \
	gstipcpipelinesrc.h \
	gstipcslavepipeline.h

libgstipcpipeline_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstipcpipeline_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)

libgstipcpipeline_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
