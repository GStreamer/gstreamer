noinst_LTLIBRARIES = libcrashtest.la

libcrashtest_la_SOURCES = dummy.c
libcrashtest_la_CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)

noinst_HEADERS = dummy.h

noinst_PROGRAMS = test
test_LDADD = libcrashtest.la $(GLIB_LIBS) 
test_CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)
