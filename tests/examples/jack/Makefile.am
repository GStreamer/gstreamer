if HAVE_GTK
GTK_EXAMPLES=jack_client
else
GTK_EXAMPLES=
endif

noinst_PROGRAMS = $(GTK_EXAMPLES)

jack_client_SOURCES = jack_client.c
jack_client_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS) $(JACK_CFLAGS)
jack_client_LDFLAGS = $(GST_LIBS) $(GTK_LIBS) $(JACK_LIBS)

