noinst_LTLIBRARIES = libgstxmlregistry.la

libgstxmlregistry_la_SOURCES = gstxmlregistry.c

libgstxmlregistry_la_CFLAGS = $(LIBGST_CFLAGS)
libgstxmlregistry_la_LIBADD = $(LIBGST_LIBS)

noinst_HEADERS = gstxmlregistry.h
check_PROGRAMS = registrytest

registrytest_SOURCES = registrytest.c
registrytest_CFLAGS = $(GST_CFLAGS)
registrytest_LDADD = $(GST_LIBS) libgstxmlregistry.la
