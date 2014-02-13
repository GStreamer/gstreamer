# sources.frag - Generated list of source files for libvpx (-*- makefile -*-)
#
# INTEL CONFIDENTIAL, FOR INTERNAL USE ONLY
# Copyright (C) 2014 Intel Corporation
#   Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
#
# @BEGIN_LICENSE@
# The source code contained or described herein and all documents
# related to the source code ("Material") are owned by Intel
# Corporation or its suppliers or licensors. Title to the Material
# remains with Intel Corporation or its suppliers and licensors. The
# Material contains trade secrets and proprietary and confidential
# information of Intel or its suppliers and licensors. The Material
# is protected by worldwide copyright and trade secret laws and
# treaty provisions. No part of the Material may be used, copied,
# reproduced, modified, published, uploaded, posted, transmitted,
# distributed, or disclosed in any way without Intel’s prior express
# written permission.
#
# No license under any patent, copyright, trade secret or other
# intellectual property right is granted to or conferred upon you by
# disclosure or delivery of the Materials, either expressly, by
# implication, inducement, estoppel or otherwise. Any license under
# such intellectual property rights must be express and approved by
# Intel in writing.
# @END_LICENSE@

vpx_source_mak = \
	docs.mk \
	examples.mk \
	libs.mk \
	solution.mk \
	test/test.mk \
	vp8/vp8_common.mk \
	vp8/vp8cx.mk \
	vp8/vp8cx_arm.mk \
	vp8/vp8dx.mk \
	vp9/vp9_common.mk \
	vp9/vp9cx.mk \
	vp9/vp9dx.mk \
	vpx/vpx_codec.mk \
	vpx_mem/vpx_mem.mk \
	vpx_ports/vpx_ports.mk \
	vpx_scale/vpx_scale.mk \
	$(NULL)

vpx_source_c = \
	vp8/common/alloccommon.c \
	vp8/common/blockd.c \
	vp8/common/debugmodes.c \
	vp8/common/dequantize.c \
	vp8/common/entropy.c \
	vp8/common/entropymode.c \
	vp8/common/entropymv.c \
	vp8/common/extend.c \
	vp8/common/filter.c \
	vp8/common/findnearmv.c \
	vp8/common/generic/systemdependent.c \
	vp8/common/idct_blk.c \
	vp8/common/idctllm.c \
	vp8/common/loopfilter.c \
	vp8/common/loopfilter_filters.c \
	vp8/common/mbpitch.c \
	vp8/common/mfqe.c \
	vp8/common/modecont.c \
	vp8/common/postproc.c \
	vp8/common/quant_common.c \
	vp8/common/reconinter.c \
	vp8/common/reconintra.c \
	vp8/common/reconintra4x4.c \
	vp8/common/rtcd.c \
	vp8/common/sad_c.c \
	vp8/common/setupintrarecon.c \
	vp8/common/swapyv12buffer.c \
	vp8/common/treecoder.c \
	vp8/common/variance_c.c \
	vp8/common/x86/filter_x86.c \
	vp8/common/x86/idct_blk_mmx.c \
	vp8/common/x86/idct_blk_sse2.c \
	vp8/common/x86/loopfilter_x86.c \
	vp8/common/x86/postproc_x86.c \
	vp8/common/x86/recon_wrapper_sse2.c \
	vp8/common/x86/variance_mmx.c \
	vp8/common/x86/variance_sse2.c \
	vp8/common/x86/variance_ssse3.c \
	vp8/common/x86/vp8_asm_stubs.c \
	vp8/decoder/dboolhuff.c \
	vp8/decoder/decodemv.c \
	vp8/decoder/decodframe.c \
	vp8/decoder/detokenize.c \
	vp8/decoder/onyxd_if.c \
	vp8/decoder/threading.c \
	vp8/vp8_dx_iface.c \
	vp9/common/generic/vp9_systemdependent.c \
	vp9/common/vp9_alloccommon.c \
	vp9/common/vp9_common_data.c \
	vp9/common/vp9_convolve.c \
	vp9/common/vp9_debugmodes.c \
	vp9/common/vp9_entropy.c \
	vp9/common/vp9_entropymode.c \
	vp9/common/vp9_entropymv.c \
	vp9/common/vp9_extend.c \
	vp9/common/vp9_filter.c \
	vp9/common/vp9_findnearmv.c \
	vp9/common/vp9_idct.c \
	vp9/common/vp9_loopfilter.c \
	vp9/common/vp9_loopfilter_filters.c \
	vp9/common/vp9_mvref_common.c \
	vp9/common/vp9_pred_common.c \
	vp9/common/vp9_quant_common.c \
	vp9/common/vp9_reconinter.c \
	vp9/common/vp9_reconintra.c \
	vp9/common/vp9_rtcd.c \
	vp9/common/vp9_scale.c \
	vp9/common/vp9_scan.c \
	vp9/common/vp9_seg_common.c \
	vp9/common/vp9_tile_common.c \
	vp9/common/vp9_treecoder.c \
	vp9/common/x86/vp9_asm_stubs.c \
	vp9/common/x86/vp9_idct_intrin_sse2.c \
	vp9/common/x86/vp9_loopfilter_intrin_sse2.c \
	vp9/decoder/vp9_dboolhuff.c \
	vp9/decoder/vp9_decodemv.c \
	vp9/decoder/vp9_decodframe.c \
	vp9/decoder/vp9_detokenize.c \
	vp9/decoder/vp9_dsubexp.c \
	vp9/decoder/vp9_onyxd_if.c \
	vp9/decoder/vp9_thread.c \
	vp9/vp9_dx_iface.c \
	vpx/src/vpx_codec.c \
	vpx/src/vpx_decoder.c \
	vpx/src/vpx_encoder.c \
	vpx/src/vpx_image.c \
	vpx_mem/vpx_mem.c \
	vpx_ports/x86_cpuid.c \
	vpx_scale/generic/gen_scalers.c \
	vpx_scale/generic/vpx_scale.c \
	vpx_scale/generic/yv12config.c \
	vpx_scale/generic/yv12extend.c \
	vpx_scale/vpx_scale_asm_offsets.c \
	vpx_scale/vpx_scale_rtcd.c \
	$(NULL)

