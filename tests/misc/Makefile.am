EXTRA_DIST = \
	network-clock.scm \
	network-clock-utils.scm \
	plot-data

noinst_PROGRAMS = netclock-replay

netclock_replay_LDADD = \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_API_VERSION@.la \
	$(GST_OBJ_LIBS) \
	$(GIO_LIBS)
netclock_replay_CFLAGS = $(GST_OBJ_CFLAGS) $(GIO_CFLAGS)
