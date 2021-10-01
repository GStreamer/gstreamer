wl_req = '>= 1.15'
wl_client_dep = dependency('wayland-client', version: wl_req, required: get_option('wayland'))
libdrm_dep = dependency('libdrm', version: '>= 2.4.55', required: get_option('wayland'))
wl_protocol_dep = dependency('wayland-protocols', version: wl_req, required: get_option('wayland'))
wl_scanner = find_program('wayland-scanner', required: get_option('wayland'))
# Also used in ext/wayland
use_wayland = wl_protocol_dep.found() and wl_client_dep.found() and wl_scanner.found() and libdrm_dep.found()

if use_wayland
  gstwayland = library('gstwayland-' + api_version,
    'wayland.c',
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_WAYLAND', '-DG_LOG_DOMAIN="GStreamer-Wayland"'],
    include_directories : [configinc, libsinc],
    version : libversion,
    soversion : soversion,
    darwin_versions : osxversion,
    install : true,
    dependencies : [gst_dep, gstvideo_dep, wl_client_dep]
  )

  pkg_name = 'gstreamer-wayland-1.0'
  libraries += [[pkg_name, {'lib': gstwayland}]]
  pkgconfig.generate(gstwayland,
    libraries : [gst_dep, gstvideo_dep],
    variables : pkgconfig_variables,
    subdirs : pkgconfig_subdirs,
    name : pkg_name,
    description : 'GStreamer Wayland support',
  )

  gstwayland_dep = declare_dependency(link_with : gstwayland,
    include_directories : [libsinc],
    dependencies : [gst_dep, gstvideo_dep])

  install_headers('wayland.h', subdir: 'gstreamer-1.0/gst/wayland')
  meson.override_dependency(pkg_name, gstwayland_dep)
endif
