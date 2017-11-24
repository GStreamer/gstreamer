# FIXME - Add other missing examples!
subdir('compositor')
subdir('mpegts')

if build_gstgl and gstgl_dep.found()
  executable('playout',
      'playout.c',
      install: true,
      dependencies : [gstgl_dep],
      c_args : ['-DGST_USE_UNSTABLE_API', ],
      )
endif
