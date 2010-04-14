noinst_PROGRAMS = audio-example

audio_example_CFLAGS  = $(GST_OBJ_CFLAGS) -I$(top_builddir)/libs
audio_example_LDADD = \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la \
	$(GST_OBJ_LIBS)
