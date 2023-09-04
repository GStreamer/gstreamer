wl_req = '>= 1.15'
wl_client_dep = dependency('wayland-client', version: wl_req, required: get_option('wayland'))
libdrm_dep = dependency('libdrm', version: '>= 2.4.98', required: get_option('wayland'))
wl_protocol_dep = dependency('wayland-protocols', version: wl_req, required: get_option('wayland'))
wl_scanner = find_program('wayland-scanner', required: get_option('wayland'))
# Also used in ext/wayland
use_wayland = wl_protocol_dep.found() and wl_client_dep.found() and wl_scanner.found() and libdrm_dep.found()

if use_wayland
  wl_sources = [
      'gstwlbuffer.c',
      'gstwlcontext.c',
      'gstwldisplay.c',
      'gstwllinuxdmabuf.c',
      'gstwlshmallocator.c',
      'gstwlvideobufferpool.c',
      'gstwlvideoformat.c',
      'gstwlwindow.c',
  ]

    wl_headers = [
      'gstwl_fwd.h',
      'gstwlbuffer.h',
      'gstwlcontext.h',
      'gstwldisplay.h',
      'gstwllinuxdmabuf.h',
      'gstwlshmallocator.h',
      'gstwlvideobufferpool.h',
      'gstwlvideoformat.h',
      'gstwlwindow.h',
      'wayland.h',
      'wayland-prelude.h',
  ]

  protocols_datadir = wl_protocol_dep.get_variable('pkgdatadir')

  protocol_defs = [
      ['/stable/viewporter/viewporter.xml', 'viewporter-protocol.c', 'viewporter-client-protocol.h'],
      ['/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml',
       'linux-dmabuf-unstable-v1-protocol.c', 'linux-dmabuf-unstable-v1-client-protocol.h'],
      ['/unstable/fullscreen-shell/fullscreen-shell-unstable-v1.xml',
       'fullscreen-shell-unstable-v1-protocol.c', 'fullscreen-shell-unstable-v1-client-protocol.h'],
      ['/stable/xdg-shell/xdg-shell.xml', 'xdg-shell-protocol.c', 'xdg-shell-client-protocol.h'],
  ]
  protocols_files = []

  foreach protodef: protocol_defs
      xmlfile = protocols_datadir + protodef.get(0)

      protocols_files += [custom_target(protodef.get(1),
        output : protodef.get(1),
        input : xmlfile,
        command : [wl_scanner, 'code', '@INPUT@', '@OUTPUT@'])]

      protocols_files += [custom_target(protodef.get(2),
        output : protodef.get(2),
        input : xmlfile,
        command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])]
  endforeach

  extra_c_args = [
    '-DGST_USE_UNSTABLE_API',
    '-DBUILDING_GST_WL',
    '-DG_LOG_DOMAIN="GStreamer-Wayland"',
    '-D_GNU_SOURCE'
  ]

  gstwayland = library('gstwayland-' + api_version,
    wl_sources + protocols_files,
    c_args : gst_plugins_bad_args + extra_c_args,
    include_directories : [configinc, libsinc],
    version : libversion,
    soversion : soversion,
    darwin_versions : osxversion,
    install : true,
    dependencies : [gst_dep, gstallocators_dep, gstvideo_dep, libdrm_dep,
                    wl_client_dep, wl_protocol_dep]
  )

  pkg_name = 'gstreamer-wayland-1.0'
  gst_libraries += [[pkg_name, {'lib': gstwayland}]]
  pkgconfig.generate(gstwayland,
    libraries : [gst_dep, gstvideo_dep],
    variables : pkgconfig_variables,
    subdirs : pkgconfig_subdirs,
    name : pkg_name,
    description : 'GStreamer Wayland support',
  )

  gstwayland_dep = declare_dependency(link_with : gstwayland,
    include_directories : [libsinc],
    dependencies : [gst_dep, gstallocators_dep, gstvideo_dep, libdrm_dep,
                    wl_client_dep, wl_protocol_dep])

  install_headers(wl_headers, subdir: 'gstreamer-1.0/gst/wayland')
  meson.override_dependency(pkg_name, gstwayland_dep)
endif
