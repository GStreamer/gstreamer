have_oss4 = true
message('Checking headers needed for Open Sound System 4 plugin...')
foreach hdr : ['fcntl.h', 'sys/ioctl.h', 'sys/stat.h', 'sys/types.h']
  if have_oss4
    if not cc.has_header(hdr)
      have_oss4 = false
    endif
  endif
endforeach

if have_oss4
  message('Required headers found, building Open Sound System 4 plugin.')

  library('gstoss4',
    'oss4-audio.c', 'oss4-property-probe.c', 'oss4-sink.c', 'oss4-source.c',
    c_args : gst_plugins_good_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, gstbase_dep],
    install : true,
    install_dir : plugins_install_dir)
else
  message('Not building Open Sound System 4 plugin, required headers not found.')
endif
