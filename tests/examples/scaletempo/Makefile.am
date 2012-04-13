noinst_PROGRAMS = scaletempo-demo

# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

scaletempo_demo_SOURCES = demo-main.c demo-player.c demo-gui.c
scaletempo_demo_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GTK_CFLAGS)
scaletempo_demo_LDFLAGS = $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) $(GTK_LIBS)

noinst_HEADERS = demo-player.h demo-gui.h

