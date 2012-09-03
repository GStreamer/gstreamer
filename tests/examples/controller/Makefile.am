noinst_PROGRAMS = audio-example control-sources text-color-example

AM_CFLAGS  = $(GST_OBJ_CFLAGS) -I$(top_builddir)/libs

LDADD = \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_API_VERSION@.la \
	$(GST_OBJ_LIBS)

Android.mk: Makefile.am audio-example.c
	androgenizer \
	-:PROJECT audio-example -:EXECUTABLE audio-example \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES audio-example.c \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(AM_CFLAGS) \
	 -:LDFLAGS -lgstcontroller-@GST_API_VERSION@ \
	     $(GST_OBJ_LIBS) -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
