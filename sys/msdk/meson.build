msdk_sources = [
  'gstmsdk.c',
  'gstmsdkenc.c',
  'gstmsdkh264enc.c',
  'gstmsdkh265enc.c',
  'gstmsdkmpeg2enc.c',
  'gstmsdkvp8enc.c',
  'msdk_libva.c',
]

python3 = find_program('python3')
msdk_root = run_command(python3, '-c', 'import os; print(os.environ.get("INTELMEDIASDKROOT", os.environ.get("MFX_HOME", "")))').stdout().strip()

if msdk_root != ''
  msdk_libdir = msdk_root + '/lib/lin_x64'
  msdk_incdir = include_directories(msdk_root + '/include')
  msdk_lib = cc.find_library('mfx', dirs: msdk_libdir, required: false)
  libva_dep = dependency('libva-drm', required: false)
  libdl_dep = cc.find_library('dl', required: false)
  msdk_dep = declare_dependency(include_directories: msdk_incdir, dependencies: [msdk_lib, libva_dep, libdl_dep])

  if msdk_lib.found() and libva_dep.found() and libdl_dep.found()
    gstmsdktag = library('gstmsdk',
      msdk_sources,
      c_args : gst_plugins_bad_args,
      include_directories : [configinc],
      dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, msdk_dep],
      install : true,
      install_dir : plugins_install_dir,
    )
  endif
endif
