wl_req = '>= 1.4'
wl_client_dep = dependency('wayland-client', version: wl_req, required: false)
libdrm_dep = dependency('libdrm', version: '>= 2.4.55', required: false)
wl_protocol_dep = dependency('wayland-protocols', version: wl_req,
                             required: false)
wl_scanner = find_program('wayland-scanner', required: false)
use_wayland = wl_protocol_dep.found() and wl_client_dep.found() and wl_scanner.found() and libdrm_dep.found()

if use_wayland
  gstwayland = library('gstwayland-' + api_version,
    'wayland.c',
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    include_directories : [configinc, libsinc],
    version : libversion,
    soversion : soversion,
    install : true,
    dependencies : [gst_dep, gstvideo_dep]
  )

  gstwayland_dep = declare_dependency(link_with : gstwayland,
    include_directories : [libsinc],
    dependencies : [gst_dep, gstvideo_dep])
endif
