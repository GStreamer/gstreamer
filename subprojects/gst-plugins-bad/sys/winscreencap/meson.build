winscreencap_sources = [
  'gstdx9screencapsrc.c',
  'gstgdiscreencapsrc.c',
  'gstwinscreencap.c',
]

have_dxgi = false
dxgi_c_args = []
dxgiscreencap_sources = []
dxgi_dep = []

if host_system != 'windows' or get_option('winscreencap').disabled()
  subdir_done()
endif

d3d_dep = cc.find_library('d3d9', required : get_option('winscreencap'))
gdi_dep = cc.find_library('gdi32', required : get_option('winscreencap'))
d3d11_dep = cc.find_library('d3d11', required : false)
dxgi_lib_dep = cc.find_library('dxgi', required : false)
windowscodecs_dep = cc.find_library('windowscodecs',  required : false)
dxguid_dep = cc.find_library('dxguid',  required : false)

have_d3d11_h = cc.has_header('d3d11.h')
have_dxgi_h = cc.has_header('dxgi1_2.h')
have_d3dcompiler_h = cc.has_header('d3dcompiler.h')
have_versionhelpers_h = cc.has_header('versionhelpers.h')

have_d3d9_h = cc.has_header('d3d9.h')
if not have_d3d9_h and get_option('winscreencap').enabled()
  error('winscreencap plugin enabled but d3d9.h not found')
endif

have_dxgi = d3d11_dep.found() and dxgi_lib_dep.found() and windowscodecs_dep.found() and dxguid_dep.found() and have_d3d11_h and have_dxgi_h and have_d3dcompiler_h and have_versionhelpers_h

if have_dxgi
  dxgi_c_args += ['-DHAVE_DXGI_CAP']
  dxgiscreencap_sources += ['dxgicapture.c', 'gstdxgiscreencapsrc.c']
  dxgi_dep += [gmodule_dep, d3d11_dep, dxgi_lib_dep, windowscodecs_dep, dxguid_dep]
endif

if d3d_dep.found() and gdi_dep.found() and have_d3d9_h
  gstwinscreencap = library('gstwinscreencap',
    winscreencap_sources + dxgiscreencap_sources,
    c_args : gst_plugins_bad_args + dxgi_c_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, d3d_dep, gdi_dep] + dxgi_dep,
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstwinscreencap, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstwinscreencap]
endif
