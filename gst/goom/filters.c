/* filter.c version 0.7
 * contient les filtres applicable a un buffer
 * creation : 01/10/2000
 *  -ajout de sinFilter()
 *  -ajout de zoomFilter()
 *  -copie de zoomFilter() en zoomFilterRGB(), gérant les 3 couleurs
 *  -optimisation de sinFilter (utilisant une table de sin)
 *	-asm
 *	-optimisation de la procedure de génération du buffer de transformation
 *		la vitesse est maintenant comprise dans [0..128] au lieu de [0..100]
*/

/*#define _DEBUG_PIXEL; */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "filters.h"
#include "graphic.h"
#include "goom_tools.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#ifdef MMX
#define USE_ASM
#endif
#ifdef POWERPC
#define USE_ASM
#endif

#ifdef USE_ASM
#define EFFECT_DISTORS 4
#else
#define EFFECT_DISTORS 10
#endif


extern volatile guint32 resolx;
extern volatile guint32 resoly;

#ifdef USE_ASM

#ifdef MMX
int mmx_zoom ();
guint32 mmx_zoom_size;
#endif /* MMX */

#ifdef POWERPC
extern unsigned int useAltivec;
extern void ppc_zoom (void);
extern void ppc_zoom_altivec (void);
unsigned int ppcsize4;
#endif /* PowerPC */

unsigned int *coeffs = 0, *freecoeffs = 0;
guint32 *expix1 = 0;		/* pointeur exporte vers p1 */
guint32 *expix2 = 0;		/* pointeur exporte vers p2 */
guint32 zoom_width;

#endif /* ASM */


static int sintable[0xffff];
static int vitesse = 127;
static char theMode = AMULETTE_MODE;
static int vPlaneEffect = 0;
static int hPlaneEffect = 0;
static char noisify = 2;
static int middleX, middleY;
static unsigned char sqrtperte = 16;

static int *firedec = 0;


/* retourne x>>s , en testant le signe de x */
inline int
ShiftRight (int x, const unsigned char s)
{
  if (x < 0)
    return -(-x >> s);
  else
    return x >> s;
}

/*
  calculer px et py en fonction de x,y,middleX,middleY et theMode
  px et py indique la nouvelle position (en sqrtperte ieme de pixel)
  (valeur * 16)
*/
void
calculatePXandPY (int x, int y, int *px, int *py)
{
  if (theMode == WATER_MODE) {
    static int wave = 0;
    static int wavesp = 0;
    int yy;

    yy = y + RAND () % 4 + wave / 10;
    yy -= RAND () % 4;
    if (yy < 0)
      yy = 0;
    if (yy >= resoly)
      yy = resoly - 1;

    *px = (x << 4) + firedec[yy] + (wave / 10);
    *py = (y << 4) + 132 - ((vitesse < 132) ? vitesse : 131);

    wavesp += RAND () % 3;
    wavesp -= RAND () % 3;
    if (wave < -10)
      wavesp += 2;
    if (wave > 10)
      wavesp -= 2;
    wave += (wavesp / 10) + RAND () % 3;
    wave -= RAND () % 3;
    if (wavesp > 100)
      wavesp = (wavesp * 9) / 10;
  } else {
    int dist;
    register int vx, vy;
    int fvitesse = vitesse << 4;

    if (noisify) {
      x += RAND () % noisify;
      x -= RAND () % noisify;
      y += RAND () % noisify;
      y -= RAND () % noisify;
    }

    if (hPlaneEffect)
      vx = ((x - middleX) << 9) + hPlaneEffect * (y - middleY);
    else
      vx = (x - middleX) << 9;

    if (vPlaneEffect)
      vy = ((y - middleY) << 9) + vPlaneEffect * (x - middleX);
    else
      vy = (y - middleY) << 9;

    switch (theMode) {
      case WAVE_MODE:
	dist =
	    ShiftRight (vx, 9) * ShiftRight (vx, 9) + ShiftRight (vy,
	    9) * ShiftRight (vy, 9);
	fvitesse *=
	    1024 +
	    ShiftRight (sintable[(unsigned short) (0xffff * dist *
		    EFFECT_DISTORS)], 6);
	fvitesse /= 1024;
	break;
      case CRYSTAL_BALL_MODE:
	dist =
	    ShiftRight (vx, 9) * ShiftRight (vx, 9) + ShiftRight (vy,
	    9) * ShiftRight (vy, 9);
	fvitesse += (dist * EFFECT_DISTORS >> 10);
	break;
      case AMULETTE_MODE:
	dist =
	    ShiftRight (vx, 9) * ShiftRight (vx, 9) + ShiftRight (vy,
	    9) * ShiftRight (vy, 9);
	fvitesse -= (dist * EFFECT_DISTORS >> 4);
	break;
      case SCRUNCH_MODE:
	dist =
	    ShiftRight (vx, 9) * ShiftRight (vx, 9) + ShiftRight (vy,
	    9) * ShiftRight (vy, 9);
	fvitesse -= (dist * EFFECT_DISTORS >> 9);
	break;
    }
    if (vx < 0)
      *px = (middleX << 4) - (-(vx * fvitesse) >> 16);
    else
      *px = (middleX << 4) + ((vx * fvitesse) >> 16);
    if (vy < 0)
      *py = (middleY << 4) - (-(vy * fvitesse) >> 16);
    else
      *py = (middleY << 4) + ((vy * fvitesse) >> 16);
  }
}

