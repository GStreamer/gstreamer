noinst_PROGRAMS = test-video test-ogg test-mp4 test-readme \
		  test-launch test-sdp test-uri test-auth \
		  test-multicast test-multicast2 test-appsrc \
		  test-video-rtx test-record test-record-auth \
		  test-netclock test-netclock-client

#INCLUDES = -I$(top_srcdir) -I$(srcdir)

AM_CFLAGS = $(GST_OBJ_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GIO_LIBS)

if HAVE_LIBCGROUP
noinst_PROGRAMS += test-cgroups
LDADD += $(LIBCGROUP_LIBS)
endif

test_netclock_LDFLAGS = \
        $(GST_LIBS) \
        -lgstnet-@GST_API_VERSION@ \
        $(top_builddir)/gst/rtsp-server/libgstrtspserver-@GST_API_VERSION@.la

test_netclock_client_LDFLAGS = \
        $(GST_LIBS) \
        -lgstnet-@GST_API_VERSION@
