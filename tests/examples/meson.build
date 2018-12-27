subdir('audiomixmatrix')
subdir('avsamplesink')
subdir('camerabin2')
subdir('codecparsers')
subdir('directfb')
subdir('ipcpipeline')
subdir('mpegts')
subdir('mxf')
subdir('opencv', if_found: opencv_dep)
subdir('uvch264')
subdir('waylandsink')
subdir('webrtc')

executable('playout',
  'playout.c',
  install: true,
  dependencies : [gstbase_dep, gstvideo_dep],
  c_args : ['-DGST_USE_UNSTABLE_API', ],
)
