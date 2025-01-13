mp4_sources = [
  'isomp4-plugin.c',
  'gstisomp4element.c',
  'gstrtpxqtdepay.c',
  'qtdemux.c',
  'qtdemux_types.c',
  'qtdemux_dump.c',
  'qtdemux_lang.c',
  'qtdemux_tags.c',
  'qtdemux_tree.c',
  'qtdemux-webvtt.c',
  'gstisoff.c',
  'gstqtmux.c',
  'gstqtmoovrecover.c',
  'atoms.c',
  'atomsrecovery.c',
  'descriptors.c',
  'properties.c',
  'gstqtmuxmap.c'
]

mp4_headers = [
  'qtdemux.h',
  'gstqtmux.h',
  'gstisoff.h',
  'qtdemux_dump.h',
  'qtdemux_lang.h',
  'qtpalette.h',
  'gstisomp4elements.h',
  'gstrtpxqtdepay.h',
  'gstqtmoovrecover.h',
  'qtdemux_types.h',
  'properties.h',
  'qtdemux_debug.h',
  'qtdemux_tags.h',
  'gstqtmux-doc.h',
  'qtdemux-webvtt.h',
  'atoms.h',
  'descriptors.h',
  'gstqtmuxmap.h',
  'fourcc.h',
  'atomsrecovery.h',
  'qtdemux_tree.h',
  'qtatomparser.h',
]

doc_sources = []
foreach s: mp4_sources + mp4_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'isomp4': pathsep.join(doc_sources)
}

if get_option('isomp4').disabled()
  subdir_done()
endif

gstisomp4 = library('gstisomp4',
  mp4_sources,
  c_args : gst_plugins_good_args,
  link_args : noseh_link_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep, gstriff_dep, gstaudio_dep, gstvideo_dep,
                  gstrtp_dep, gsttag_dep, gstpbutils_dep, zlib_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstisomp4]

install_data(sources: 'GstQTMux.prs', install_dir: presetdir)

env = environment()
env.prepend('GST_PRESET_PATH', meson.current_source_dir())
meson.add_devenv(env)
