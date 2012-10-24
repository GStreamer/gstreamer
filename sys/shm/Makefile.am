plugin_LTLIBRARIES = libgstshm.la

libgstshm_la_SOURCES = shmpipe.c shmalloc.c gstshm.c gstshmsrc.c gstshmsink.c
libgstshm_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS) -DSHM_PIPE_USE_GLIB
libgstshm_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstshm_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(SHM_LIBS)

libgstshm_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstshmsrc.h gstshmsink.h shmpipe.h  shmalloc.h
