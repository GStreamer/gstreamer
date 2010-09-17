plugin_LTLIBRARIES = libgstcog.la

ORC_SOURCE=gstcogorc
include $(top_srcdir)/common/orc.mak

libgstcog_la_CFLAGS = \
	-DCOG_ENABLE_UNSTABLE_API \
	-I$(srcdir)/.. \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(ORC_CFLAGS) \
	$(COG_CFLAGS)
libgstcog_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ORC_LIBS) \
	$(COG_LIBS)
libgstcog_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)
libgstcog_la_LIBTOOLFLAGS = --tag=disable-static

libgstcog_la_SOURCES = \
	cog.h \
	cogframe.c \
	cogframe.h \
	cogtables.c \
	cogutils.h \
	cogvirtframe.c \
	cogvirtframe.h \
	gstcog.c \
	gstcogcolorspace.c \
	gstcogdownsample.c \
	gstcogmse.c \
	gstcogscale.c \
	gstcogutils.c \
	gstcogutils.h \
	gstcolorconvert.c \
	gstlogoinsert.c \
	gstcms.h \
	gstcms.c
nodist_libgstcog_la_SOURCES = $(ORC_NODIST_SOURCES)

noinst_PROGRAMS = generate_tables
generate_tables_SOURCES = generate_tables.c gstcms.c
generate_tables_CFLAGS = $(GST_CFLAGS)
generate_tables_LDADD = $(GST_LIBS) $(LIBM)


