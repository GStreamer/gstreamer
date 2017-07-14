plugin_LTLIBRARIES = libgstdecklink.la

libgstdecklink_la_CPPFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CXXFLAGS) \
	$(DECKLINK_CXXFLAGS)
libgstdecklink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ \
	-lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(DECKLINK_LIBS) \
	$(LIBM)
libgstdecklink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

if DECKLINK_OSX
libgstdecklink_la_LDFLAGS += \
	-Wl,-framework,CoreFoundation
endif

libgstdecklink_la_SOURCES = \
	gstdecklink.cpp \
	gstdecklinkaudiosink.cpp \
	gstdecklinkvideosink.cpp \
	gstdecklinkaudiosrc.cpp \
	gstdecklinkvideosrc.cpp

if DECKLINK_OSX
libgstdecklink_la_SOURCES += \
	osx/DeckLinkAPIDispatch.cpp
else

if DECKLINK_WIN
libgstdecklink_la_SOURCES += \
	win/DeckLinkAPIDispatch.cpp \
	win/DeckLinkAPI_i.c
else
libgstdecklink_la_SOURCES += \
	linux/DeckLinkAPIDispatch.cpp
endif

endif

noinst_HEADERS = \
	gstdecklink.h \
	gstdecklinkaudiosink.h \
	gstdecklinkvideosink.h \
	gstdecklinkaudiosrc.h \
	gstdecklinkvideosrc.h \
	linux/DeckLinkAPIConfiguration.h \
	linux/DeckLinkAPIDeckControl.h \
	linux/DeckLinkAPIDiscovery.h \
	linux/DeckLinkAPI.h \
	linux/DeckLinkAPIModes.h \
	linux/DeckLinkAPITypes.h \
	linux/LinuxCOM.h \
	osx/DeckLinkAPIConfiguration.h \
	osx/DeckLinkAPIDeckControl.h \
	osx/DeckLinkAPIDiscovery.h \
	osx/DeckLinkAPI.h \
	osx/DeckLinkAPIModes.h \
	osx/DeckLinkAPIStreaming.h \
	osx/DeckLinkAPITypes.h \
	osx/DeckLinkAPIVersion.h \
	win/DeckLinkAPI.h

EXTRA_DIST = \
	linux/DeckLinkAPIDispatch.cpp \
	osx/DeckLinkAPIDispatch.cpp \
	win/DeckLinkAPIDispatch.cpp \
	win/DeckLinkAPI_i.c


