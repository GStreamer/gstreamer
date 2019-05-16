# Shaders
gst_vulkan_shader_sources = [
  'identity.frag',
  'identity.vert',
]

bin2array = find_program('bin2array.py')

vulkan_compiled_shader_sources = []
foreach shader: gst_vulkan_shader_sources
  basefn = shader.split('.').get(0)
  suffix = shader.split('.').get(1)

  stage_arg = suffix == 'frag' ? '-fshader-stage=fragment' : '-fshader-stage=vertex'
  basename = '@0@.@1@'.format(basefn, suffix)
  spv_shader = basename + '.spv'
  c_shader_source = basename + '.c'
  c_shader_header = basename + '.h'

  compiled_shader = custom_target(spv_shader,
                                  input: shader,
                                  output: spv_shader,
                                  command: [
                                    glslc,
                                    stage_arg,
                                    '@INPUT@',
                                    '-o', '@OUTPUT@'
                                  ])

  c_shader = custom_target (c_shader_source,
      input: compiled_shader,
      output: [c_shader_source, c_shader_header],
      command: [ bin2array,
        '--array-name=' + basename.underscorify(),
        '--c-include=gst/gst.h',
        '--element-type=gchar',
        '--element-size=1',
        '--linebreak=8',
        '--input', '@INPUT@', 
        '--output', '@OUTPUT0@',
        '--header-output', '@OUTPUT1@'])

  vulkan_compiled_shader_sources += [c_shader]
endforeach
