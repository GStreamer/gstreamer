plugin_LTLIBRARIES = libgstopenexr.la

libgstopenexr_la_SOURCES = gstopenexrdec.cpp gstopenexr.c
libgstopenexr_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	$(OPENEXR_CFLAGS)
libgstopenexr_la_CXXFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) \
	$(OPENEXR_CFLAGS)
libgstopenexr_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(OPENEXR_LIBS)
libgstopenexr_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstopenexrdec.h \
	gstopenexr.h
