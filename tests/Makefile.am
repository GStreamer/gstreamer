noinst_PROGRAMS = test-cleanup test-reuse

AM_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir) -I$(srcdir)
LDADD = $(GST_OBJ_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-1.0 -lgstsdp-1.0 -lgstrtsp-1.0

SUBDIRS = check

DIST_SUBDIRS = check
