oss4_sources = [
  'gstoss4audioplugin.c',
  'oss4-audio.c',
  'oss4-property-probe.c',
  'oss4-sink.c',
  'oss4-source.c',
]

oss4_headers = [
  'oss4-property-probe.h',
  'oss4-sink.h',
  'oss4-audio.h',
  'oss4-soundcard.h',
  'oss4-source.h',
]

doc_sources = []
foreach s: oss4_sources + oss4_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'oss4': pathsep.join(doc_sources)
}

have_oss4 = false
oss4_option = get_option('oss4')
if not oss4_option.disabled()
  have_oss4 = true
  message('Checking headers needed for Open Sound System 4 plugin...')
  foreach hdr : ['fcntl.h', 'sys/ioctl.h', 'sys/stat.h', 'sys/types.h']
    if have_oss4 and not cc.has_header(hdr)
      have_oss4 = false
    endif
  endforeach

  if not have_oss4 and oss4_option.enabled()
    error('OSS4 plugin was enabled but headers were not found')
  endif
endif

if have_oss4
  message('Required headers found, building Open Sound System 4 plugin.')

  gstoss4 = library('gstoss4',
    oss4_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, gstbase_dep],
    install : true,
    install_dir : plugins_install_dir)

  plugins += [gstoss4]
else
  message('Not building Open Sound System 4 plugin, required headers not found.')
endif
