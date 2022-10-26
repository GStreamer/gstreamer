tests = [
  ['basic',  'basic.py'],
]

check_test = find_program ('configure_test_check.py')
have_webrtc_check_deps = run_command (check_test, check: false).returncode() == 0

if openssl.found() and have_webrtc_check_deps
  test_deps = [certs]
  message ('adding webrtc tests')

  foreach elem : tests
    test(elem.get(0),
      py3,
      depends: test_deps,
      args : files(elem.get(1)),
      # XXX: This PYTHONPATH hack is fun
      env : ['PYTHONPATH=' + join_paths(meson.project_source_root(), 'sendrecv', 'gst') + ':' + join_paths(meson.project_source_root(), 'signalling'),
             'TEST_HTML_SOURCE=' + join_paths(meson.project_source_root(), 'sendrecv', 'js'),
             'TEST_CA_CERT_PATH=' + join_paths(meson.project_build_root(), 'signalling')])
  endforeach
endif

env = environment()
env.prepend('GST_VALIDATE_SCENARIOS_PATH', meson.current_source_dir() / 'validate/scenarios')
env.prepend('GST_VALIDATE_APPS_DIR', meson.current_source_dir() / 'validate' /
    'apps')
meson.add_devenv(env)
