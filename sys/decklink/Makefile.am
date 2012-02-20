plugin_LTLIBRARIES = libgstdecklink.la

libgstdecklink_la_CPPFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CXXFLAGS) \
	$(DECKLINK_CXXFLAGS)
libgstdecklink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(DECKLINK_LIBS) \
	$(LIBM)
libgstdecklink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 
libgstdecklink_la_LIBTOOLFLAGS = --tag=disable-static

libgstdecklink_la_SOURCES = \
	gstdecklinksrc.cpp \
	gstdecklinksink.cpp \
	gstdecklink.cpp \
	capture.cpp \
	linux/DeckLinkAPIDispatch.cpp

noinst_HEADERS = \
	gstdecklink.h \
	gstdecklinksrc.h \
	gstdecklinksink.h \
	capture.h \
	linux/DeckLinkAPI.h \
	linux/LinuxCOM.h

EXTRA_DIST = \
	win/DeckLinkAPI.h \
	win/DeckLinkAPIDispatch.cpp \
	win/DeckLinkAPI_i.c

