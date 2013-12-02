plugin_LTLIBRARIES = libgstopenni2.la

# sources used to compile this plug-in
libgstopenni2_la_SOURCES = gstopenni2.cpp \
                          gstopenni2src.cpp

libgstopenni2_la_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) $(OPENNI2_CFLAGS)

# flags used to compile this facedetect
# add other _CFLAGS and _LIBS as needed
#
libgstopenni2_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
                       $(GST_CFLAGS) $(OPENNI2_CFLAGS)

libgstopenni2_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENNI2_LIBS) \
                       $(GSTPB_BASE_LIBS) -lgstvideo-$(GST_API_VERSION)

libgstopenni2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstopenni2_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstopenni2src.h