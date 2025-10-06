hip_sources = [
  'gsthip-enums.cpp',
  'gsthip-interop.cpp',
  'gsthipbufferpool.cpp',
  'gsthipdevice.cpp',
  'gsthipevent.cpp',
  'gsthiploader.cpp',
  'gsthipmemory.cpp',
  'gsthiprtc.cpp',
  'gsthipstream.cpp',
  'gsthiputils.cpp',
]

hip_headers = [
  'gsthip_fwd.h',
  'gsthip-enums.h',
  'gsthip-interop.h',
  'gsthip.h',
  'gsthipbufferpool.h',
  'gsthipdevice.h',
  'gsthipevent.h',
  'gsthiploader.h',
  'gsthipmemory.h',
  'gsthiprtc.h',
  'gsthipstream.h',
  'gsthiputils.h',
  'hip-prelude.h',
]

hipgst_headers = [
  'hip-gst.h',
]

hip_gl_headers = [
  'gsthip-gl.h',
  'gsthip-interop-gl.h',
]

hipgst_gl_headers = [
  'hip-gst-gl.h',
]

gsthip_dep = dependency('', required : false)
gsthip_gl_dep = dependency('', required : false)

hip_option = get_option('hip')
if hip_option.disabled()
  subdir_done()
endif

if host_system not in ['linux', 'windows']
  subdir_done()
endif

extra_args = [
  '-DGST_USE_UNSTABLE_API',
  '-DBUILDING_GST_HIP',
  '-DG_LOG_DOMAIN="GStreamer-HIP"',
]

extra_deps = []

hip_cdata = configuration_data()
if gstgl_dep.found()
  hip_cdata.set('HAVE_GST_GL', true)
  extra_deps += [gstgl_dep]
endif

configure_file(
  output: 'gsthip-config.h',
  configuration: hip_cdata,
)

hipstub_incdir = include_directories('./stub')

pkg_name = 'gstreamer-hip-' + api_version
gsthip = library('gsthip', hip_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc, libsinc, hipstub_incdir],
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gmodule_dep] + extra_deps,
  version : libversion,
  soversion : soversion,
  install : true,
  override_options : ['cpp_std=c++14'],
)

gen_sources = []
library_def = {'lib': gsthip}

stub_path = meson.current_source_dir() / 'stub'

if build_gir
  gir_includes = ['Gst-1.0', 'GstBase-1.0', 'GstVideo-1.0']

  gir = {
    'sources' : hip_sources + hip_headers,
    'namespace' : 'GstHip',
    'nsversion' : api_version,
    'identifier_prefix' : 'Gst',
    'symbol_prefix' : 'gst',
    'export_packages' : pkg_name,
    'includes' : gir_includes,
    'install' : true,
    'extra_args' : gir_init_section + ['-DGST_USE_UNSTABLE_API', '-I' + stub_path],
    'dependencies' : [gst_dep, gstbase_dep, gstvideo_dep],
  }

  library_def += {'gir': [gir]}
  hip_gir = ['GstHip-1.0']
  if not static_build
    hip_gir = gnome.generate_gir(gsthip, kwargs: gir)
    library_def += {'gir_targets': [hip_gir]}
    gen_sources += hip_gir
  endif

endif
gst_libraries += [[pkg_name, library_def]]

pkgconfig.generate(
  libraries : [gst_dep, gstbase_dep, gstvideo_dep, gsthip],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'GStreamer HIP library',
)

install_headers(hip_headers + hipgst_headers, subdir : 'gstreamer-1.0/gst/hip')
gsthip_dep = declare_dependency(link_with : gsthip,
  include_directories : [libsinc],
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
  sources: gen_sources)
meson.override_dependency(pkg_name, gsthip_dep)

if gstgl_dep.found()
  pkg_name = 'gstreamer-hip-gl-' + api_version

  hip_gl_gir = []
  if build_gir
    gir_includes += ['GstGL-1.0', hip_gir[0]]

    gir = {
      'sources' : hip_gl_headers + ['gsthip-interop.cpp'],
      'namespace' : 'GstHipGL',
      'nsversion' : api_version,
      'identifier_prefix' : 'Gst',
      'symbol_prefix' : 'gst',
      'export_packages' : pkg_name,
      'includes' : gir_includes,
      'install' : true,
      'extra_args' : gir_init_section + ['-DGST_USE_UNSTABLE_API', '-I' + stub_path],
      'dependencies' : [gst_dep, gstbase_dep, gstvideo_dep, gstgl_dep],
    }

    library_def += {'gir': [gir]}
    if not static_build
      hip_gl_gir = gnome.generate_gir(gsthip, kwargs: gir)
      library_def += {'gir_targets':  [hip_gl_gir]}
    endif
  endif

  gst_libraries += [[pkg_name, library_def]]

  pkgconfig.generate(
    libraries : [gst_dep, gstbase_dep, gstvideo_dep, gsthip, gstgl_dep],
    variables : pkgconfig_variables,
    subdirs : pkgconfig_subdirs,
    name : pkg_name,
    description : 'GStreamer HIP library (OpenGL specifics)',
  )

  install_headers(hip_gl_headers + hipgst_gl_headers, subdir : 'gstreamer-1.0/gst/hip')
  gsthip_gl_dep = declare_dependency(link_with : gsthip,
    include_directories : [libsinc],
    dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstgl_dep],
    sources: hip_gl_gir)
  meson.override_dependency(pkg_name, gsthip_gl_dep)
endif
