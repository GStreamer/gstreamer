plugin_LTLIBRARIES = libgstdecklink.la

libgstdecklink_la_CPPFLAGS = \
	$(GST_BASE_CFLAGS) \
	$(GST_CXXFLAGS) \
	$(DECKLINK_CXXFLAGS)
libgstdecklink_la_LIBADD = \
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
	DeckLinkAPIDispatch.cpp

noinst_HEADERS = \
	gstdecklinksrc.h \
	gstdecklinksink.h \
	capture.h \
	DeckLinkAPI.h \
	LinuxCOM.h

