noinst_LTLIBRARIES =				\
	libgstvaapi-codecparsers.la		\
	$(NULL)

local_codecparsers_srcdir = \
	$(top_srcdir)/ext/codecparsers/gst-libs/gst/codecparsers

libgstvaapi_codecparsers_cflags =		\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GST_BASE_CFLAGS)			\
	$(GST_CFLAGS)				\
	$(NULL)

libgstvaapi_codecparsers_libs =			\
	$(GST_BASE_LIBS)			\
	$(GST_LIBS)				\
	$(NULL)

gen_source_c =
gen_source_h =

if USE_LOCAL_CODEC_PARSERS_JPEG
gen_source_c += gstjpegparser.c
gen_source_h += gstjpegparser.h
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

$(gen_source_c): %.c: $(local_codecparsers_srcdir)/%.c
	$(LN_S) -f $< $@
$(gen_source_h): %.h: $(local_codecparsers_srcdir)/%.h
	$(LN_S) -f $< $@

DISTCLEANFILES = $(GENFILES) .timestamp.symlinks

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