/*#define _DEBUG */

inline void
setPixelRGB (Uint * buffer, Uint x, Uint y, Color c)
{
/*		buffer[ y*WIDTH + x ] = (c.r<<16)|(c.v<<8)|c.b */
#ifdef _DEBUG_PIXEL
  if (x + y * resolx >= resolx * resoly) {
    fprintf (stderr, "setPixel ERROR : hors du tableau... %i, %i\n", x, y);
    /*exit (1) ; */
  }
#endif

#ifdef USE_DGA
  buffer[y * resolx + x] = (c.b << 16) | (c.v << 8) | c.r;
#else
  buffer[y * resolx + x] = (c.r << 16) | (c.v << 8) | c.b;
#endif
}


inline void
setPixelRGB_ (Uint * buffer, Uint x, Color c)
{
#ifdef _DEBUG
  if (x >= resolx * resoly) {
    printf ("setPixel ERROR : hors du tableau... %i, %i\n", x, y);
    exit (1);
  }
#endif

#ifdef USE_DGA
  buffer[x] = (c.b << 16) | (c.v << 8) | c.r;
#else
  buffer[x] = (c.r << 16) | (c.v << 8) | c.b;
#endif
}



inline void
getPixelRGB (Uint * buffer, Uint x, Uint y, Color * c)
{
  register unsigned char *tmp8;

#ifdef _DEBUG
  if (x + y * resolx >= resolx * resoly) {
    printf ("getPixel ERROR : hors du tableau... %i, %i\n", x, y);
    exit (1);
  }
#endif

#ifdef __BIG_ENDIAN__
  c->b = *(unsigned char *) (tmp8 =
      (unsigned char *) (buffer + (x + y * resolx)));
  c->r = *(unsigned char *) (++tmp8);
  c->v = *(unsigned char *) (++tmp8);
  c->b = *(unsigned char *) (++tmp8);

#else
  /* ATTENTION AU PETIT INDIEN  */
  c->b = *(unsigned char *) (tmp8 =
      (unsigned char *) (buffer + (x + y * resolx)));
  c->v = *(unsigned char *) (++tmp8);
  c->r = *(unsigned char *) (++tmp8);
/*	*c = (Color) buffer[x+y*WIDTH] ; */
#endif
}


inline void
getPixelRGB_ (Uint * buffer, Uint x, Color * c)
{
  register unsigned char *tmp8;

#ifdef _DEBUG
  if (x >= resolx * resoly) {
    printf ("getPixel ERROR : hors du tableau... %i\n", x);
    exit (1);
  }
#endif

#ifdef __BIG_ENDIAN__
  c->b = *(unsigned char *) (tmp8 = (unsigned char *) (buffer + x));
  c->r = *(unsigned char *) (++tmp8);
  c->v = *(unsigned char *) (++tmp8);
  c->b = *(unsigned char *) (++tmp8);

#else
  /* ATTENTION AU PETIT INDIEN  */
  c->b = *(unsigned char *) (tmp8 = (unsigned char *) (buffer + x));
  c->v = *(unsigned char *) (++tmp8);
  c->r = *(unsigned char *) (++tmp8);
/*	*c = (Color) buffer[x+y*WIDTH] ; */
#endif
}


