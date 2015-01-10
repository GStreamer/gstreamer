noinst_PROGRAMS = netclock-server netclock-client

netclock_server_LDADD = \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_API_VERSION@.la \
	$(GST_OBJ_LIBS)
netclock_server_CFLAGS = $(GST_OBJ_CFLAGS)

netclock_client_LDADD = \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_API_VERSION@.la \
	$(GST_OBJ_LIBS)
netclock_client_CFLAGS = $(GST_OBJ_CFLAGS)
