gsettings_SCHEMAS = org.freedesktop.gstreamer-@GST_API_VERSION@.default-elements.gschema.xml

org.freedesktop.gstreamer-@GST_API_VERSION@.default-elements.gschema.xml: org.freedesktop.gstreamer.default-elements.gschema.xml
	cp org.freedesktop.gstreamer.default-elements.gschema.xml org.freedesktop.gstreamer-@GST_API_VERSION@.default-elements.gschema.xml

if USE_GSETTINGS
@GSETTINGS_RULES@
endif

plugin_LTLIBRARIES = libgstgsettingselements.la

libgstgsettingselements_la_SOURCES = \
        gstgsettingsaudiosink.c \
        gstgsettingsaudiosrc.c \
        gstgsettingsvideosink.c \
        gstgsettingsvideosrc.c \
        gstswitchsink.c \
        gstswitchsrc.c \
	plugin.c

libgstgsettingselements_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS) $(GSETTINGS_CFLAGS) $(DIR_CFLAGS) \
	-DGstSwitchSrc=GstGSettingsSwitchSrc \
	-DGstSwitchSrcClass=GstGSettingsSwitchSrcClass \
	-DGstSwitchSink=GstGSettingsSwitchSink \
	-DGstSwitchSinkClass=GstGSettingsSwitchSinkClass
libgstgsettingselements_la_LIBADD = $(GST_LIBS) $(GSETTINGS_LIBS)
libgstgsettingselements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgsettingselements_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
        gstgsettingsaudiosink.h \
        gstgsettingsaudiosrc.h \
        gstgsettingsvideosink.h \
        gstgsettingsvideosrc.h \
        gstswitchsink.h \
        gstswitchsrc.h \
	gstgsettings.h

EXTRA_DIST = org.freedesktop.gstreamer.default-elements.gschema.xml

CLEANFILES = $(gsettings_SCHEMAS)
