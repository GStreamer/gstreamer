helpers_PROGRAMS = gst-plugin-scanner
helpersdir=$(libexecdir)/gstreamer-$(GST_MAJORMINOR)

gst_plugin_scanner_SOURCES = gst-plugin-scanner.c
gst_plugin_scanner_CFLAGS = $(GST_OBJ_CFLAGS)
gst_plugin_scanner_LDADD = $(GST_OBJ_LIBS)

# clean out the old one to make sure everything is udpated correctly
# remove again after release
CLEANFILES = plugin-scanner
