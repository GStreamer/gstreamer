noinst_LTLIBRARIES = libgstxmlregistry.la

if GST_DISABLE_LOADSAVE
xmlregistry_sources = gstxmlregistry.c
else
xmlregistry_sources = gstlibxmlregistry.c
endif

libgstxmlregistry_la_SOURCES = $(xmlregistry_sources)

libgstxmlregistry_la_CFLAGS = $(GST_LIB_CFLAGS)
libgstxmlregistry_la_LIBADD = $(GST_LIB_LIBS)

noinst_HEADERS = gstxmlregistry.h gstlibxmlregistry.h
check_PROGRAMS = registrytest

registrytest_SOURCES = registrytest.c
registrytest_CFLAGS = $(GST_OBJ_CFLAGS)
registrytest_LDADD = $(GST_OBJ_LIBS) libgstxmlregistry.la
