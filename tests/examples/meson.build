# FIXME - Add other missing examples!
#subdir('audiomixmatrix')
#subdir('avsamplesink')
#subdir('camerabin2')
#subdir('codecparsers')
subdir('compositor')
#subdir('directfb')
#subdir('gl')
#subdir('gtk')
#subdir('ipcpipeline')
subdir('mpegts')
#subdir('mxf')
#subdir('opencv')
#subdir('qt')
#subdir('uvch264')
#subdir('waylandsink')

if build_gstgl and gstgl_dep.found()
  executable('playout',
      'playout.c',
      install: true,
      dependencies : [gstgl_dep],
      c_args : ['-DGST_USE_UNSTABLE_API', ],
      )
endif
