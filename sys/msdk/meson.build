msdk_sources = [
  'gstmsdk.c',
  'gstmsdkcontext.c',
  'gstmsdkcontextutil.c',
  'gstmsdksystemmemory.c',
  'gstmsdkvideomemory.c',
  'gstmsdkbufferpool.c',
  'gstmsdkdec.c',
  'gstmsdkenc.c',
  'gstmsdkh264dec.c',
  'gstmsdkh264enc.c',
  'gstmsdkh265dec.c',
  'gstmsdkh265enc.c',
  'gstmsdkmjpegdec.c',
  'gstmsdkmjpegenc.c',
  'gstmsdkmpeg2dec.c',
  'gstmsdkmpeg2enc.c',
  'gstmsdkvp8dec.c',
  'gstmsdkvp8enc.c',
  'gstmsdkvc1dec.c',
  'msdk.c',
  'msdk-enums.c'
]

if host_machine.system() == 'windows'
  msdk_sources += ['msdk_d3d.c', 'gstmsdkallocator_d3d.c' ]
else
  msdk_sources += ['msdk_libva.c', 'gstmsdkallocator_libva.c']
endif

msdk_root = run_command(python3, '-c', 'import os; print(os.environ.get("INTELMEDIASDKROOT", os.environ.get("MFX_HOME", "")))').stdout().strip()

have_msdk = false
msdk_dep = []

if msdk_root != ''
  msdk_libdir = [msdk_root + '/lib/lin_x64', msdk_root + '/lib/x64']
  msdk_incdir = include_directories(msdk_root + '/include')
  msdk_lib = cxx.find_library('mfx', dirs: msdk_libdir, required: false)
  if host_machine.system() == 'windows'
    legacy_stdio_dep = cc.find_library('legacy_stdio_definitions', required: false)
    d3d11_dep = cc.find_library('d3d11', required: false)
    msdk_dep = declare_dependency(include_directories: msdk_incdir, dependencies: [msdk_lib, d3d11_dep, legacy_stdio_dep])
    msdk_dep_found = msdk_lib.found() and d3d11_dep.found() and legacy_stdio_dep.found() and cc.get_id() == 'msvc'
  else
    libva_dep = dependency('libva-drm', required: false)
    libdl_dep = cc.find_library('dl', required: false)
    libgudev_dep = dependency('gudev-1.0', required : false)
    msdk_dep = declare_dependency(include_directories: msdk_incdir, dependencies: [msdk_lib, libva_dep, libdl_dep, libgudev_dep])
    msdk_dep_found = msdk_lib.found() and libva_dep.found() and libdl_dep.found() and libgudev_dep.found()
  endif

  if msdk_dep_found
    gstmsdktag = library('gstmsdk',
      msdk_sources,
      c_args : gst_plugins_bad_args,
      include_directories : [configinc],
      dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, msdk_dep],
      install : true,
      install_dir : plugins_install_dir,
    )
    have_msdk = true
  endif
endif
