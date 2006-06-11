plugin_LTLIBRARIES = libgstsunaudio.la

libgstsunaudio_la_SOURCES = gstsunaudio.c \
                            gstsunaudiosink.c \
                            gstsunaudiomixerctrl.c \
                            gstsunaudiomixer.c \
                            gstsunaudiomixertrack.c

libgstsunaudio_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsunaudio_la_LIBADD = \
        $(GST_PLUGINS_BASE_LIBS) \
        -lgstinterfaces-$(GST_MAJORMINOR) \
        -lgstaudio-(GST_MAJORMINOR) \
        $(GST_LIBS)
libgstsunaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstsunaudiosink.h \
                 gstsunaudiomixer.h \
                 gstsunaudiomixerctrl.h \
                 gstsunaudiomixertrack.h

