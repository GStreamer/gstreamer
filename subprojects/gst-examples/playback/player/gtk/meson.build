gtk_dep = dependency('gtk+-3.0', required: false)
x11_dep = dependency('x11', required: false)

if host_system == 'darwin'
  extra_c_args = ['-xobjective-c']
else
  extra_c_args = []
endif

if gtk_dep.found()
  gnome = import('gnome')

  gtk_play_resources = gnome.compile_resources('gtk-play-resources', 'resources/gresources.xml',
    source_dir : './resources',
    c_name : 'gtk_play')

  executable('gtk-play',
      sources: ['gtk-play.c',
        gtk_play_resources,
       'gtk-video-renderer.h',
       'gtk-video-renderer.c'],
      c_args :  extra_c_args,
      dependencies : [gst_dep, gsttag_dep, gstplay_dep, gtk_dep, x11_dep])
endif
