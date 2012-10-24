plugin_LTLIBRARIES = libgstsubparse.la

if USE_XML
SAMIPARSE_SOURCES = samiparse.c samiparse.h
else
SAMIPARSE_SOURCES = 
endif

libgstsubparse_la_SOURCES = \
	gstssaparse.c \
	gstssaparse.h \
	gstsubparse.c \
	gstsubparse.h \
	$(SAMIPARSE_SOURCES) \
	tmplayerparse.c \
	tmplayerparse.h \
	mpl2parse.c \
	mpl2parse.h \
        qttextparse.c \
        qttextparse.h

libgstsubparse_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstsubparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsubparse_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstsubparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstssaparse.h \
	gstsubparse.h \
	samiparse.h \
	tmplayerparse.h \
	mpl2parse.h \
        qttextparse.h
