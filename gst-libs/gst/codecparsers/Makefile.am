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

add_source_c =
add_source_h =
gen_source_c = parserutils.c nalutils.c
gen_source_h = parserutils.h nalutils.h

# Always build VC-1 and MPEG-4 parsers for now
gen_source_c += gstvc1parser.c gstmpeg4parser.c
gen_source_h += gstvc1parser.h gstmpeg4parser.h

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

if USE_LOCAL_CODEC_PARSERS_VP8
gen_source_c += gstvp8parser.c
gen_source_h += gstvp8parser.h gstvp8rangedecoder.h vp8utils.h

if USE_BUILTIN_LIBVPX
add_source_c += gstvaapilibvpx.c

libgstvaapi_codecparsers_cflags += \
	-I$(top_srcdir)/ext/libvpx \
	-I$(top_srcdir)/ext/libvpx/upstream \
	-I$(top_builddir)/ext/libvpx/upstream

libgstvaapi_codecparsers_libs += \
	$(top_builddir)/ext/libvpx/libgstcodecparsers_vpx.la
else
gen_source_c += dboolhuff.c gstvp8rangedecoder.c vp8utils.c
gen_source_h += dboolhuff.h
endif
endif

GENFILES = \
	$(gen_source_c)				\
	$(gen_source_h)				\
	$(NULL)

nodist_EXTRA_libgstvaapi_codecparsers_la_SOURCES = dummy.c

nodist_libgstvaapi_codecparsers_la_SOURCES =	\
	$(gen_source_c)				\
	$(add_source_c)				\
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
$(top_builddir)/ext/libvpx/libgstcodecparsers_vpx.la:
	$(MAKE) -C $(top_builddir)/ext/libvpx

# All sources and headers that could be used here
all_source_c = \
	dboolhuff.c				\
	gsth264parser.c				\
	gstjpegparser.c				\
	gstmpeg4parser.c			\
	gstmpegvideoparser.c			\
	gstvaapilibvpx.c			\
	gstvc1parser.c				\
	gstvp8parser.c				\
	gstvp8rangedecoder.c			\
	nalutils.c				\
	parserutils.c				\
	vp8utils.c				\
	$(NULL)

all_source_h = \
	dboolhuff.h				\
	gsth264parser.h				\
	gstjpegparser.h				\
	gstmpeg4parser.h			\
	gstmpegvideoparser.h			\
	gstvc1parser.h				\
	gstvp8parser.h				\
	gstvp8rangedecoder.h			\
	nalutils.h				\
	parserutils.h				\
	vp8utils.h				\
	$(NULL)

EXTRA_DIST = \
	$(all_source_c) \
	$(all_source_h) \
	$(NULL)

DISTCLEANFILES = $(GENFILES) .timestamp.symlinks

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
