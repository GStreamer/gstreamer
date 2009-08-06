lib_LTLIBRARIES = libgstcheck-@GST_MAJORMINOR@.la

SYMBOLS_FILE = exports.sym

libgstcheck_@GST_MAJORMINOR@_la_DEPENDENCIES = \
	$(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la \
	$(SYMBOLS_FILE)
libgstcheck_@GST_MAJORMINOR@_la_SOURCES =	\
	gstbufferstraw.c			\
	gstcheck.c				\
	gstconsistencychecker.c

libgstcheck_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS) \
	-I$(top_builddir)/libs/gst/check \
	-I$(top_builddir)/libs/gst/check/libcheck
libgstcheck_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS) \
	$(top_builddir)/libs/gst/check/libcheck/libcheckinternal.la \
	$(LIBM)
libgstcheck_@GST_MAJORMINOR@_la_LDFLAGS = \
	-exports $(SYMBOLS_FILE) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstcheck_@GST_MAJORMINOR@includedir =		\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/check

libgstcheck_@GST_MAJORMINOR@include_HEADERS =	\
	internal-check.h			\
	gstbufferstraw.h			\
	gstcheck.h				\
	gstconsistencychecker.h

CLEANFILES = *.gcno *.gcda *.gcov internal-check.h $(SYMBOLS_FILE)

internal-check.h: $(top_builddir)/libs/gst/check/libcheck/check.h
	cp $(top_builddir)/libs/gst/check/libcheck/check.h internal-check.h

%.c.gcov: .libs/libgstcheck_@GST_MAJORMINOR@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcheck_@GST_MAJORMINOR@_la_SOURCES:=.gcov)

SUBDIRS = libcheck

LIBGSTCHECK_EXPORTED_SYMBOLS = \
	_gst_check_debug \
	_gst_check_expecting_log \
	_gst_check_raised_critical \
	_gst_check_raised_warning \
	_gst_check_run_test_func \
	_gst_check_threads_running \
	gst_buffer_straw_get_buffer \
	gst_buffer_straw_start_pipeline \
	gst_buffer_straw_stop_pipeline \
	gst_check_abi_list \
	gst_check_caps_equal \
	gst_check_chain_func \
	gst_check_drop_buffers \
	gst_check_element_push_buffer \
	gst_check_element_push_buffer_list \
	gst_check_init \
	gst_check_message_error \
	gst_check_run_suite \
	gst_check_setup_element \
	gst_check_setup_sink_pad \
	gst_check_setup_sink_pad_by_name \
	gst_check_setup_src_pad \
	gst_check_setup_src_pad_by_name \
	gst_check_teardown_element \
	gst_check_teardown_pad_by_name \
	gst_check_teardown_sink_pad \
	gst_check_teardown_src_pad

$(SYMBOLS_FILE):
	CHECK_SYMS=`sed -n -e 's/^..*CK_EXPORT[[:space:]][[:space:]]*\([[:alnum:]_][[:alnum:]_]*\)..*$$/\1/p' @top_srcdir@/libs/gst/check/libcheck/check.h.in` ; \
	rm -f $(SYMBOLS_FILE) 2>/dev/null; \
	for s in $(LIBGSTCHECK_EXPORTED_SYMBOLS) $$CHECK_SYMS; do \
	  echo $$s >> $(SYMBOLS_FILE); \
	done

