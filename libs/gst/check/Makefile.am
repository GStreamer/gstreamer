lib_LTLIBRARIES = libgstcheck-@GST_API_VERSION@.la

SYMBOLS_FILE = exports.sym

libgstcheck_@GST_API_VERSION@_la_DEPENDENCIES = \
	$(top_builddir)/gst/libgstreamer-@GST_API_VERSION@.la \
	$(SYMBOLS_FILE)
libgstcheck_@GST_API_VERSION@_la_SOURCES =	\
	gstbufferstraw.c			\
	gstcheck.c				\
	gstconsistencychecker.c			\
	gstharness.c				\
	gsttestclock.c

libgstcheck_@GST_API_VERSION@_la_CFLAGS = $(GST_OBJ_CFLAGS) \
	-UG_DISABLE_ASSERT \
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
	check-prelude.h				\
	gstbufferstraw.h			\
	gstcheck.h				\
	gstconsistencychecker.h			\
	gstharness.h				\
	gsttestclock.h

nodist_libgstcheck_@GST_API_VERSION@include_HEADERS =	\
	internal-check.h

CLEANFILES = *.gcno *.gcda *.gcov $(SYMBOLS_FILE)

%.c.gcov: .libs/libgstcheck_@GST_API_VERSION@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcheck_@GST_API_VERSION@_la_SOURCES:=.gcov)

SUBDIRS = libcheck

LIBGSTCHECK_EXPORTED_VARS = \
	_gst_check_threads_running \
	_gst_check_raised_critical \
	_gst_check_raised_warning \
	_gst_check_expecting_log \
	_gst_check_list_tests \
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
	gst_check_add_log_filter \
	gst_check_remove_log_filter \
	gst_check_clear_log_filter \
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
	gst_check_objects_destroyed_on_unref \
	gst_check_object_destroyed_on_unref \
	gst_consistency_checker_add_pad \
	gst_consistency_checker_new \
	gst_consistency_checker_reset \
	gst_consistency_checker_free \
	gst_harness_add_element_full \
	gst_harness_add_element_src_pad \
	gst_harness_add_element_sink_pad \
	gst_harness_add_parse \
	gst_harness_add_probe \
	gst_harness_add_sink \
	gst_harness_add_sink_harness \
	gst_harness_add_sink_parse \
	gst_harness_add_src \
	gst_harness_add_src_harness \
	gst_harness_add_src_parse \
	gst_harness_buffers_in_queue \
	gst_harness_buffers_received \
	gst_harness_crank_multiple_clock_waits \
	gst_harness_crank_single_clock_wait \
	gst_harness_create_buffer \
	gst_harness_dump_to_file \
	gst_harness_events_in_queue \
	gst_harness_events_received \
	gst_harness_find_element \
	gst_harness_get \
	gst_harness_get_allocator \
	gst_harness_get_last_pushed_timestamp \
	gst_harness_get_testclock \
	gst_harness_new \
	gst_harness_new_empty \
	gst_harness_new_full \
	gst_harness_new_parse \
	gst_harness_new_with_element \
	gst_harness_new_with_padnames \
	gst_harness_new_with_templates \
	gst_harness_play \
	gst_harness_pull \
	gst_harness_pull_event \
	gst_harness_pull_upstream_event \
	gst_harness_push \
	gst_harness_push_and_pull \
	gst_harness_push_event \
	gst_harness_push_from_src \
	gst_harness_push_to_sink \
	gst_harness_query_latency \
	gst_harness_push_upstream_event \
	gst_harness_set \
	gst_harness_set_caps \
	gst_harness_set_caps_str \
	gst_harness_set_drop_buffers \
	gst_harness_set_blocking_push_mode \
	gst_harness_set_forwarding \
	gst_harness_set_propose_allocator \
	gst_harness_set_sink_caps \
	gst_harness_set_src_caps \
	gst_harness_set_src_caps_str \
	gst_harness_set_sink_caps_str \
	gst_harness_set_time \
	gst_harness_set_upstream_latency \
	gst_harness_sink_push_many \
	gst_harness_src_crank_and_push_many \
	gst_harness_src_push_event \
	gst_harness_stress_custom_start \
	gst_harness_stress_property_start_full \
	gst_harness_stress_push_buffer_start_full \
	gst_harness_stress_push_buffer_with_cb_start_full \
	gst_harness_stress_push_event_start_full \
	gst_harness_stress_push_event_with_cb_start_full \
	gst_harness_stress_push_upstream_event_start_full \
	gst_harness_stress_push_upstream_event_with_cb_start_full \
	gst_harness_stress_requestpad_start_full \
	gst_harness_stress_statechange_start_full \
	gst_harness_stress_thread_stop \
	gst_harness_take_all_data \
	gst_harness_take_all_data_as_buffer \
	gst_harness_take_all_data_as_bytes \
	gst_harness_teardown \
	gst_harness_try_pull \
	gst_harness_try_pull_event \
	gst_harness_try_pull_upstream_event \
	gst_harness_upstream_events_in_queue \
	gst_harness_upstream_events_received \
	gst_harness_use_systemclock \
	gst_harness_use_testclock \
	gst_harness_wait_for_clock_id_waits \
	gst_test_clock_advance_time \
	gst_test_clock_crank \
	gst_test_clock_get_next_entry_time \
	gst_test_clock_get_type \
	gst_test_clock_has_id \
	gst_test_clock_id_list_get_latest_time \
	gst_test_clock_new \
	gst_test_clock_new_with_start_time \
	gst_test_clock_peek_id_count \
	gst_test_clock_peek_next_pending_id \
	gst_test_clock_process_id_list \
	gst_test_clock_process_next_clock_id \
	gst_test_clock_set_time \
	gst_test_clock_wait_for_multiple_pending_ids \
	gst_test_clock_wait_for_next_pending_id \
	gst_test_clock_wait_for_pending_id_count

# For backwards compatibility with GStreamer < 1.5
LIBGSTCHECK_EXPORTED_COMPAT_FUNCS = \
	_fail_unless

LIBGSTCHECK_EXPORTED_SYMBOLS = \
	$(LIBGSTCHECK_EXPORTED_VARS) \
	$(LIBGSTCHECK_EXPORTED_FUNCS) \
	$(LIBGSTCHECK_EXPORTED_COMPAT_FUNCS)

# Please do not even think about changing the alphabet below into A-Za-z.
# It is there for purpose. (Bug #602093)
CHECK_SYMS=`$(SED) -n -e 's/^..*CK_EXPORT[ ]\{1,\}\([_0-9ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz]\{1,\}\)..*$$/\1/p' @top_srcdir@/libs/gst/check/libcheck/check.h.in`
print-check-symbols:
	@for s in $(CHECK_SYMS); do \
	  echo $$s; \
	done;

$(SYMBOLS_FILE): $(libgstcheck_@GST_API_VERSION@include_HEADERS) internal-check.h
	$(AM_V_GEN)rm -f $(SYMBOLS_FILE) 2>/dev/null; \
	for s in $(LIBGSTCHECK_EXPORTED_SYMBOLS) $(CHECK_SYMS); do \
	  echo $$s >> $(SYMBOLS_FILE); \
	done

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstCheck-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstcheck_@GST_API_VERSION@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstcheck_@GST_API_VERSION@_la_SOURCES))

GstCheck-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstcheck-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" GI_SCANNER_DISABLE_CACHE=yes\
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_DISABLE=yes \
		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstCheck \
		--nsversion=@GST_API_VERSION@ \
		--identifier-prefix=Gst \
		--symbol-prefix=gst \
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
		--add-init-section="$(INTROSPECTION_INIT)" \
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
