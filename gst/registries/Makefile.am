noinst_LTLIBRARIES = libgstxmlregistry.la

libgstxmlregistry_la_SOURCES = gstxmlregistry.c

libgstxmlregistry_la_CFLAGS = $(GST_LIB_CFLAGS)
libgstxmlregistry_la_LIBADD = $(GST_LIB_LIBS)

noinst_HEADERS = gstxmlregistry.h
check_PROGRAMS = registrytest

registrytest_SOURCES = registrytest.c
registrytest_CFLAGS = $(GST_OBJ_CFLAGS)
registrytest_LDADD = $(GST_OBJ_LIBS) libgstxmlregistry.la
