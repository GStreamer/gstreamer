sdl_dep = dependency('sdl', version : '>=1.2.0', required : false)
if sdl_dep.found() and build_gstgl
  executable('sdlshare',
    'sdlshare.c',
    install: false,
    dependencies : [gstbase_dep, gstgl_dep, sdl_dep, gl_dep],
    c_args : ['-DGST_USE_UNSTABLE_API', ],
  )

  executable('sdlshare2',
    'sdlshare2.c',
    install: false,
    dependencies : [gstbase_dep, gstgl_dep, sdl_dep, gl_dep],
    c_args : ['-DGST_USE_UNSTABLE_API', ],
)
endif
