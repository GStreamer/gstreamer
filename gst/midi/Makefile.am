plugin_LTLIBRARIES = libgstmidi.la

libgstmidi_la_SOURCES = midi.c midiparse.c
libgstmidi_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstmidi_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(LIBM)
libgstmidi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmidi_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = midiparse.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmidi -:SHARED libgstmidi \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmidi_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmidi_la_CFLAGS) \
	 -:LDFLAGS $(libgstmidi_la_LDFLAGS) \
	           $(libgstmidi_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-1.0' \
	> $@
