/* define DCT types */

/*
 * DCTSIZE      underlying (1d) transform size
 * DCTSIZE2     DCTSIZE squared
 */

#define DCTSIZE      (8)
#define DCTSIZE2     (DCTSIZE*DCTSIZE)

#define EIGHT_BIT_SAMPLES	/* needed in jrevdct.c */

typedef short DCTELEM;		/* must be at least 16 bits */

typedef DCTELEM DCTBLOCK[DCTSIZE2];

typedef long INT32;		/* must be at least 32 bits */

extern void gst_idct_int_idct();

extern void gst_idct_init_fast_int_idct (void);
extern void gst_idct_fast_int_idct (short *block);

#ifdef HAVE_LIBMMX
extern void gst_idct_mmx_idct (short *block);
extern void gst_idct_mmx32_idct (short *block);
extern void gst_idct_sse_idct (short *block);
#endif /* HAVE_LIBMMX */

extern void gst_idct_init_float_idct(void);
extern void gst_idct_float_idct (short *block);

