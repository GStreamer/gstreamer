#ifndef FILTERS_H
#define FILTERS_H

#include <glib.h>

#include "graphic.h"

typedef struct
{
  int vitesse;
  unsigned char pertedec;
  unsigned char sqrtperte;
  int middleX, middleY;
  char reverse;
  char mode;
	/** @since June 2001 */
  int hPlaneEffect;
  int vPlaneEffect;
  char noisify;
} ZoomFilterData;


#define NORMAL_MODE 0
#define WAVE_MODE 1
#define CRYSTAL_BALL_MODE 2
#define SCRUNCH_MODE 3
#define AMULETTE_MODE 4
#define WATER_MODE 5

void pointFilter (guint32 * pix1, Color c,
    float t1, float t2, float t3, float t4, guint32 cycle);

/* filtre de zoom :
 le contenu de pix1 est copie dans pix2, avec l'effet appliqué
 midx et midy represente le centre du zoom

void zoomFilter(Uint *pix1, Uint *pix2, Uint middleX, Uint middleY);
void zoomFilterRGB(Uint *pix1,
Uint *pix2,
Uint middleX,
Uint middleY);
*/

void zoomFilterFastRGB (guint32 * pix1,
    guint32 * pix2, ZoomFilterData * zf, guint32 resx, guint32 resy);


/* filtre sin :
 le contenu de pix1 est copie dans pix2, avec l'effet appliqué
 cycle est la variable de temps.
 mode vaut SIN_MUL ou SIN_ADD
 rate est le pourcentage de l'effet appliqué
 lenght : la longueur d'onde (1..10) [5]
 speed : la vitesse (1..100) [10]
*/
/*
void sinFilter(Uint *pix1,Uint *pix2,
			   Uint cycle,
			   Uint mode,
			   Uint rate,
			   char lenght,
			   Uint speed);
*/

#define SIN_MUL 1
#define SIN_ADD 2

#endif
