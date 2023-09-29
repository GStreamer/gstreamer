kernel_image_path = get_option('virtme_kernel_image')
if kernel_image_path != ''
  if not fs.is_absolute(kernel_image_path)
    error('Kernel image path @0@ must be absolute'.format(kernel_image_path))
  endif
  if not fs.is_file(kernel_image_path)
    error('Kernel image @0@ does not exist'.format(kernel_image_path))
  endif

  virtme_run = find_program('virtme-run.sh')

  supported_decoders = {
    'vp8': [],
    'vp9': [],
    'h.264': [
      'FM1_BT_B',
      'FM2_SVA_C'
    ],
    'h.265': [
      'PICSIZE_A_Bossen_1',
      'PICSIZE_B_Bossen_1',
      'PICSIZE_C_Bossen_1',
      'PICSIZE_D_Bossen_1',
      'EXTPREC_HIGHTHROUGHPUT_444_16_INTRA_16BIT_RExt_Sony_1',
      'EXTPREC_MAIN_444_16_INTRA_16BIT_RExt_Sony_1',
      'GENERAL_12b_400_RExt_Sony_1',
      'GENERAL_16b_400_RExt_Sony_1',
      'GENERAL_16b_444_highThroughput_RExt_Sony_2',
      'GENERAL_16b_444_RExt_Sony_2',
      'GENERAL_8b_400_RExt_Sony_1',
      'WAVETILES_RExt_Sony_2'
    ],
  }

  foreach decoder, skipped : supported_decoders
    test('v4l2-stateless-decoders-' + decoder, virtme_run,
      workdir: meson.current_source_dir(),
      suite: 'v4l2-stateless-decoders',
      args: [
        kernel_image_path,
        meson.project_build_root(),
        meson.source_root(),
        decoder,
        skipped,
      ],
      is_parallel: true,
      timeout: 180,
    )
  endforeach
else
  warning('No kernel image path specified, skipping virtme tests')
endif
