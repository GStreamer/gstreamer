hlsl_sources = [
  ['ps-checker-luma', 'PSMain_checker_luma', 'ps_5_0'],
  ['ps-checker-rgb', 'PSMain_checker_rgb', 'ps_5_0'],
  ['ps-checker-vuya', 'PSMain_checker_vuya', 'ps_5_0'],
  ['ps-checker', 'PSMain_checker', 'ps_5_0'],
  ['ps-color', 'PSMain_color', 'ps_5_0'],
  ['ps-sample-premul', 'PSMain_sample_premul', 'ps_5_0'],
  ['ps-sample', 'PSMain_sample', 'ps_5_0'],
  ['ps-snow', 'PSMain_snow', 'ps_5_0'],
  ['vs-color', 'VSMain_color', 'vs_5_0'],
  ['vs-coord', 'VSMain_coord', 'vs_5_0'],
  ['vs-pos', 'VSMain_pos', 'vs_5_0'],
]

foreach shader : hlsl_sources
  basename = shader.get(0)
  source = files('@0@.hlsl'.format(basename))
  header = '@0@.h'.format(basename)
  compiled_shader = custom_target(header,
      input : source,
      output : header,
      command : [fxc, '/Fh', '@OUTPUT@',
                '/E', shader.get(1),
                '/T', shader.get(2),
                '/D', 'BUILDING_HLSL=1',
                '@INPUT@'])
  hlsl_precompiled += [compiled_shader]
endforeach
