if GST_HAVE_MMAP
EFENCE_PLUGIN=libgstefence.la
else
EFENCE_PLUGIN=
endif

plugin_LTLIBRARIES = $(EFENCE_PLUGIN) libgstdebug.la libgstnavigationtest.la 

noinst_HEADERS = \
	cpureport.h \
	efence.h \
	gstcapsdebug.h \
	gstcapssetter.h \
	gstnavigationtest.h \
	gstnavseek.h \
	gstpushfilesrc.h \
	gsttaginject.h \
	progressreport.h \
	tests.h

libgstefence_la_SOURCES = efence.c
libgstefence_la_CFLAGS = $(GST_CFLAGS)
libgstefence_la_LIBADD = $(GST_LIBS)
libgstefence_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstefence_la_LIBTOOLFLAGS = --tag=disable-static

libgstnavigationtest_la_SOURCES = gstnavigationtest.c
libgstnavigationtest_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
				 $(GST_PLUGINS_BASE_CFLAGS)
libgstnavigationtest_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ $(LIBM)
libgstnavigationtest_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstnavigationtest_la_LIBTOOLFLAGS = --tag=disable-static

libgstdebug_la_SOURCES = \
	gstdebug.c \
	breakmydata.c \
	gstcapsdebug.c \
	gstcapssetter.c \
	gstnavseek.c \
	gstpushfilesrc.c \
	gsttaginject.c \
	rndbuffersize.c \
	progressreport.c \
	tests.c \
	cpureport.c \
	testplugin.c

libgstdebug_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstdebug_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstdebug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdebug_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstdebug -:SHARED libgstdebug \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdebug_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstdebug_la_CFLAGS) \
	 -:LDFLAGS $(libgstdebug_la_LDFLAGS) \
	           $(libgstdebug_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
