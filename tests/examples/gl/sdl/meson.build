sdl_dep = dependency('sdl', version : '>=1.2.0', required : get_option('examples'))
if sdl_dep.found() and build_gstgl
  executable('sdlshare',
    'sdlshare.c',
    install: false,
    dependencies : [gst_base_dep, gstgl_dep, sdl_dep, gl_dep])

  executable('sdlshare2',
    'sdlshare2.c',
    install: false,
    dependencies : [gst_base_dep, gstgl_dep, sdl_dep, gl_dep])
endif
