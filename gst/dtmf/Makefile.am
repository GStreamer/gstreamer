plugin_LTLIBRARIES = libgstdtmf.la

libgstdtmf_la_SOURCES = gstdtmfsrc.c \
                        gstrtpdtmfsrc.c \
                        gstdtmf.c

noinst_HEADERS = gstdtmfsrc.h \
                 gstrtpdtmfsrc.h

libgstdtmf_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(ERROR_CFLAGS) -DEXTERN_BUF -DRTP_SUPPORT
libgstdtmf_la_LIBADD = $(GST_LIBS_LIBS)
libgstdtmf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@

