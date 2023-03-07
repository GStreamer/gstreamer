project('gst-examples', 'c', version : '1.23.0.1', license : 'LGPL')

static_build = get_option('default_library') == 'static'
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

if cc.get_id() == 'msvc'
  add_project_arguments(
      cc.get_supported_arguments(['/utf-8']), # set the input encoding to utf-8
      language : 'c')
endif

host_system = host_machine.system()

gst_version = meson.project_version()
version_arr = gst_version.split('.')

gst_version_major = version_arr[0].to_int()
gst_version_minor = version_arr[1].to_int()

gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

gio_dep = dependency('gio-2.0')

gst_dep = dependency('gstreamer-1.0', version: gst_req,
    fallback : ['gstreamer', 'gst_dep'])
gstplay_dep = dependency('gstreamer-play-1.0', version: gst_req,
    fallback: ['gst-plugins-bad', 'gstplay_dep'])
gsttag_dep = dependency('gstreamer-tag-1.0', version: gst_req,
    fallback: ['gst-plugins-base', 'tag_dep'])
gstwebrtc_dep = dependency('gstreamer-webrtc-1.0', version: gst_req,
    fallback: ['gst-plugins-bad', 'gstwebrtc_dep'])
gstsdp_dep = dependency('gstreamer-sdp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'sdp_dep'])
gstrtp_dep = dependency('gstreamer-rtp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtp_dep'])

if static_build
  subdir_done()
endif

subdir('playback')
subdir('network')
subdir('webrtc')
