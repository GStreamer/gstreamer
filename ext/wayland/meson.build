wl_sources = [
    'gstwaylandsink.c',
    'wlshmallocator.c',
    'wlbuffer.c',
    'wldisplay.c',
    'wlwindow.c',
    'wlvideoformat.c',
    'wllinuxdmabuf.c'
]

if use_wayland
    protocols_datadir = wl_protocol_dep.get_pkgconfig_variable('pkgdatadir')

    protocol_defs = [
        ['/stable/viewporter/viewporter.xml', 'viewporter-protocol.c', 'viewporter-client-protocol.h'],
        ['/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml',
         'linux-dmabuf-unstable-v1-protocol.c', 'linux-dmabuf-unstable-v1-client-protocol.h']
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

    gstwaylandsink = library('gstwaylandsink',
        wl_sources + protocols_files,
        c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
        include_directories : [configinc],
        dependencies : [gst_dep, gstvideo_dep, gstwayland_dep, gstallocators_dep,
                        wl_client_dep, wl_protocol_dep, libdrm_dep],
        install : true,
        install_dir : plugins_install_dir,
    )
endif
