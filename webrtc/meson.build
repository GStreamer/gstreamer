project('gstwebrtc-demo', 'c',
  meson_version : '>= 0.48',
  default_options : [ 'warning_level=1',
                      'buildtype=debug' ])

cc = meson.get_compiler('c')

if cc.get_id() == 'msvc'
  add_project_arguments(
      cc.get_supported_arguments(['/utf-8']), # set the input encoding to utf-8
      language : 'c')
endif

gst_req = '>= 1.14.0'
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstsdp_dep = dependency('gstreamer-sdp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'sdp_dep'])
gstwebrtc_dep = dependency('gstreamer-webrtc-1.0', version : gst_req,
    fallback : ['gst-plugins-bad', 'gstwebrtc_dep'])

libsoup_dep = dependency('libsoup-2.4', version : '>=2.48',
    fallback : ['libsoup', 'libsoup_dep'])
json_glib_dep = dependency('json-glib-1.0',
    fallback : ['json-glib', 'json_glib_dep'])

subdir('multiparty-sendrecv')
subdir('sendrecv')
