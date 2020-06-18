tests = [
  ['basic',  'basic.py'],
]

test_deps = [certs]

foreach elem : tests
  test(elem.get(0),
    py3,
    depends: test_deps,
    args : files(elem.get(1)),
    env : ['PYTHONPATH=' + join_paths(meson.source_root(), 'sendrecv', 'gst') + ':' + join_paths(meson.source_root(), 'signalling'),
           'TEST_HTML_SOURCE=' + join_paths(meson.source_root(), 'sendrecv', 'js'),
           'TEST_CA_CERT_PATH=' + join_paths(meson.build_root(), 'signalling')])
endforeach
