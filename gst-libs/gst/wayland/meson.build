wl_req = '>= 1.4'
wl_client_dep = dependency('wayland-client', version: wl_req, required: get_option('wayland'))
libdrm_dep = dependency('libdrm', version: '>= 2.4.55', required: get_option('wayland'))
wl_protocol_dep = dependency('wayland-protocols', version: wl_req, required: get_option('wayland'))
wl_scanner = find_program('wayland-scanner', required: get_option('wayland'))
# Also used in ext/wayland
use_wayland = wl_protocol_dep.found() and wl_client_dep.found() and wl_scanner.found() and libdrm_dep.found()

if use_wayland
  gstwayland = library('gstwayland-' + api_version,
    'wayland.c',
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_WAYLAND'],
    include_directories : [configinc, libsinc],
    version : libversion,
    soversion : soversion,
  darwin_versions : osxversion,
    install : true,
    dependencies : [gst_dep, gstvideo_dep]
  )

  gstwayland_dep = declare_dependency(link_with : gstwayland,
    include_directories : [libsinc],
    dependencies : [gst_dep, gstvideo_dep])
endif
