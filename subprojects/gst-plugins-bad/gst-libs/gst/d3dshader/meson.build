d3dshader_sources = [
  'gstd3dcompile.cpp',
  'gstd3dshadercache.cpp',
]

gst_d3dshader_dep = dependency('', required : false)

if host_system != 'windows'
  subdir_done()
endif

extra_args = [
  '-DGST_USE_UNSTABLE_API',
  '-DBUILDING_GST_D3D_SHADER',
  '-DG_LOG_DOMAIN="GStreamer-D3DShader"',
]

sdk_headers = [
  'wrl.h',
  'd3dcompiler.h',
  'dxgi.h',
]

foreach h : sdk_headers
  if not cc.has_header(h)
    subdir_done()
  endif
endforeach

# MinGW 32bits compiler seems to be complaining about redundant-decls
# when ComPtr is in use. Let's just disable the warning
if cc.get_id() != 'msvc'
  extra_args += cc.get_supported_arguments([
    '-Wno-redundant-decls',
  ])
endif

hlsl_precompile_opt = get_option('d3d-hlsl-precompile')

hlsl_precompiled = []
fxc = find_program ('fxc', required : hlsl_precompile_opt)
if not hlsl_precompile_opt.disabled() and cc.get_id() == 'msvc' and fxc.found()
  subdir('converter-hlsl')
  subdir('plugin-hlsl')
  extra_args += ['-DHLSL_PRECOMPILED']
endif

pkg_name = 'gstreamer-d3dshader-' + api_version
gstd3dshader = library('gstd3dshader-' + api_version,
  d3dshader_sources + hlsl_precompiled,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args : gst_plugins_bad_args + extra_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstvideo_dep, gmodule_dep]
)

library_def = {'lib': gstd3dshader}
gst_libraries += [[pkg_name, library_def]]

gstd3dshader_dep = declare_dependency(link_with : gstd3dshader,
  include_directories : [libsinc],
  dependencies : [gstvideo_dep],
  sources : gen_sources)

meson.override_dependency(pkg_name, gstd3dshader_dep)
