# Shaders
gst_vulkan_shader_sources = [
  'identity.frag',
  'identity.vert',
  'swizzle.frag',
  'swizzle_and_clobber_alpha.frag',
  'yuy2_to_rgb.frag',
  'rgb_to_yuy2.frag',
  'ayuv_to_rgb.frag',
  'rgb_to_ayuv.frag',
  'nv12_to_rgb.frag',
  'rgb_to_nv12.frag',
  'view_convert.frag',
]

bin2array = find_program('bin2array.py')

# FIXME: meson compiler class instead?
if glslc.found()
  glslc_build_options = []
  optimization = get_option('optimization')
  if get_option('debug')
    glslc_build_options += ['-g']
  endif
  if get_option('werror')
    glslc_build_options += ['-Werror']
  endif
  if optimization == 's'
    glslc_build_options += ['-Os']
  endif
  if optimization in ['1', '2', '3']
    glslc_build_options += ['-O']
  endif
elif glslang.found()
  glslang_build_options = []
  optimization = get_option('optimization')
  if get_option('debug')
    glslang_build_options += ['-g']
  endif
  if optimization == 's'
    glslang_build_options += ['-Os']
  endif
  if optimization == '0'
    glslang_build_options += ['-Od']
  endif
else
  error('No glslc or glslang!')
  subdir_done()
endif

vulkan_compiled_shader_sources = []
foreach shader: gst_vulkan_shader_sources
  basefn = shader.split('.').get(0)
  suffix = shader.split('.').get(1)

  basename = '@0@.@1@'.format(basefn, suffix)
  spv_shader = basename + '.spv'
  c_shader_source = basename + '.c'
  c_shader_header = basename + '.h'

  if glslc.found()
    stage_arg = suffix == 'frag' ? '-fshader-stage=fragment' : '-fshader-stage=vertex'
    compiled_shader = custom_target(spv_shader,
        input: shader,
        output: spv_shader,
        depfile: '@PLAINNAME@.d',
        command: [glslc] + glslc_build_options + [stage_arg,
          '--target-env=vulkan1.0',
          '-MD', '-MF', '@DEPFILE@',
          '@INPUT@',
          '-o', '@OUTPUT@'])
  elif glslang.found()
    compiled_shader = custom_target(spv_shader,
        input: shader,
        output: spv_shader,
        depfile: '@PLAINNAME@.d',
        command: [glslang] + glslang_build_options + [
          '-V100',
          '-I' + meson.current_source_dir(),
          '--target-env', 'vulkan1.0',
          '--depfile', '@DEPFILE@',
          '--quiet',
          '@INPUT@',
          '-o', '@OUTPUT@'])
  endif

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
