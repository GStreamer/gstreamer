plugin_LTLIBRARIES = libgstcog.la

libgstcog_la_CFLAGS = \
	-DCOG_ENABLE_UNSTABLE_API \
	-I$(srcdir)/.. \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(COG_CFLAGS)
libgstcog_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
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
	gstcogfilter.c \
	gstcogmse.c \
	gstcogscale.c \
	gstcogutils.c \
	gstcogutils.h \
	gstcolorconvert.c \
	gstlogoinsert.c \
	gstcms.h \
	gstcms.c

noinst_PROGRAMS = generate_tables
generate_tables_SOURCES = generate_tables.c gstcms.c
generate_tables_CFLAGS = $(GST_CFLAGS)
generate_tables_LDADD = $(GST_LIBS)

nodist_libgstcog_la_SOURCES = cogorc.c cogorc.h
BUILT_SOURCES = cogorc.c cogorc.h
CLEANFILES = cogorc.c cogorc.h

EXTRA_DIST=cog.orc

cogorc.c: cog.orc
	$(ORCC) --implementation -o cogorc.c cog.orc

cogorc.h: cog.orc
	$(ORCC) --header -o cogorc.h cog.orc

