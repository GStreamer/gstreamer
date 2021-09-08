project('gst-examples', 'c', version : '1.18.5', license : 'LGPL')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

if cc.get_id() == 'msvc'
  add_project_arguments(
      cc.get_supported_arguments(['/utf-8']), # set the input encoding to utf-8
      language : 'c')
endif

glib_dep = dependency('glib-2.0', version: '>= 2.38',
    fallback: ['glib', 'libglib_dep'])
gio_dep = dependency('gio-2.0',
    fallback: ['glib', 'libgio_dep'])
gobject_dep = dependency('gobject-2.0', version: '>= 2.38',
    fallback: ['glib', 'libgobject_dep'])
gmodule_dep = dependency('gmodule-2.0',
    fallback: ['glib', 'libgmodule_dep'])

gst_dep = dependency('gstreamer-1.0',
    fallback : ['gstreamer', 'gst_dep'])
gstplayer_dep = dependency('gstreamer-player-1.0', version: '>= 1.7.1.1',
    fallback: ['gst-plugins-bad', 'gstplayer_dep'])
gsttag_dep = dependency('gstreamer-tag-1.0',
    fallback: ['gst-plugins-base', 'tag_dep'])
gstwebrtc_dep = dependency('gstreamer-webrtc-1.0', version: '>= 1.14.0',
    fallback: ['gst-plugins-bad', 'gstwebrtc_dep'])
gstsdp_dep = dependency('gstreamer-webrtc-1.0',
    fallback : ['gst-plugins-bad', 'gstwebrtc_dep'])

subdir('playback')
subdir('network')
subdir('webrtc')
