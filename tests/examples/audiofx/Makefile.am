noinst_PROGRAMS = firfilter-example iirfilter-example

# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

firfilter_example_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
firfilter_example_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstfft-@GST_API_VERSION@ $(LIBM)

iirfilter_example_CFLAGS = $(GST_CFLAGS)
iirfilter_example_LDADD = $(GST_LIBS) $(LIBM)
