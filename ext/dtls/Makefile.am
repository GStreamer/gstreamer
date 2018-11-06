plugin_LTLIBRARIES = libgstdtls.la

libgstdtls_la_SOURCES = \
    plugin.c \
    gstdtlsdec.c \
    gstdtlsenc.c \
    gstdtlssrtpdemux.c \
    gstdtlssrtpbin.c \
    gstdtlssrtpdec.c \
    gstdtlssrtpenc.c \
    gstdtlsagent.c \
    gstdtlsconnection.c \
    gstdtlscertificate.c

libgstdtls_la_CFLAGS = \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS) \
    $(DTLS_CFLAGS)

libgstdtls_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(DTLS_LIBS) $(WINSOCK2_LIBS)
libgstdtls_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
    gstdtlsdec.h \
    gstdtlsenc.h \
    gstdtlssrtpdemux.h \
    gstdtlssrtpbin.h \
    gstdtlssrtpdec.h \
    gstdtlssrtpenc.h \
    gstdtlsagent.h \
    gstdtlsconnection.h \
    gstdtlscertificate.h

