gst_req = '>= 1.14.0'
gstsdp_dep = dependency('gstreamer-sdp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'sdp_dep'])

libsoup_dep = dependency('libsoup-2.4', version : '>=2.48',
    fallback : ['libsoup', 'libsoup_dep'], required: false)
json_glib_dep = dependency('json-glib-1.0',
    fallback : ['json-glib', 'json_glib_dep'], required: false)

if not libsoup_dep.found() or not json_glib_dep.found()
    message('WebRTC examples are disabled because some dependencies are missing.')
    subdir_done()
endif

libgstwebrtcnice_dep = dependency('gstreamer-webrtc-nice-1.0', version : gst_req)

py3_mod = import('python3')
py3 = py3_mod.find_python()

py3_version = py3_mod.language_version()
if py3_version.version_compare('< 3.6')
  error('Could not find a sufficient python version required: 3.6, found {}'.format(py3_version))
endif

subdir('multiparty-sendrecv')
subdir('signalling')
subdir('sendonly')
subdir('sendrecv')

if not get_option('tests').disabled()
    subdir('check')
endif
