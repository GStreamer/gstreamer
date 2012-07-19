noinst_LTLIBRARIES =				\
	libgstvaapi-codecparsers.la		\
	$(NULL)

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

libgstvaapi_codecparsers_source_c =		\
	gstjpegparser.c				\
	$(NULL)

libgstvaapi_codecparsers_source_h =		\
	gstjpegparser.h				\
	$(NULL)

libgstvaapi_codecparsers_la_SOURCES =		\
	$(libgstvaapi_codecparsers_source_c)	\
	$(libgstvaapi_codecparsers_source_h)	\
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

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
