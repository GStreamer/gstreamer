wl_req = '>= 1.15'
wl_client_dep = dependency('wayland-client', version: wl_req, required: get_option('wayland'))
libdrm_dep = dependency('libdrm', version: '>= 2.4.98', required: get_option('wayland'))
wl_proto_req = '>= 1.44'
wl_protocol_dep = dependency('wayland-protocols', version: wl_proto_req, required: get_option('wayland'))
wl_scanner = find_program('wayland-scanner', required: get_option('wayland'))
# Also used in ext/wayland
use_wayland = wl_protocol_dep.found() and wl_client_dep.found() and wl_scanner.found() and libdrm_dep.found()

if use_wayland
  wl_sources = files([
      'gstwlbuffer.c',
      'gstwlcontext.c',
      'gstwldisplay.c',
      'gstwllinuxdmabuf.c',
      'gstwlshmallocator.c',
      'gstwlvideobufferpool.c',
      'gstwlvideoformat.c',
      'gstwlwindow.c',
  ])

  wl_headers = files([
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
  ])

  protocols_datadir = wl_protocol_dep.get_variable('pkgdatadir')

  protocol_defs = [
    ['color-management', 'staging', 'v1' ],
    ['color-representation', 'staging', 'v1' ],
    ['viewporter', 'stable', ],
    ['linux-dmabuf', 'unstable', 'v1', ],
    ['fullscreen-shell', 'unstable', 'v1', ],
    ['xdg-shell', 'stable', ],
    ['single-pixel-buffer', 'staging', 'v1' ],
  ]
  protocols_files = []

  foreach protodef: protocol_defs
    proto_name = protodef[0]
    proto_stability = protodef[1]

    if proto_stability == 'internal'
      base_file = proto_name
      xml_path = 'protocols' / proto_name + '.xml'
    elif proto_stability == 'stable'
      base_file = proto_name
      xml_path = protocols_datadir / 'stable' / proto_name / (base_file + '.xml')
    elif proto_stability == 'unstable'
      base_file = '@0@-unstable-@1@'.format(proto_name, protodef[2])
      xml_path = protocols_datadir / 'unstable' / proto_name / (base_file + '.xml')
    elif proto_stability == 'staging'
      base_file = '@0@-@1@'.format(proto_name, protodef[2])
      xml_path = protocols_datadir / 'staging' / proto_name / (base_file + '.xml')
    else
      error('Unsupported protocol stability')
    endif

    protocols_files += [custom_target(f'@base_file@ client header',
      input: xml_path,
      output: f'@base_file@-client-protocol.h',
      command: [
        wl_scanner,
        'client-header',
        '@INPUT@', '@OUTPUT@',
      ],
    )]

    protocols_files += [custom_target(f'@base_file@ source',
      input: xml_path,
      output: f'@base_file@-protocol.c',
      command: [
        wl_scanner,
        'private-code',
        '@INPUT@', '@OUTPUT@',
      ],
    )]
  endforeach

  extra_c_args = [
    '-DGST_USE_UNSTABLE_API',
    '-DBUILDING_GST_WL',
    '-DG_LOG_DOMAIN="GStreamer-Wayland"',
    '-D_GNU_SOURCE'
  ]

  if wl_client_dep.version().version_compare('>= 1.22.91')
    extra_c_args += [ '-DHAVE_WL_EVENT_QUEUE_NAME' ]
  endif

  gstwayland = library('gstwayland-' + api_version,
    wl_sources + protocols_files,
    c_args : gst_plugins_bad_args + extra_c_args,
    include_directories : [configinc, libsinc],
    version : libversion,
    soversion : soversion,
    darwin_versions : osxversion,
    install : true,
    dependencies : [gst_dep, gstallocators_dep, gstvideo_dep, libdrm_dep,
                    wl_client_dep]
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
                    wl_client_dep])

  install_headers(wl_headers, subdir: 'gstreamer-1.0/gst/wayland')
  meson.override_dependency(pkg_name, gstwayland_dep)
endif
