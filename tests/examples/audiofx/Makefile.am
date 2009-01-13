noinst_PROGRAMS = firfilter-example iirfilter-example

firfilter_example_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
firfilter_example_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstfft-@GST_MAJORMINOR@ $(LIBM)

iirfilter_example_CFLAGS = $(GST_CFLAGS)
iirfilter_example_LDADD = $(GST_LIBS) $(LIBM)
