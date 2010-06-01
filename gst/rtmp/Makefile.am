plugin_LTLIBRARIES = libgstrtmp.la

libgstrtmp_la_SOURCES = amf.c \
                        gstrtmpsrc.c \
                        hashswf.c \
                        log.c \
                        parseurl.c \
                        rtmp.c

noinst_HEADERS = amf.h \
                 bytes.h \
                 dhgroups.h \
                 dh.h \
                 gstrtmpsrc.h \
                 handshake.h \
                 http.h \
                 log.h \
                 rtmp.h \
                 rtmp_sys.h

libgstrtmp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) -DCRYPTO
libgstrtmp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) -lssl $(LIBM)
libgstrtmp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtmp_la_LIBTOOLFLAGS = --tag=disable-static