/*===============================================================*/
void
zoomFilterFastRGB (Uint * pix1,
    Uint * pix2, ZoomFilterData * zf, Uint resx, Uint resy)
{
  static guint32 prevX = 0, prevY = 0;

  static char reverse = 0;	/*vitesse inversé..(zoom out) */

  /*    static int perte = 100; // 100 = normal */
  static unsigned char pertedec = 8;
  static char firstTime = 1;

  Uint x, y;

/*  static unsigned int prevX = 0, prevY = 0; */

#ifdef USE_ASM
  expix1 = pix1;
  expix2 = pix2;
#else
  Color couleur;
  Color col1, col2, col3, col4;
  Uint position;

  static unsigned int *pos10 = 0;
  static unsigned int *c1 = 0, *c2 = 0, *c3 = 0, *c4 = 0;
#endif

  if ((prevX != resx) || (prevY != resy)) {
    prevX = resx;
    prevY = resy;
#ifndef USE_ASM
    if (c1)
      free (c1);
    if (c2)
      free (c2);
    if (c3)
      free (c3);
    if (c4)
      free (c4);
    if (pos10)
      free (pos10);
    c1 = c2 = c3 = c4 = pos10 = 0;
#else
    if (coeffs)
      free (freecoeffs);
    coeffs = 0;
#endif
    middleX = resx / 2;
    middleY = resy - 1;
    firstTime = 1;
    if (firedec)
      free (firedec);
    firedec = 0;
  }

  if (zf) {
    reverse = zf->reverse;
    vitesse = zf->vitesse;
    if (reverse)
      vitesse = 256 - vitesse;
#ifndef USE_ASM
    sqrtperte = zf->sqrtperte;
#endif
    pertedec = zf->pertedec;
    middleX = zf->middleX;
    middleY = zf->middleY;
    theMode = zf->mode;
    hPlaneEffect = zf->hPlaneEffect;
    vPlaneEffect = zf->vPlaneEffect;
    noisify = zf->noisify;
  }

  if (firstTime || zf) {

    /* generation d'une table de sinus */
    if (firstTime) {
      unsigned short us;

      firstTime = 0;
#ifdef USE_ASM
      freecoeffs = (unsigned int *)
	  malloc (resx * resy * 2 * sizeof (unsigned int) + 128);
      coeffs = (guint32 *) ((1 + ((unsigned int) (freecoeffs)) / 128) * 128);

#else
      pos10 = (unsigned int *) malloc (resx * resy * sizeof (unsigned int));
      c1 = (unsigned int *) malloc (resx * resy * sizeof (unsigned int));
      c2 = (unsigned int *) malloc (resx * resy * sizeof (unsigned int));
      c3 = (unsigned int *) malloc (resx * resy * sizeof (unsigned int));
      c4 = (unsigned int *) malloc (resx * resy * sizeof (unsigned int));
#endif
      for (us = 0; us < 0xffff; us++) {
	sintable[us] = (int) (1024.0f * sin (us * 2 * 3.31415f / 0xffff));
      }

      {
	int loopv;
	firedec = (int *) malloc (prevY * sizeof (int));
	for (loopv = prevY; loopv != 0;) {
	  static int decc = 0;
	  static int spdc = 0;
	  static int accel = 0;

	  loopv--;
	  firedec[loopv] = decc;
	  decc += spdc / 10;
	  spdc += RAND () % 3;
	  spdc -= RAND () % 3;

	  if (decc > 4)
	    spdc -= 1;
	  if (decc < -4)
	    spdc += 1;

	  if (spdc > 30)
	    spdc = spdc - RAND () % 3 + accel / 10;
	  if (spdc < -30)
	    spdc = spdc + RAND () % 3 + accel / 10;

	  if (decc > 8 && spdc > 1)
	    spdc -= RAND () % 3 - 2;

	  if (decc < -8 && spdc < -1)
	    spdc += RAND () % 3 + 2;

	  if (decc > 8 || decc < -8)
	    decc = decc * 8 / 9;

	  accel += RAND () % 2;
	  accel -= RAND () % 2;
	  if (accel > 20)
	    accel -= 2;
	  if (accel < -20)
	    accel += 2;
	}
      }
    }


    /* generation du buffer */
    for (y = 0; y < prevY; y++)
      for (x = 0; x < prevX; x++) {
	int px, py;
	unsigned char coefv, coefh;

	/* calculer px et py en fonction de */
	/*   x,y,middleX,middleY et theMode */
	calculatePXandPY (x, y, &px, &py);
	if ((px == x << 4) && (py == y << 4))
	  py += 8;

	if ((py < 0) || (px < 0) ||
	    (py >= (prevY - 1) * sqrtperte) ||
	    (px >= (prevX - 1) * sqrtperte)) {
#ifdef USE_ASM
	  coeffs[(y * prevX + x) * 2] = 0;
	  coeffs[(y * prevX + x) * 2 + 1] = 0;
#else
	  pos10[y * prevX + x] = 0;
	  c1[y * prevX + x] = 0;
	  c2[y * prevX + x] = 0;
	  c3[y * prevX + x] = 0;
	  c4[y * prevX + x] = 0;
#endif
	} else {
	  int npx10;
	  int npy10;
	  int pos;

	  npx10 = (px / sqrtperte);
	  npy10 = (py / sqrtperte);

/*			  if (npx10 >= prevX) fprintf(stderr,"error npx:%d",npx10);
			  if (npy10 >= prevY) fprintf(stderr,"error npy:%d",npy10);
*/
	  coefh = px % sqrtperte;
	  coefv = py % sqrtperte;
#ifdef USE_ASM
	  pos = (y * prevX + x) * 2;
	  coeffs[pos] = (npx10 + prevX * npy10) * 4;

	  if (!(coefh || coefv))
	    coeffs[pos + 1] = (sqrtperte * sqrtperte - 1);
	  else
	    coeffs[pos + 1] = ((sqrtperte - coefh) * (sqrtperte - coefv));

	  coeffs[pos + 1] |= (coefh * (sqrtperte - coefv)) << 8;
	  coeffs[pos + 1] |= ((sqrtperte - coefh) * coefv) << 16;
	  coeffs[pos + 1] |= (coefh * coefv) << 24;
#else
	  pos = y * prevX + x;
	  pos10[pos] = npx10 + prevX * npy10;

	  if (!(coefh || coefv))
	    c1[pos] = sqrtperte * sqrtperte - 1;
	  else
	    c1[pos] = (sqrtperte - coefh) * (sqrtperte - coefv);

	  c2[pos] = coefh * (sqrtperte - coefv);
	  c3[pos] = (sqrtperte - coefh) * coefv;
	  c4[pos] = coefh * coefv;
#endif
	}
      }
  }
#ifdef USE_ASM

#ifdef MMX
  zoom_width = prevX;
  mmx_zoom_size = prevX * prevY;
  mmx_zoom ();
#endif

#ifdef POWERPC
  zoom_width = prevX;
  if (useAltivec) {
    ppcsize4 = ((unsigned int) (prevX * prevY)) / 4;
    ppc_zoom_altivec ();
  } else {
    ppcsize4 = ((unsigned int) (prevX * prevY));
    ppc_zoom ();
  }
#endif
#else
  for (position = 0; position < prevX * prevY; position++) {
    getPixelRGB_ (pix1, pos10[position], &col1);
    getPixelRGB_ (pix1, pos10[position] + 1, &col2);
    getPixelRGB_ (pix1, pos10[position] + prevX, &col3);
    getPixelRGB_ (pix1, pos10[position] + prevX + 1, &col4);

    couleur.r = col1.r * c1[position]
	+ col2.r * c2[position]
	+ col3.r * c3[position]
	+ col4.r * c4[position];
    couleur.r >>= pertedec;

    couleur.v = col1.v * c1[position]
	+ col2.v * c2[position]
	+ col3.v * c3[position]
	+ col4.v * c4[position];
    couleur.v >>= pertedec;

    couleur.b = col1.b * c1[position]
	+ col2.b * c2[position]
	+ col3.b * c3[position]
	+ col4.b * c4[position];
    couleur.b >>= pertedec;

    setPixelRGB_ (pix2, position, couleur);
  }
#endif
}


void
pointFilter (Uint * pix1, Color c,
    float t1, float t2, float t3, float t4, Uint cycle)
{
  Uint x = (Uint) ((int) middleX + (int) (t1 * cos ((float) cycle / t3)));
  Uint y = (Uint) ((int) middleY + (int) (t2 * sin ((float) cycle / t4)));

  if ((x > 1) && (y > 1) && (x < resolx - 2) && (y < resoly - 2)) {
    setPixelRGB (pix1, x + 1, y, c);
    setPixelRGB (pix1, x, y + 1, c);
    setPixelRGB (pix1, x + 1, y + 1, WHITE);
    setPixelRGB (pix1, x + 2, y + 1, c);
    setPixelRGB (pix1, x + 1, y + 2, c);
  }
}
