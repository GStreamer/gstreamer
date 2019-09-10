project('gst-examples', 'c', version : '1.17.0.1', license : 'LGPL')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

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

subdir('playback')
subdir('network')
