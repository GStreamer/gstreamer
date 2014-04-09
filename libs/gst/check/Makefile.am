lib_LTLIBRARIES = libgstcheck-@GST_API_VERSION@.la

SYMBOLS_FILE = exports.sym

BUILT_SOURCES = internal-check.h

libgstcheck_@GST_API_VERSION@_la_DEPENDENCIES = \
	$(top_builddir)/gst/libgstreamer-@GST_API_VERSION@.la \
	internal-check.h \
	$(SYMBOLS_FILE)
libgstcheck_@GST_API_VERSION@_la_SOURCES =	\
	gstbufferstraw.c			\
	gstcheck.c				\
	gstconsistencychecker.c			\
	gsttestclock.c

libgstcheck_@GST_API_VERSION@_la_CFLAGS = $(GST_OBJ_CFLAGS) \
	-I$(top_builddir)/libs \
	-I$(top_builddir)/libs/gst/check \
	-I$(top_builddir)/libs/gst/check/libcheck
libgstcheck_@GST_API_VERSION@_la_LIBADD = $(GST_OBJ_LIBS) \
	$(top_builddir)/libs/gst/check/libcheck/libcheckinternal.la \
	$(LIBM)
libgstcheck_@GST_API_VERSION@_la_LDFLAGS = \
	-export-symbols $(SYMBOLS_FILE) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstcheck_@GST_API_VERSION@includedir =		\
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/check

libgstcheck_@GST_API_VERSION@include_HEADERS =	\
	check.h					\
	gstbufferstraw.h			\
	gstcheck.h				\
	gstconsistencychecker.h			\
	gsttestclock.h

nodist_libgstcheck_@GST_API_VERSION@include_HEADERS =	\
	internal-check.h	

CLEANFILES = *.gcno *.gcda *.gcov internal-check.h $(SYMBOLS_FILE)

internal-check.h: $(top_builddir)/libs/gst/check/libcheck/check.h
	$(AM_V_GEN)cp $(top_builddir)/libs/gst/check/libcheck/check.h internal-check.h

%.c.gcov: .libs/libgstcheck_@GST_API_VERSION@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcheck_@GST_API_VERSION@_la_SOURCES:=.gcov)

SUBDIRS = libcheck

LIBGSTCHECK_EXPORTED_VARS = \
	_gst_check_threads_running \
	_gst_check_raised_critical \
	_gst_check_raised_warning \
	_gst_check_expecting_log \
	buffers \
	check_cond \
	check_debug \
	check_mutex \
	mutex \
	start_cond \
	sync_cond \
	thread_list

LIBGSTCHECK_EXPORTED_FUNCS = \
	_gst_check_run_test_func \
	gst_buffer_straw_get_buffer \
	gst_buffer_straw_start_pipeline \
	gst_buffer_straw_stop_pipeline \
	gst_check_abi_list \
	gst_check_caps_equal \
	gst_check_buffer_data \
	gst_check_chain_func \
	gst_check_drop_buffers \
	gst_check_element_push_buffer \
	gst_check_element_push_buffer_list \
	gst_check_init \
	gst_check_message_error \
	gst_check_run_suite \
	gst_check_setup_element \
	gst_check_setup_events \
	gst_check_setup_events_with_stream_id \
	gst_check_setup_sink_pad \
	gst_check_setup_sink_pad_from_template \
	gst_check_setup_sink_pad_by_name \
	gst_check_setup_sink_pad_by_name_from_template \
	gst_check_setup_src_pad \
	gst_check_setup_src_pad_from_template \
	gst_check_setup_src_pad_by_name \
	gst_check_setup_src_pad_by_name_from_template \
	gst_check_teardown_element \
	gst_check_teardown_pad_by_name \
	gst_check_teardown_sink_pad \
	gst_check_teardown_src_pad \
	gst_consistency_checker_add_pad \
	gst_consistency_checker_new \
	gst_consistency_checker_reset \
	gst_consistency_checker_free \
	gst_test_clock_get_type \
	gst_test_clock_new \
	gst_test_clock_new_with_start_time \
	gst_test_clock_set_time \
	gst_test_clock_advance_time \
	gst_test_clock_peek_id_count \
	gst_test_clock_has_id \
	gst_test_clock_peek_next_pending_id \
	gst_test_clock_wait_for_next_pending_id \
	gst_test_clock_wait_for_pending_id_count \
	gst_test_clock_process_next_clock_id \
	gst_test_clock_get_next_entry_time \
	gst_test_clock_wait_for_multiple_pending_ids \
	gst_test_clock_process_id_list \
	gst_test_clock_id_list_get_latest_time


LIBGSTCHECK_EXPORTED_SYMBOLS = \
	$(LIBGSTCHECK_EXPORTED_VARS) \
	$(LIBGSTCHECK_EXPORTED_FUNCS)

# Please do not even think about changing the alphabet below into A-Za-z.
# It is there for purpose. (Bug #602093)
CHECK_SYMS=`$(SED) -n -e 's/^..*CK_EXPORT[ ]\{1,\}\([_0-9ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz]\{1,\}\)..*$$/\1/p' @top_srcdir@/libs/gst/check/libcheck/check.h.in`
print-check-symbols:
	@for s in $(CHECK_SYMS); do \
	  echo $$s; \
	done;

$(SYMBOLS_FILE): $(libgstcheck_@GST_API_VERSION@include_HEADERS) libcheck/check.h
	$(AM_V_GEN)rm -f $(SYMBOLS_FILE) 2>/dev/null; \
	for s in $(LIBGSTCHECK_EXPORTED_SYMBOLS) $(CHECK_SYMS); do \
	  echo $$s >> $(SYMBOLS_FILE); \
	done

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstCheck-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstcheck_@GST_API_VERSION@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstcheck_@GST_API_VERSION@_la_SOURCES))

GstCheck-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstcheck-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstCheck \
		--nsversion=@GST_API_VERSION@ \
		--strip-prefix=Gst \
		--warn-all \
		-I$(top_srcdir) \
		-I$(top_srcdir)/libs \
		-I$(top_builddir) \
		-I$(top_builddir)/libs \
		--c-include "gst/check/check.h" \
		--add-include-path=$(top_builddir)/gst \
		--library-path=$(top_builddir)/gst \
		--library=libgstcheck-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--libtool="${LIBTOOL}" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg-export gstreamer-check-@GST_API_VERSION@ \
		--add-init-section="gst_init(NULL,NULL);" \
		--output $@ \
		$(gir_headers) \
		$(gir_sources)

# INTROSPECTION_GIRDIR/INTROSPECTION_TYPELIBDIR aren't the right place to
# install anything - we need to install inside our prefix.
girdir = $(datadir)/gir-1.0
gir_DATA = $(BUILT_GIRSOURCES)

typelibsdir = $(libdir)/girepository-1.0/

typelibs_DATA = $(BUILT_GIRSOURCES:.gir=.typelib)

%.typelib: %.gir $(INTROSPECTION_COMPILER)
	$(AM_V_GEN)$(INTROSPECTION_COMPILER) --includedir=$(srcdir) --includedir=$(builddir) --includedir=$(top_builddir)/gst $(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
