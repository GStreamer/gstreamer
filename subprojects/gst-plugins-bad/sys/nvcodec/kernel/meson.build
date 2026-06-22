jpegenc_source = files('gstnvjpegenc.cu')

header_collector = find_program('collect_ptx_headers.py')

jpegenc_precompiled = []
opt_common = ['-ptx', '-w', '-o', '@OUTPUT@']
arch_opt = get_option('nvcodec-nvcc-arch')
if arch_opt != ''
  opt_common += ['-arch=' + arch_opt]
endif

ptx_name = 'GstJpegEnc.ptx'
compiled_kernel = custom_target(ptx_name,
    input : jpegenc_source,
    output : ptx_name,
    command : [nvcc] + opt_common + ['@INPUT@'])
jpegenc_precompiled += [compiled_kernel]

jpegenc_ptx_collection = custom_target('jpegenc_ptx',
    input : jpegenc_precompiled,
    output : 'jpegenc_ptx.h',
    command : [header_collector,
        '--input', meson.current_build_dir(),
        '--prefix', 'GstJpegEnc',
        '--name', 'g_precompiled_ptx_table',
        '--output', '@OUTPUT@'
    ])

nvcodec_kernel_precompiled += [
  jpegenc_precompiled,
  jpegenc_ptx_collection,
]
