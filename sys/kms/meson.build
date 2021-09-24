kmssink_sources = [
  'gstkmsallocator.c',
  'gstkmsbufferpool.c',
  'gstkmssink.c',
  'gstkmsutils.c',
]

if host_system != 'linux'
  subdir_done()
endif

libdrm_dep = dependency('libdrm', version : '>= 2.4.98',
                        required : get_option('kms'),
                        fallback: ['libdrm', 'ext_libdrm'])
if libdrm_dep.found()
  gstkmssink = library('gstkms',
    kmssink_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstallocators_dep, libdrm_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstkmssink, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstkmssink]
endif
