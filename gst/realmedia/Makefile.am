plugin_LTLIBRARIES = libgstrmdemux.la

libgstrmdemux_la_SOURCES = rademux.c rmdemux.c  \
			   rmutils.c rdtdepay.c rdtmanager.c \
			   rtspreal.c realhash.c asmrules.c \
			   rdtjitterbuffer.c gstrdtbuffer.c \
			   pnmsrc.c realmedia.c


libgstrmdemux_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
libgstrmdemux_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)\
	     -lgstrtsp-@GST_MAJORMINOR@ -lgstsdp-@GST_MAJORMINOR@
libgstrmdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrmdemux_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = rademux.h rmdemux.h rmutils.h rdtdepay.h rdtmanager.h \
		 rdtjitterbuffer.h rtspreal.h realhash.h asmrules.h gstrdtbuffer.h \
		 pnmsrc.h

noinst_PROGRAMS = asmrules
asmrules_CFLAGS = $(GST_CFLAGS) -DTEST
asmrules_LDADD = $(GST_LIBS) $(LIBM)

