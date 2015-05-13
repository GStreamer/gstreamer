noinst_LTLIBRARIES =				\
	libgstvaapi-baseutils.la		\
	$(NULL)

source_c = \
	gstbitwriter.c				\
	$(NULL)

source_h = \
	gstbitwriter.h				\
	$(NULL)

libgstvaapi_baseutils_cflags =			\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	-I$(top_builddir)/gst-libs		\
	$(GST_BASE_CFLAGS)			\
	$(GST_CFLAGS)				\
	$(NULL)

libgstvaapi_baseutils_libs =			\
	$(GST_BASE_LIBS)			\
	$(GST_LIBS)				\
	$(NULL)

nodist_libgstvaapi_baseutils_la_SOURCES =	\
	$(source_c)				\
	$(NULL)

libgstvaapi_baseutils_la_CFLAGS =		\
	$(libgstvaapi_baseutils_cflags)		\
	$(NULL)

libgstvaapi_baseutils_la_LIBADD =		\
	$(libgstvaapi_baseutils_libs)		\
	$(NULL)

libgstvaapi_baseutils_la_LDFLAGS =		\
	$(GST_ALL_LDFLAGS)			\
	$(NULL)

EXTRA_DIST = \
	$(source_c)				\
	$(source_h)				\
	$(NULL)

-include $(top_srcdir)/git.mk
