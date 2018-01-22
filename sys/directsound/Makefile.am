plugin_LTLIBRARIES = libgstdirectsound.la

libgstdirectsound_la_SOURCES =  gstdirectsoundsink.c gstdirectsounddevice.c gstdirectsoundplugin.c
libgstdirectsound_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(DIRECTSOUND_CFLAGS) \
	-DGstDirectSoundDeviceProvider=GstDirectSoundSinkDeviceProvider \
	-DGstDirectSoundDeviceProviderClass=GstDirectSoundSinkDeviceProviderClass \
	-DGstDirectSoundDevice=GstDirectSoundSinkDevice \
	-DGstDirectSoundDeviceClass=GstDirectSoundSinkDeviceClass
libgstdirectsound_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(DIRECTSOUND_LIBS)
libgstdirectsound_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTSOUND_LDFLAGS)

noinst_HEADERS = gstdirectsoundsink.h gstdirectsounddevice.h
