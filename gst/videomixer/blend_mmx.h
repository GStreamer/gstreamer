#ifdef A32
static inline void
NAME_BLEND (guint8 * dest, const guint8 * src, gint src_height, gint src_width,
    gint src_stride, gint dest_stride, guint s_alpha)
{
  gint i;
  gint src_add = src_stride - (4 * src_width);
  gint dest_add = dest_stride - (4 * src_width);

  for (i = 0; i < src_height; i++) {
    gulong old_ebx;

    /*      (P1 * (256 - A) + (P2 * A)) / 256
     * =>   (P1 * 256 - P1 * A + P2 * A) / 256
     * =>   (P1 * 256 + A * (P2 - P1) / 256
     * =>   P1 + (A * (P2 - P1)) / 256
     */
    /* *INDENT-OFF* */
    __asm__ __volatile__ (
        " movl       %%ebx ,      %6   \n\t"

        " pcmpeqd    %%mm5 ,   %%mm5   \n\t"   /* mm5 = 0xffff... */
#if A_OFF == 0
        " psrld        $24 ,   %%mm5   \n\t"   /* mm5 = 00 00 00 ff 00 00 00 ff, selector for alpha */
#else
        " pslld        $24 ,   %%mm5   \n\t"   /* mm5 = ff 00 00 00 ff 00 00 00, selector for alpha */
#endif
        " mov           %4 ,   %%eax   \n\t"   /* eax = s_alpha */
        " movd       %%eax ,   %%mm6   \n\t"   /* mm6 = s_alpha */
        " punpckldq  %%mm6 ,   %%mm6   \n\t"   /* mm6 = 00 00 00 aa 00 00 00 aa, alpha scale factor */

        " movl          %5 ,   %%ebx   \n\t"   /* ebx = src_width */
        " test          $1 ,   %%ebx   \n\t"   /* check odd pixel */
        " je                      1f   \n\t"

        /* do odd pixel */
        " movd        (%2) ,   %%mm2   \n\t"   /* mm2 = src,  00 00 00 00 sv su sy sa */
        " movd        (%3) ,   %%mm1   \n\t"   /* mm1 = dest, 00 00 00 00 dv du dy da */
        " movq       %%mm2 ,   %%mm0   \n\t"
        " punpcklbw  %%mm7 ,   %%mm2   \n\t"   /* mm2 = 00 sv 00 su 00 sy 00 sa */
        " pand       %%mm5 ,   %%mm0   \n\t"   /* mm0 = 00 00 00 00 00 00 00 sa, get alpha component  */
#if A_OFF != 0
        " psrld        $24 ,   %%mm0   \n\t"
#endif
        " punpcklbw  %%mm7 ,   %%mm1   \n\t"   /* mm1 = 00 dv 00 du 00 dy 00 da */
        " pmullw     %%mm6 ,   %%mm0   \n\t"   /* mult with scale */
        " psubw      %%mm1 ,   %%mm2   \n\t"   /* mm2 = mm2 - mm1 */
        " punpcklwd  %%mm0 ,   %%mm0   \n\t"
        " punpckldq  %%mm0 ,   %%mm0   \n\t"   /* mm0 == 00 aa 00 aa 00 aa 00 aa */
        " psrlw         $8 ,   %%mm0   \n\t"
        " pmullw     %%mm0 ,   %%mm2   \n\t"   /* mm2 == a * mm2 */
        " psllw         $8 ,   %%mm1   \n\t"   /* scale up */
        " paddw      %%mm1 ,   %%mm2   \n\t"   /* mm2 == mm2 + mm1 */
        " psrlw         $8 ,   %%mm2   \n\t"   /* scale down */
        " por        %%mm5 ,   %%mm2   \n\t"   /* set alpha to ff */
        " packuswb   %%mm2 ,   %%mm2   \n\t" 
        " movd       %%mm2 ,    (%3)   \n\t"   /* dest = mm1 */
        " add           $4 ,     %1    \n\t"
        " add           $4 ,     %0    \n\t"

        "1:                            \n\t"
        " sar           $1 ,   %%ebx   \n\t"   /* prepare for 2 pixel per loop */
        " cmp           $0 ,   %%ebx   \n\t"
        " je                      3f   \n\t"
        "2:                            \n\t"

        /* do even pixels */
        " movq        (%2) ,   %%mm2   \n\t"   /* mm2 = src,  sv1 su1 sy1 sa1  sv0 su0 sy0 sa0 */
        " movq        (%3) ,   %%mm1   \n\t"   /* mm1 = dest, dv1 du1 dy1 da1  dv0 du0 dy0 da0 */
        " movq       %%mm2 ,   %%mm4   \n\t"
        " movq       %%mm1 ,   %%mm3   \n\t"
        " movq       %%mm2 ,   %%mm0   \n\t"   /* copy for doing the alpha */

        " pxor       %%mm7 ,   %%mm7   \n\t"  
        " punpcklbw  %%mm7 ,   %%mm2   \n\t"   /* mm2 = 00 sv0  00 su0  00 sy0  00 sa0 */
        " punpckhbw  %%mm7 ,   %%mm4   \n\t"   /* mm4 = 00 sv1  00 su1  00 sy1  00 sa1 */
        " punpcklbw  %%mm7 ,   %%mm1   \n\t"   /* mm1 = 00 dv0  00 du0  00 dy0  00 da0 */
        " punpckhbw  %%mm7 ,   %%mm3   \n\t"   /* mm2 = 00 dv1  00 du1  00 dy1  00 da1 */

        " pand       %%mm5 ,   %%mm0   \n\t"   /* mm0 = 00 00 00 sa1  00 00 00 sa0 */
#if A_OFF != 0
        " psrld        $24 ,   %%mm0   \n\t"
#endif
        " psubw      %%mm1 ,   %%mm2   \n\t"   /* mm2 = mm2 - mm1 */
        " pmullw     %%mm6 ,   %%mm0   \n\t"   /* mult with scale */
        " psubw      %%mm3 ,   %%mm4   \n\t"   /* mm4 = mm4 - mm3 */
        " psrlw         $8 ,   %%mm0   \n\t"   /* scale back */
        " movq       %%mm0 ,   %%mm7   \n\t"   /* save copy */
        " punpcklwd  %%mm0 ,   %%mm0   \n\t"   /* mm0 = 00 00   00 00   00 aa0  00 aa0 */
        " punpckhwd  %%mm7 ,   %%mm7   \n\t"   /* mm7 = 00 00   00 00   00 aa1  00 aa1 */
        " punpckldq  %%mm0 ,   %%mm0   \n\t"   /* mm0 = 00 aa0  00 aa0  00 aa0  00 aa0 */
        " punpckldq  %%mm7 ,   %%mm7   \n\t"   /* mm7 = 00 aa1  00 aa1  00 aa1  00 aa1 */

        " pmullw     %%mm0 ,   %%mm2   \n\t"   /* mm2 == aa * mm2 */
        " pmullw     %%mm7 ,   %%mm4   \n\t"   /* mm2 == aa * mm2 */
        " psllw         $8 ,   %%mm1   \n\t"
        " psllw         $8 ,   %%mm3   \n\t"
        " paddw      %%mm1 ,   %%mm2   \n\t"   /* mm2 == mm2 + mm1 */
        " paddw      %%mm3 ,   %%mm4   \n\t"   /* mm2 == mm2 + mm1 */

        " psrlw         $8 ,   %%mm2   \n\t"
        " psrlw         $8 ,   %%mm4   \n\t"
        " packuswb   %%mm4 ,   %%mm2   \n\t"
        " por        %%mm5 ,   %%mm2   \n\t"   /* set alpha to ff */
        " movq       %%mm2 ,    (%3)   \n\t"

        " add           $8 ,     %1    \n\t"
        " add           $8 ,     %0    \n\t"
        " dec           %%ebx          \n\t"
        " jne                     2b   \n\t"

        "3:                            \n\t"
        " movl          %6 ,   %%ebx   \n\t"
        :"=r" (src), "=r" (dest)
        :"0" (src), "1" (dest), "m" (s_alpha), "m" (src_width), "m" (old_ebx)
        :"%eax", "memory"
#ifdef __MMX__
        , "mm0", "mm1", "mm2", "mm5", "mm6", "mm7"
#endif
    );
      /* *INDENT-ON* */
    src += src_add;
    dest += dest_add;
  }
  __asm__ __volatile__ ("emms");
}

