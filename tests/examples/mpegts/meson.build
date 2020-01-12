executable('tsparser',
  'ts-parser.c',
  install: false,
  include_directories : [configinc],
  dependencies : [gstmpegts_dep],
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
)
