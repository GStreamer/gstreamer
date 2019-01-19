msdk_sources = [
  'gstmsdk.c',
  'gstmsdkbufferpool.c',
  'gstmsdkcontext.c',
  'gstmsdkcontextutil.c',
  'gstmsdkdec.c',
  'gstmsdkdecproputil.c',
  'gstmsdkenc.c',
  'gstmsdkh264dec.c',
  'gstmsdkh264enc.c',
  'gstmsdkh265dec.c',
  'gstmsdkh265enc.c',
  'gstmsdkmjpegdec.c',
  'gstmsdkmjpegenc.c',
  'gstmsdkmpeg2dec.c',
  'gstmsdkmpeg2enc.c',
  'gstmsdksystemmemory.c',
  'gstmsdkvc1dec.c',
  'gstmsdkvideomemory.c',
  'gstmsdkvp8dec.c',
  'gstmsdkvp8enc.c',
  'gstmsdkvpp.c',
  'gstmsdkvpputil.c',
  'msdk-enums.c',
  'msdk.c',
]

if host_machine.system() == 'windows'
  msdk_sources += ['msdk_d3d.c', 'gstmsdkallocator_d3d.c' ]
else
  msdk_sources += ['msdk_libva.c', 'gstmsdkallocator_libva.c']
endif

have_msdk = false
msdk_dep = []

msdk_option = get_option('msdk')
if msdk_option.disabled()
  subdir_done()
endif

mfx_dep = dependency('libmfx', required: false)
if mfx_dep.found()
  mfx_incdir = mfx_dep.get_pkgconfig_variable('includedir')
else
  # Old versions of MediaSDK don't provide a pkg-config file
  mfx_root = run_command(python3, '-c', 'import os; print(os.environ.get("INTELMEDIASDKROOT", os.environ.get("MFX_HOME", "")))').stdout().strip()

  if mfx_root != ''
    mfx_libdir = [mfx_root + '/lib/lin_x64', mfx_root + '/lib/x64', mfx_root + '/lib64', mfx_root + '/lib']
    mfx_incdir = join_paths([mfx_root, 'include'])
    mfx_lib = cxx.find_library('mfx', dirs: mfx_libdir, required: msdk_option)
    mfx_inc = include_directories(mfx_incdir)
    mfx_dep = declare_dependency(include_directories: mfx_inc, dependencies: mfx_lib)
  elif msdk_option.enabled()
    error('msdk plugin enabled but Intel Media SDK not found: consider setting PKG_CONFIG_PATH, INTELMEDIASDKROOT or MFX_HOME')
  else
    subdir_done()
  endif
endif

# Old versions of MediaSDK don't have the 'mfx' directory prefix
if cxx.has_header('mfx/mfxdefs.h', args: '-I' + mfx_incdir)
  mfx_incdir = join_paths([mfx_incdir, 'mfx'])
endif

if cxx.has_header('mfxvp9.h', args: '-I' + mfx_incdir)
  msdk_sources += [ 'gstmsdkvp9dec.c' ]
  cdata.set10('USE_MSDK_VP9_DEC', 1)
endif

if host_machine.system() == 'windows'
  if cc.get_id() != 'msvc' and msdk_option.enabled()
    error('msdk plugin can only be built with MSVC')
  endif
  legacy_stdio_dep = cc.find_library('legacy_stdio_definitions', required: get_option('msdk'))
  d3d11_dep = cc.find_library('d3d11', required: get_option('msdk'))
  msdk_deps = declare_dependency(dependencies: [d3d11_dep, legacy_stdio_dep])
  msdk_deps_found = d3d11_dep.found() and legacy_stdio_dep.found() and cc.get_id() == 'msvc'
else
  libva_dep = dependency('libva-drm', required: get_option('msdk'))
  libdl_dep = cc.find_library('dl', required: get_option('msdk'))
  libgudev_dep = dependency('gudev-1.0', required: get_option('msdk'))
  msdk_deps = declare_dependency(dependencies: [libva_dep, libdl_dep, libgudev_dep])
  msdk_deps_found = libva_dep.found() and libdl_dep.found() and libgudev_dep.found()
endif

if msdk_deps_found
  gstmsdktag = library('gstmsdk',
    msdk_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, gstallocators_dep, mfx_dep, msdk_deps],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstmsdktag, install_dir : plugins_pkgconfig_install_dir)
  have_msdk = true
endif