vpx_source_h = \
	vp8/common/alloccommon.h \
	vp8/common/blockd.h \
	vp8/common/coefupdateprobs.h \
	vp8/common/common.h \
	vp8/common/default_coef_probs.h \
	vp8/common/entropy.h \
	vp8/common/entropymode.h \
	vp8/common/entropymv.h \
	vp8/common/extend.h \
	vp8/common/filter.h \
	vp8/common/findnearmv.h \
	vp8/common/header.h \
	vp8/common/invtrans.h \
	vp8/common/loopfilter.h \
	vp8/common/modecont.h \
	vp8/common/mv.h \
	vp8/common/onyxc_int.h \
	vp8/common/onyxd.h \
	vp8/common/postproc.h \
	vp8/common/ppflags.h \
	vp8/common/pragmas.h \
	vp8/common/quant_common.h \
	vp8/common/reconinter.h \
	vp8/common/reconintra4x4.h \
	vp8/common/setupintrarecon.h \
	vp8/common/swapyv12buffer.h \
	vp8/common/systemdependent.h \
	vp8/common/threading.h \
	vp8/common/treecoder.h \
	vp8/common/variance.h \
	vp8/common/vp8_entropymodedata.h \
	vp8/common/x86/filter_x86.h \
	vp8/decoder/dboolhuff.h \
	vp8/decoder/decodemv.h \
	vp8/decoder/decoderthreading.h \
	vp8/decoder/detokenize.h \
	vp8/decoder/onyxd_int.h \
	vp8/decoder/treereader.h \
	vp9/common/vp9_alloccommon.h \
	vp9/common/vp9_blockd.h \
	vp9/common/vp9_common.h \
	vp9/common/vp9_common_data.h \
	vp9/common/vp9_convolve.h \
	vp9/common/vp9_default_coef_probs.h \
	vp9/common/vp9_entropy.h \
	vp9/common/vp9_entropymode.h \
	vp9/common/vp9_entropymv.h \
	vp9/common/vp9_enums.h \
	vp9/common/vp9_extend.h \
	vp9/common/vp9_filter.h \
	vp9/common/vp9_findnearmv.h \
	vp9/common/vp9_idct.h \
	vp9/common/vp9_loopfilter.h \
	vp9/common/vp9_mv.h \
	vp9/common/vp9_mvref_common.h \
	vp9/common/vp9_onyxc_int.h \
	vp9/common/vp9_ppflags.h \
	vp9/common/vp9_pred_common.h \
	vp9/common/vp9_quant_common.h \
	vp9/common/vp9_reconinter.h \
	vp9/common/vp9_reconintra.h \
	vp9/common/vp9_scale.h \
	vp9/common/vp9_scan.h \
	vp9/common/vp9_seg_common.h \
	vp9/common/vp9_systemdependent.h \
	vp9/common/vp9_tile_common.h \
	vp9/common/vp9_treecoder.h \
	vp9/decoder/vp9_dboolhuff.h \
	vp9/decoder/vp9_decodemv.h \
	vp9/decoder/vp9_decodframe.h \
	vp9/decoder/vp9_detokenize.h \
	vp9/decoder/vp9_dsubexp.h \
	vp9/decoder/vp9_onyxd.h \
	vp9/decoder/vp9_onyxd_int.h \
	vp9/decoder/vp9_read_bit_buffer.h \
	vp9/decoder/vp9_thread.h \
	vp9/decoder/vp9_thread.h \
	vp9/decoder/vp9_treereader.h \
	vp9/vp9_iface_common.h \
	vpx/internal/vpx_codec_internal.h \
	vpx/vp8.h \
	vpx/vp8dx.h \
	vpx/vpx_codec.h \
	vpx/vpx_decoder.h \
	vpx/vpx_encoder.h \
	vpx/vpx_image.h \
	vpx/vpx_integer.h \
	vpx_mem/include/vpx_mem_intrnl.h \
	vpx_mem/vpx_mem.h \
	vpx_ports/asm_offsets.h \
	vpx_ports/emmintrin_compat.h \
	vpx_ports/mem.h \
	vpx_ports/vpx_once.h \
	vpx_ports/vpx_timer.h \
	vpx_ports/x86.h \
	vpx_scale/vpx_scale.h \
	vpx_scale/yv12config.h \
	$(NULL)

