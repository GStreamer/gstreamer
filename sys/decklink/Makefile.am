plugin_LTLIBRARIES = libgstdecklink.la

libgstdecklink_la_CPPFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CXXFLAGS) \
	$(DECKLINK_CXXFLAGS)
libgstdecklink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
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

libgstdecklink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstdecklink_la_SOURCES = \
	gstdecklinksink.cpp \
	gstdecklinksrc.cpp \
	gstdecklink.cpp \
	capture.cpp

if DECKLINK_OSX
libgstdecklink_la_SOURCES += \
	osx/DeckLinkAPIDispatch-osx.cpp
else
libgstdecklink_la_SOURCES += \
	linux/DeckLinkAPIDispatch.cpp
endif


noinst_HEADERS = \
	gstdecklink.h \
	gstdecklinksrc.h \
	gstdecklinksink.h \
	capture.h \
	linux/DeckLinkAPI.h \
	linux/LinuxCOM.h \
	osx/DeckLinkAPI.h \
	osx/DeckLinkAPIConfiguration.h \
	osx/DeckLinkAPIDeckControl.h \
	osx/DeckLinkAPIDiscovery.h \
	osx/DeckLinkAPIModes.h \
	osx/DeckLinkAPIStreaming.h \
	osx/DeckLinkAPITypes.h \
	osx/DeckLinkAPIVersion.h

EXTRA_DIST = \
	win/DeckLinkAPI.h \
	win/DeckLinkAPIDispatch.cpp \
	win/DeckLinkAPI_i.c