static inline void
NAME_FILL_COLOR (guint8 * dest, gint height, gint width, gint c1, gint c2,
    gint c3)
{
  guint64 val;
  guint nvals = width * height;

  val = (((guint64) 0xff << A_OFF)) | (((guint64) c1) << C1_OFF) |
      (((guint64) c2) << C2_OFF) | (((guint64) c3) << C3_OFF);
  val = (val << 32) | val;

  /* *INDENT-OFF* */
  __asm__ __volatile__ (
    "movq     %4 , %%mm0  \n\t"
    "test     $1 ,    %0  \n\t"
    "je       1f          \n\t"
    "movd  %%mm0 ,  (%1)  \n\t"
    "add      $4 ,    %1  \n\t"
    "dec      %0          \n\t"
    "1:                   \n\t"
    "sar      $1 ,    %0  \n\t"
    "cmp      $0 ,    %0  \n\t"
    "je       3f          \n\t"
    "2:                   \n\t"
    "movq  %%mm0 ,  (%1)  \n\t"
    "add      $8 ,    %1  \n\t"
    "dec      %0          \n\t"
    "jne      2b          \n\t"
    "3:                   \n\t"
    "emms                 \n\t"
    : "=r" (nvals), "=r" (dest)
    : "0" (nvals), "1" (dest), "m" (val)
    : "memory"
#ifdef __MMX__
      , "mm0"
#endif
  );
  /* *INDENT-ON* */
}
#endif

