noinst_LTLIBRARIES =				\
	libgstvaapi-codecparsers.la		\
	$(NULL)

local_codecparsers_srcdir = \
	$(top_srcdir)/ext/codecparsers/gst-libs/gst/codecparsers

libgstvaapi_codecparsers_cflags =		\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	-I$(top_builddir)/gst-libs		\
	$(GST_BASE_CFLAGS)			\
	$(GST_CFLAGS)				\
	$(NULL)

libgstvaapi_codecparsers_libs =			\
	$(GST_BASE_LIBS)			\
	$(GST_LIBS)				\
	$(NULL)

gen_source_c = parserutils.c
gen_source_h = parserutils.h

# Always build VC-1 parser for now
gen_source_c += gstvc1parser.c
gen_source_h += gstvc1parser.h

if USE_LOCAL_CODEC_PARSERS_JPEG
gen_source_c += gstjpegparser.c
gen_source_h += gstjpegparser.h
endif

if USE_LOCAL_CODEC_PARSERS_MPEG2
gen_source_c += gstmpegvideoparser.c
gen_source_h += gstmpegvideoparser.h
endif

if USE_LOCAL_CODEC_PARSERS_H264
gen_source_c += gsth264parser.c
gen_source_h += gsth264parser.h
endif

GENFILES = \
	$(gen_source_c)				\
	$(gen_source_h)				\
	$(NULL)

nodist_EXTRA_libgstvaapi_codecparsers_la_SOURCES = dummy.c

nodist_libgstvaapi_codecparsers_la_SOURCES =	\
	$(gen_source_c)				\
	$(NULL)

libgstvaapi_codecparsers_la_CFLAGS =		\
	$(libgstvaapi_codecparsers_cflags)	\
	$(NULL)

libgstvaapi_codecparsers_la_LIBADD =		\
	$(libgstvaapi_codecparsers_libs)	\
	$(NULL)

libgstvaapi_codecparsers_la_LDFLAGS =		\
	$(GST_ALL_LDFLAGS)			\
	$(NULL)

all-local: .timestamp.symlinks

.timestamp.symlinks: $(GENFILES)
	touch $@

$(gen_source_c): %.c: $(local_codecparsers_srcdir)/%.c $(gen_source_h)
	$(LN_S) -f $< $@
$(gen_source_h): %.h: $(local_codecparsers_srcdir)/%.h
	$(LN_S) -f $< $@

DISTCLEANFILES = $(GENFILES) .timestamp.symlinks

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
