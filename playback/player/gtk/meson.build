glib_dep = dependency('glib-2.0', version: '>= 2.38')
gobject_dep = dependency('gobject-2.0', version: '>= 2.38')
gmodule_dep = dependency('gmodule-2.0')
gst_dep = dependency('gstreamer-1.0')
gsttag_dep = dependency('gstreamer-tag-1.0')
gstplayer_dep = dependency('gstreamer-player-1.0', version: '>= 1.7.1.1')
gtk_dep = dependency('gtk+-3.0')
x11_dep = dependency('x11', required: false)

gnome = import('gnome')

gtk_play_resources = gnome.compile_resources('gtk-play-resources', 'resources/gresources.xml',
  source_dir : './resources',
  c_name : 'gtk_play')

executable('gtk-play',
    sources: ['gtk-play.c',
      gtk_play_resources,
     'gtk-video-renderer.h',
     'gtk-video-renderer.c'],
    dependencies : [glib_dep, gobject_dep, gmodule_dep, gst_dep, gsttag_dep, gstplayer_dep, gtk_dep, x11_dep])

