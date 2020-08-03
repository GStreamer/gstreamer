subdir('audiomixmatrix')
subdir('avsamplesink')
subdir('camerabin2')
subdir('codecparsers')
subdir('d3d11videosink')
subdir('directfb')
subdir('ipcpipeline')
subdir('mpegts')
subdir('msdk')
subdir('mxf')
subdir('nvcodec')
subdir('opencv', if_found: opencv_dep)
subdir('uvch264')
subdir('va')
subdir('waylandsink')
subdir('webrtc')

executable('playout',
  'playout.c',
  install: true,
  dependencies : [gstbase_dep, gstvideo_dep],
  c_args : ['-DGST_USE_UNSTABLE_API', ],
)
