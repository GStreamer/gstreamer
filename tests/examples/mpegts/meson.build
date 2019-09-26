foreach fname : ['ts-parser.c', 'ts-section-writer.c', 'ts-scte-writer.c']
  exe_name = fname.split('.').get(0).underscorify()

  executable(exe_name,
    fname,
    install: false,
    include_directories : [configinc],
    dependencies : [gstmpegts_dep],
    c_args : ['-DHAVE_CONFIG_H=1', '-DGST_USE_UNSTABLE_API' ],
  )
endforeach
