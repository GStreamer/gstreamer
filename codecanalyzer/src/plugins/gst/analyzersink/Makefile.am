noinst_LTLIBRARIES =                            \
        libcodecanalyzer-gst-analyzersink.la               \
        $(NULL)

noinst_HEADERS = gstanalyzersink.h mpeg_xml.h xml_utils.h analyzer_utils.h

libcodecanalyzer_gst_analyzersink_cflags =                          \
        -DGST_USE_UNSTABLE_API                           \
        $(GST_BASE_CFLAGS)                               \
	$(GST_VIDEO_CFLAGS)                              \
        $(GST_CFLAGS)                                    \
	$(GST_CODEC_PARSERS_CFLAGS)                      \
        $(LIBXML2_CFLAGS)                                \
        $(NULL)

libcodecanalyzer_gst_analyzersink_libs =                   \
        $(GST_BASE_LIBS)                        \
        $(GST_LIBS)                             \
        $(GST_VIDEO_LIBS)                       \
        $(GST_CODEC_PARSERS_LIBS)               \
        $(LIBXML2_LIBS)                         \
        $(NULL)

libcodecanalyzer_gst_analyzersink_la_SOURCES =  \
        gstanalyzersink.c            \
        mpeg_xml.c                   \
        plugin.c                     \
	analyzer_utils.c	     \
        $(NULL)

libcodecanalyzer_gst_analyzersink_la_CFLAGS =            \
        $(libcodecanalyzer_gst_analyzersink_cflags)      \
        $(NULL)

libcodecanalyzer_gst_analyzersink_la_LIBADD =            \
        $(libcodecanalyzer_gst_analyzersink_libs) -lgstcodecparsers-1.0        \
        $(NULL)

libcodecanalyzer_gst_analyzersink_la_LDFLAGS =           \
        $(GST_ALL_LDFLAGS)                    \
        $(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
