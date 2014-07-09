SUBDIRS = plugins

bin_PROGRAMS = codecanalyzer

codecanalyzer_SOURCES  =              \
	gst_analyzer.c                \
	xml_parse.c                   \
	codecanalyzer.c               \
	$(NULL)

noinst_HEADERS = gst_analyzer.h xml_parse.h

codecanalyzer_CFLAGS = \
	$(GLIB_CFLAGS)   		\
	$(GMODULE_EXPORT_CFLAGS)	\
	$(GTK_CFLAGS)			\
	$(GST_CFLAGS)			\
	$(GST_PBUTILS_CFLAGS)		\
	$(LIBXML2_CFLAGS)		\
	-I$(top_builddir)/src/plugins/gst/analyzersink \
	-I$(top_srcdir)/src/plugins/gst/analyzersink \
	-DDATADIR=\"$(datadir)\" \
	$(NULL)

codecanalyzer_LDADD = \
	$(GLIB_LIBS)   			\
	$(GMODULE_EXPORT_LIBS)		\
	$(GTK_LIBS)			\
	$(GST_LIBS)			\
	$(GST_PBUTILS_LIBS)		\
	$(top_builddir)/src/plugins/gst/analyzersink/libcodecanalyzer-gst-analyzersink.la \
	$(LIBXML2_LIBS)			\
	$(NULL)

codecanalyzer_LDFLAGS =           \
        $(GST_ALL_LDFLAGS)        \
        $(NULL)


# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
