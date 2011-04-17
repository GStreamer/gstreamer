plugin_LTLIBRARIES = libgstdecklink.la

libgstdecklink_la_CPPFLAGS = \
	$(GST_PLUGINS_BAD_CXXFLAGS) \
	$(GST_PLUGINS_BASE_CXXFLAGS) \
	$(GST_CXXFLAGS) \
	$(DECKLINK_CXXFLAGS)
libgstdecklink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(DECKLINK_LIBS)
libgstdecklink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)
libgstdecklink_la_LIBTOOLFLAGS = --tag=disable-static

libgstdecklink_la_SOURCES = \
	gstdecklinksrc.cpp \
	gstdecklinksink.cpp \
	gstdecklink.cpp \
	capture.cpp \
	DeckLinkAPIDispatch.cpp

noinst_HEADERS = \
	gstdecklinksrc.h \
	gstdecklinksink.h \
	capture.h \
	DeckLinkAPI.h \
	LinuxCOM.h

