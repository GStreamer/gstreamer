kmssink_sources = [
  'gstkmsallocator.c',
  'gstkmsbufferpool.c',
  'gstkmsedid.c',
  'gstkmssink.c',
  'gstkmsutils.c',
]
extra_deps = []

if host_system != 'linux'
  subdir_done()
endif

libdrm_dep = dependency('libdrm', version : '>= 2.4.108',
                        required : get_option('kms'))
mathlib = cc.find_library('m', required : false)

if libdrm_dep.found()
  gstkmssink = library('gstkms',
    kmssink_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstallocators_dep, libdrm_dep, mathlib],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstkmssink]
endif
