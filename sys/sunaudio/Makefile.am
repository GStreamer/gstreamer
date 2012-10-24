plugin_LTLIBRARIES = libgstsunaudio.la

libgstsunaudio_la_SOURCES = gstsunaudio.c \
                            gstsunaudiosink.c \
                            gstsunaudiomixerctrl.c \
                            gstsunaudiomixer.c \
                            gstsunaudiomixertrack.c \
                            gstsunaudiomixeroptions.c \
                            gstsunaudiosrc.c

libgstsunaudio_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsunaudio_la_LIBADD = \
        -lgstinterfaces-@GST_API_VERSION@ \
        -lgstaudio-@GST_API_VERSION@ \
        $(GST_PLUGINS_BASE_LIBS) \
        $(GST_LIBS)
libgstsunaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsunaudio_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstsunaudiosink.h \
                 gstsunaudiomixer.h \
                 gstsunaudiomixerctrl.h \
                 gstsunaudiomixertrack.h \
                 gstsunaudiomixeroptions.h \
                 gstsunaudiosrc.h

