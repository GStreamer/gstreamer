xvimage_sources = [
  'xvcontext.c',
  'xvimage.c',
  'xvimageallocator.c',
  'xvimagepool.c',
  'xvimagesink.c',
]

xvideo_dep = dependency('xv', required : false)
core_conf.set('HAVE_XVIDEO', x11_dep.found() and xvideo_dep.found())

if xvideo_dep.found()
  gstxvimage = library('gstxvimagesink',
    xvimage_sources,
    c_args : gst_plugins_base_args,
    include_directories: [configinc, libsinc],
    dependencies : glib_deps + [video_dep, gst_base_dep, gst_dep, x11_dep, xshm_dep, xvideo_dep, libm],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
