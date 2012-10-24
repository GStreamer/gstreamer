plugin_LTLIBRARIES = libgstequalizer.la

libgstequalizer_la_SOURCES = \
        gstiirequalizer.c gstiirequalizer.h \
        gstiirequalizernbands.c gstiirequalizernbands.h \
        gstiirequalizer3bands.c gstiirequalizer3bands.h \
        gstiirequalizer10bands.c gstiirequalizer10bands.h

libgstequalizer_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstequalizer_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) $(GST_BASE_LIBS) \
	$(GST_LIBS) $(LIBM)
libgstequalizer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstequalizer_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstiirequalizer.h

presetdir = $(datadir)/gstreamer-$(GST_API_VERSION)/presets
preset_DATA = GstIirEqualizer3Bands.prs GstIirEqualizer10Bands.prs

EXTRA_DIST = $(preset_DATA)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstequalizer -:SHARED libgstequalizer \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstequalizer_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstequalizer_la_CFLAGS) \
	 -:LDFLAGS $(libgstequalizer_la_LDFLAGS) \
	           $(libgstequalizer_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
