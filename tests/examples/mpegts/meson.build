executable('tsparser',
  'ts-parser.c',
  install: true,
  include_directories : [configinc],
  dependencies : [gstmpegts_dep],
  c_args : ['-DHAVE_CONFIG_H=1' ],
)
