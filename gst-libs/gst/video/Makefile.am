noinst_LTLIBRARIES =				\
	libgstvaapi-videoutils.la		\
	$(NULL)

local_videoutils_srcdir = \
	$(top_srcdir)/ext/videoutils/gst-libs/gst/video

libgstvaapi_videoutils_cflags =			\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	-I$(top_builddir)/gst-libs		\
	$(GST_BASE_CFLAGS)			\
	$(GST_CFLAGS)				\
	$(NULL)

libgstvaapi_videoutils_libs =			\
	$(GST_BASE_LIBS)			\
	$(GST_LIBS)				\
	$(NULL)

gen_source_c =
gen_source_h =

if USE_LOCAL_GST_VIDEO_DECODER
gen_source_c += gstvideodecoder.c gstvideoutils.c video.c
gen_source_h += gstvideodecoder.h gstvideoutils.h video.h
endif

GENFILES = \
	$(gen_source_c)				\
	$(gen_source_h)				\
	$(NULL)

nodist_EXTRA_libgstvaapi_videoutils_la_SOURCES = dummy.c

nodist_libgstvaapi_videoutils_la_SOURCES =	\
	$(gen_source_c)				\
	$(NULL)

libgstvaapi_videoutils_la_CFLAGS =		\
	$(libgstvaapi_videoutils_cflags)	\
	$(NULL)

libgstvaapi_videoutils_la_LIBADD =		\
	$(libgstvaapi_videoutils_libs)		\
	$(NULL)

libgstvaapi_videoutils_la_LDFLAGS =		\
	$(GST_ALL_LDFLAGS)			\
	$(NULL)

all-local: .timestamp.symlinks

.timestamp.symlinks: $(GENFILES)
	touch $@

$(gen_source_c): %.c: $(local_videoutils_srcdir)/%.c $(gen_source_h) 
	$(LN_S) -f $< $@
$(gen_source_h): %.h: $(local_videoutils_srcdir)/%.h
	$(LN_S) -f $< $@

DISTCLEANFILES = $(GENFILES) .timestamp.symlinks

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
