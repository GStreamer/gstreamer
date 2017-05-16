plugin_LTLIBRARIES = libgstrealmedia.la

libgstrealmedia_la_SOURCES = rademux.c rmdemux.c  \
			   rmutils.c rdtdepay.c rdtmanager.c \
			   rtspreal.c realhash.c asmrules.c \
			   rdtjitterbuffer.c gstrdtbuffer.c \
			   pnmsrc.c realmedia.c


libgstrealmedia_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstrealmedia_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
				-lgstrtsp-@GST_API_VERSION@ \
				-lgstsdp-@GST_API_VERSION@ \
				-lgstpbutils-@GST_API_VERSION@ \
				$(GST_BASE_LIBS) $(GST_LIBS)
libgstrealmedia_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = rademux.h rmdemux.h rmutils.h rdtdepay.h rdtmanager.h \
		 rdtjitterbuffer.h rtspreal.h realhash.h asmrules.h gstrdtbuffer.h \
		 pnmsrc.h

noinst_PROGRAMS = asmrules
asmrules_CFLAGS = $(GST_CFLAGS) -DTEST
asmrules_LDADD = $(GST_LIBS) $(LIBM)
