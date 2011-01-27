helpers_PROGRAMS = gst-plugin-scanner
helpersdir=$(libexecdir)/gstreamer-$(GST_MAJORMINOR)

gst_plugin_scanner_SOURCES = gst-plugin-scanner.c
gst_plugin_scanner_CFLAGS = $(GST_OBJ_CFLAGS)
gst_plugin_scanner_LDADD = $(GST_OBJ_LIBS)

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer -:EXECUTABLE gst-plugin-scanner  -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(gst_plugin_scanner_SOURCES) \
	 -:CFLAGS $(gst_plugin_scanner_CFLAGS) \
	 -:LDFLAGS $(gst_plugin_scanner_LDADD) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@

# clean out the old one to make sure everything is udpated correctly
# remove again after release
CLEANFILES = plugin-scanner
