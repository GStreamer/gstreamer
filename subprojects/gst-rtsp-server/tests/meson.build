# FIXME: make check work on windows
if get_option('tests').disabled() or static_build or host_machine.system() == 'windows'
  subdir_done()
endif

if gstcheck_dep.found()
  subdir('check')
endif

test_cleanup_exe = executable('test-cleanup', 'test-cleanup.c',
  dependencies: gst_rtsp_server_dep)

test_reuse_exe = executable('test-reuse', 'test-reuse.c',
  dependencies: gst_rtsp_server_dep)

test('test-cleanup', test_cleanup_exe)
test('test-reuse', test_reuse_exe)
