noinst_PROGRAMS = scaletempo-demo

scaletempo_demo_SOURCES = demo-main.c demo-player.c demo-gui.c
scaletempo_demo_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GTK_CFLAGS) 
scaletempo_demo_LDFLAGS = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GTK_LIBS) -lgstinterfaces-@GST_MAJORMINOR@

noinst_HEADERS = demo-player.h demo-gui.h

