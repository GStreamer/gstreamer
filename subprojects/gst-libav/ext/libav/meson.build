sources = [
    'gstav.c',
    'gstavprotocol.c',
    'gstavcodecmap.c',
    'gstavutils.c',
    'gstavaudenc.c',
    'gstavvidenc.c',
    'gstavauddec.c',
    'gstavviddec.c',
    'gstavcfg.c',
    'gstavdemux.c',
    'gstavmux.c',
    'gstavdeinterlace.c',
    'gstavvidcmp.c',
]

gstlibav_plugin = library('gstlibav',
    sources,
    c_args : gst_libav_args,
    include_directories : [configinc],
    dependencies : libav_deps + [gst_dep, gstbase_dep, gstvideo_dep,
        gstaudio_dep, gstpbutils_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
plugins += [gstlibav_plugin]
