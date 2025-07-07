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
  'hip-gst.h',
  'hip-prelude.h',
]

hip_gl_headers = [
  'gsthip-gl.h',
  'gsthip-interop-gl.h',
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

pkgconfig.generate(
  libraries : [gst_dep, gstbase_dep, gstvideo_dep, gsthip],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'GStreamer HIP library',
)

gsthip_dep = declare_dependency(link_with : gsthip,
  include_directories : [libsinc],
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep])
meson.override_dependency(pkg_name, gsthip_dep)
install_headers(hip_headers, subdir : 'gstreamer-1.0/gst/hip')

library_def = {'lib': gsthip}
gst_libraries += [[pkg_name, library_def]]

if gstgl_dep.found()
  pkg_name = 'gstreamer-hip-gl-' + api_version

  pkgconfig.generate(
    libraries : [gst_dep, gstbase_dep, gstvideo_dep, gsthip, gstgl_dep],
    variables : pkgconfig_variables,
    subdirs : pkgconfig_subdirs,
    name : pkg_name,
    description : 'GStreamer HIP library (OpenGL specifics)',
  )

  gsthip_gl_dep = declare_dependency(link_with : gsthip,
    include_directories : [libsinc],
    dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstgl_dep])
  meson.override_dependency(pkg_name, gsthip_gl_dep)
  install_headers(hip_gl_headers, subdir : 'gstreamer-1.0/gst/hip')

  gst_libraries += [[pkg_name, library_def]]
endif
