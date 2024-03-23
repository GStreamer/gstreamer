hlsl_sources = [
  ['PSMain_checker_luma', 'ps'],
  ['PSMain_checker_rgb', 'ps'],
  ['PSMain_checker_vuya', 'ps'],
  ['PSMain_checker', 'ps'],
  ['PSMain_color', 'ps'],
  ['PSMain_sample_premul', 'ps'],
  ['PSMain_sample', 'ps'],
  ['PSMain_snow', 'ps'],
  ['VSMain_color', 'vs'],
  ['VSMain_coord', 'vs'],
  ['VSMain_pos', 'vs'],
]

shader_model = '5_0'

plugin_hlsl_precompiled = []

foreach shader : hlsl_sources
  entry_point_prefix = shader.get(0)
  target_prefix = shader.get(1)
  source = files('@0@.hlsl'.format(entry_point_prefix))
  entry_point = '@0@_@1@'.format(entry_point_prefix, shader_model)
  header = '@0@.h'.format(entry_point)
  sm_target = '@0@_@1@'.format(target_prefix, shader_model)
  compiled_shader = custom_target(header,
      input : source,
      output : header,
      command : [fxc, '/Fh', '@OUTPUT@',
                '/E', entry_point,
                '/T', sm_target,
                '/D', 'BUILDING_HLSL=1',
                '/D', 'ENTRY_POINT=@0@'.format(entry_point),
                '/nologo',
                '@INPUT@'])
  plugin_hlsl_precompiled += [compiled_shader]
endforeach

header_collector = find_program('collect_hlsl_headers.py')

plugin_ps_collection = custom_target('plugin_hlsl_ps',
    input : plugin_hlsl_precompiled,
    output : 'plugin_hlsl_ps.h',
    command : [header_collector,
        '--input', meson.current_build_dir(),
        '--prefix', 'PSMain_',
        '--name', 'g_plugin_ps_table',
        '--output', '@OUTPUT@'
    ])

plugin_vs_collection = custom_target('plugin_hlsl_vs',
    input : plugin_hlsl_precompiled,
    output : 'plugin_hlsl_vs.h',
    command : [header_collector,
        '--input', meson.current_build_dir(),
        '--prefix', 'VSMain_',
        '--name', 'g_plugin_vs_table',
        '--output', '@OUTPUT@'
    ])

hlsl_precompiled += [
  plugin_hlsl_precompiled,
  plugin_ps_collection,
  plugin_vs_collection,
]
