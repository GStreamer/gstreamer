conv_source = files('gstcudaconverter.cu')
conv_comm_source = files('gstcudaconverter-unpack.cu')

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

header_collector = find_program('collect_ptx_headers.py')

conv_precompiled = []
opt_common = ['-ptx', '-w', '-o', '@OUTPUT@']
arch_opt = get_option('nvcodec-nvcc-arch')
if arch_opt != ''
  opt_common += ['-arch=' + arch_opt]
endif

foreach input_format : conv_input_formats
  foreach output_format : conv_output_formats
    ptx_name = 'GstCudaConverterMain_@0@_@1@.ptx'.format(input_format, output_format)
    opts = opt_common + ['-DSAMPLER=Sample@0@'.format(input_format),
            '-DOUTPUT=Output@0@'.format(output_format), '@INPUT@']
    compiled_kernel = custom_target(ptx_name,
        input : conv_source,
        output : ptx_name,
        command : [nvcc] + opts)
    conv_precompiled += [compiled_kernel]
  endforeach
endforeach

ptx_name = 'GstCudaConverterUnpack.ptx'
compiled_kernel = custom_target(ptx_name,
    input : conv_comm_source,
    output : ptx_name,
    command : [nvcc] + opt_common + ['@INPUT@'])
conv_precompiled += [compiled_kernel]

conv_ptx_collection = custom_target('converter_ptx',
    input : conv_precompiled,
    output : 'converter_ptx.h',
    command : [header_collector,
        '--input', meson.current_build_dir(),
        '--prefix', 'GstCudaConverter',
        '--name', 'g_precompiled_ptx_table',
        '--output', '@OUTPUT@'
    ])

nvcodec_kernel_precompiled += [
  conv_precompiled,
  conv_ptx_collection,
]
