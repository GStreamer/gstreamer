
/* This file is an attempt to hack compatibility between the
 * FLAC API version used in the code in this directory
 * (currently 1.0.3) and older versions of FLAC, particularly
 * 1.0.2.
 */

#ifndef _FLAC_COMPAT_H_
#define _FLAC_COMPAT_H_

#ifndef VERSION
#define VERSION bogus
#endif
#include <FLAC/all.h>

/* FIXME when there's a autoconf symbol */
#ifndef FLAC_VERSION

#ifndef FLAC__VERSION_STRING	/* removed in 1.0.4 */
#define FLAC_VERSION 0x010004
#else
#ifdef FLAC__REFERENCE_CODEC_MAX_BITS_PER_SAMPLE
#define FLAC_VERSION 0x010003
#else
#define FLAC_VERSION 0x010002
#endif
#endif

#endif /* !defined(FLAC_VERSION) */


#if FLAC_VERSION < 0x010004
#define FLAC__STREAM_ENCODER_OK FLAC__STREAM_ENCODER_WRITE_OK
#define FLAC__seekable_stream_decoder_process_single(a) \
	FLAC__seekable_stream_decoder_process_one_frame(a)
#endif /* FLAC_VERSION < 0x010004 */

#if FLAC_VERSION < 0x010003

#define FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC \
	FLAC__STREAM_DECODER_ERROR_LOST_SYNC
#define FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER \
	FLAC__STREAM_DECODER_ERROR_BAD_HEADER
#define FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH \
	FLAC__STREAM_DECODER_ERROR_FRAME_CRC_MISMATCH
#define FLAC__STREAM_DECODER_WRITE_STATUS_ABORT \
	FLAC__STREAM_DECODER_WRITE_ABORT
#define FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE \
	FLAC__STREAM_DECODER_WRITE_CONTINUE

#define FLAC__StreamMetadata FLAC__StreamMetaData

#endif /* FLAC_VERSION < 0x010003 */

#endif /* _FLAC_COMPAT_H_ */
