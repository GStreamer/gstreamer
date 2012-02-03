noinst_PROGRAMS = scaletempo-demo

scaletempo_demo_SOURCES = demo-main.c demo-player.c demo-gui.c
scaletempo_demo_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GTK_CFLAGS)
scaletempo_demo_LDFLAGS = $(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-@GST_MAJORMINOR@ $(GST_LIBS) $(GTK_LIBS)

noinst_HEADERS = demo-player.h demo-gui.h

