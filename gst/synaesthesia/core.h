
#ifndef HGUARD_SYNAESTHESIA_CORE_H
#define HGUARD_SYNAESTHESIA_CORE_H

#include <glib.h>

#define SYNA_BITS 8
#define SYNA_SIZE (1 << SYNA_BITS)

struct syna_priv {
  gdouble cosTable[SYNA_SIZE],negSinTable[SYNA_SIZE];
  gint bitReverse[SYNA_SIZE];
  gint scaleDown[256];
  gint maxStarRadius;
  gint outWidth,outHeight;
  gint fadeMode;
  gint brightnessTwiddler;
  gint starSize;
  gint pointsAreDiamonds;

  gdouble fgRedSlider, fgGreenSlider, bgRedSlider, bgGreenSlider;

  guchar *output,*lastOutput,*lastLastOutput;
};

#define FADE_WAVE 1
#define FADE_HEAT 2
#define FADE_STARS 3
#define FADE_FLAME 4

void setStarSize(struct syna_priv *sp, gdouble size);
void coreInit(struct syna_priv *sp, int w, int h);
int coreGo(struct syna_priv *sp, guchar *data, gint len);
void fade(struct syna_priv *sp);
void setupPalette(struct syna_priv *sp, guchar *palette);


#endif /* HGUARD_SYNAESTHESIA_CORE_H */
