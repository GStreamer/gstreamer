resindvd_sources = [
  'gstmpegdemux.c',
  'gstmpegdesc.c',
  'gstpesfilter.c',
  'plugin.c',
  'resindvdbin.c',
  'resindvdsrc.c',
  'rsndec.c',
  'rsninputselector.c',
  'rsnparsetter.c',
]

dvdnav_dep = dependency('dvdnav', version : '>= 4.1.2', required : false)
dvdread_dep = dependency('dvdread', version : '>= 4.1.2', required : false)

if dvdnav_dep.found() and dvdread_dep.found()
  gstresindvd = library('gstresindvd',
    resindvd_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstvideo_dep, gstpbutils_dep, gsttag_dep,
                    dvdnav_dep, dvdread_dep, gmodule_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
