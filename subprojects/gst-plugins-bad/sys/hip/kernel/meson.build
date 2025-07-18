conv_source = files('converter.cu')
conv_comm_source = files('converter-unpack.cu')

conv_input_formats = [
  'I420',
  'YV12',
  'I420_10',
  'I420_12',
  'NV12',
  'NV21',
  'VUYA',
  'RGBA',
  'BGRA',
  'RGBx',
  'BGRx',
  'ARGB',
  'ABGR',
  'RGBP',
  'BGRP',
  'GBR',
  'GBR_10',
  'GBR_12',
  'GBRA',
]

conv_output_formats = [
  'I420',
  'YV12',
  'NV12',
  'NV21',
  'P010',
  'I420_10',
  'I420_12',
  'Y444',
  'Y444_10',
  'Y444_12',
  'Y444_16',
  'Y42B',
  'I422_10',
  'I422_12',
  'VUYA',
  'RGBA',
  'RGBx',
  'BGRA',
  'BGRx',
  'ARGB',
  'ABGR',
  'RGB',
  'BGR',
  'RGB10A2',
  'BGR10A2',
  'RGBP',
  'GBR',
  'GBR_10',
  'GBR_12',
  'GBR_16',
  'GBRA',
]

if have_hipcc
  amd_header_collector = find_program('collect_hsaco_headers.py')
  amd_conv_precompiled = []
  amd_opt_common = ['-w', '--genco', '-c', '@INPUT@', '-o', '@OUTPUT@']
  amd_arch_opt = get_option('hip-hipcc-arch')
  if amd_arch_opt != ''
    amd_opt_common += ['--offload-arch=' + amd_arch_opt]
  endif

  foreach input_format : conv_input_formats
    foreach output_format : conv_output_formats
      hsaco_name = 'GstHipConverterMain_@0@_@1@_amd.hsaco'.format(input_format, output_format)
      opts = amd_opt_common + ['-DSAMPLER=Sample@0@'.format(input_format),
              '-DOUTPUT=Output@0@'.format(output_format)]
      compiled_kernel = custom_target(hsaco_name,
          input : conv_source,
          output : hsaco_name,
          command : [hipcc] + opts)
      amd_conv_precompiled += [compiled_kernel]
    endforeach
  endforeach

  hsaco_name = 'GstHipConverterUnpack_amd.hsaco'
  compiled_kernel = custom_target(hsaco_name,
      input : conv_comm_source,
      output : hsaco_name,
      command : [hipcc] + amd_opt_common)
  amd_conv_precompiled += [compiled_kernel]

  amd_conv_hsaco_collection = custom_target('hip_converter_hsaco',
      input : amd_conv_precompiled,
      output : 'converter_hsaco.h',
      command : [amd_header_collector,
          '--input', meson.current_build_dir(),
          '--prefix', 'GstHipConverter',
          '--name', 'g_precompiled_hsaco_table',
          '--output', '@OUTPUT@'
      ])

  hip_amd_precompiled += [
    amd_conv_precompiled,
    amd_conv_hsaco_collection,
  ]
endif

if have_nvcc
  nvidia_header_collector = find_program('collect_ptx_headers.py')
  nvidia_conv_precompiled = []
  nvidia_opt_common = ['-ptx', '-w', '-o', '@OUTPUT@']
  nvidia_arch_opt = get_option('hip-nvcc-arch')
  if nvidia_arch_opt != ''
    nvidia_opt_common += ['-arch=' + nvidia_arch_opt]
  endif

  foreach input_format : conv_input_formats
    foreach output_format : conv_output_formats
      ptx_name = 'GstHipConverterMain_@0@_@1@_nvidia.ptx'.format(input_format, output_format)
      opts = nvidia_opt_common + ['-DSAMPLER=Sample@0@'.format(input_format),
              '-DOUTPUT=Output@0@'.format(output_format), '@INPUT@']
      compiled_kernel = custom_target(ptx_name,
          input : conv_source,
          output : ptx_name,
          command : [nvcc] + opts)
      nvidia_conv_precompiled += [compiled_kernel]
    endforeach
  endforeach

  ptx_name = 'GstHipConverterUnpack_nvidia.ptx'
  compiled_kernel = custom_target(ptx_name,
      input : conv_comm_source,
      output : ptx_name,
      command : [nvcc] + nvidia_opt_common + ['@INPUT@'])
  nvidia_conv_precompiled += [compiled_kernel]

  nvidia_conv_ptx_collection = custom_target('hip_converter_ptx',
      input : nvidia_conv_precompiled,
      output : 'converter_ptx.h',
      command : [nvidia_header_collector,
          '--input', meson.current_build_dir(),
          '--prefix', 'GstHipConverter',
          '--name', 'g_precompiled_ptx_table',
          '--output', '@OUTPUT@'
      ])

  hip_nvidia_precompiled += [
    nvidia_conv_precompiled,
    nvidia_conv_ptx_collection,
  ]
endif