vpx_source_asm = \
	third_party/x86inc/x86inc.asm \
	vp8/common/x86/dequantize_mmx.asm \
	vp8/common/x86/idctllm_mmx.asm \
	vp8/common/x86/idctllm_sse2.asm \
	vp8/common/x86/iwalsh_mmx.asm \
	vp8/common/x86/iwalsh_sse2.asm \
	vp8/common/x86/loopfilter_block_sse2.asm \
	vp8/common/x86/loopfilter_mmx.asm \
	vp8/common/x86/loopfilter_sse2.asm \
	vp8/common/x86/mfqe_sse2.asm \
	vp8/common/x86/postproc_mmx.asm \
	vp8/common/x86/postproc_sse2.asm \
	vp8/common/x86/recon_mmx.asm \
	vp8/common/x86/recon_sse2.asm \
	vp8/common/x86/sad_mmx.asm \
	vp8/common/x86/sad_sse2.asm \
	vp8/common/x86/sad_sse3.asm \
	vp8/common/x86/sad_sse4.asm \
	vp8/common/x86/sad_ssse3.asm \
	vp8/common/x86/subpixel_mmx.asm \
	vp8/common/x86/subpixel_sse2.asm \
	vp8/common/x86/subpixel_ssse3.asm \
	vp8/common/x86/variance_impl_mmx.asm \
	vp8/common/x86/variance_impl_sse2.asm \
	vp8/common/x86/variance_impl_ssse3.asm \
	vp9/common/x86/vp9_copy_sse2.asm \
	vp9/common/x86/vp9_intrapred_sse2.asm \
	vp9/common/x86/vp9_intrapred_ssse3.asm \
	vp9/common/x86/vp9_loopfilter_mmx.asm \
	vp9/common/x86/vp9_subpixel_8t_sse2.asm \
	vp9/common/x86/vp9_subpixel_8t_ssse3.asm \
	vpx_ports/emms.asm \
	vpx_ports/x86_abi_support.asm \
	$(NULL)
