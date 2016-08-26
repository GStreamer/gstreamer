gst_dep = dependency('gstreamer-1.0')
gstplayer_dep = dependency('gstreamer-player-1.0', version: '>= 1.7.1.1')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : true)

executable('gst-play',
    ['gst-play.c',
     'gst-play-kb.c',
     'gst-play-kb.h'],
    dependencies : [gst_dep, gstplayer_dep, m_dep])

