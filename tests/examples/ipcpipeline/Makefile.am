noinst_PROGRAMS = ipcpipeline1 \
		  ipc-play

ipcpipeline1_SOURCES = ipcpipeline1.c
ipcpipeline1_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
ipcpipeline1_LDFLAGS = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GSTPB_BASE_LIBS)

ipc_play_SOURCES = ipc-play.c
ipc_play_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
ipc_play_LDFLAGS = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GSTPB_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION)
