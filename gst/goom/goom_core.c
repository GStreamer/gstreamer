#include <stdlib.h>
#include <string.h>
#include "goom_core.h"
#include "goom_tools.h"
#include "filters.h"
#include "lines.h"

/*#define VERBOSE */

#ifdef VERBOSE
#include <stdio.h>
#endif

#define STOP_SPEED 128


/**-----------------------------------------------------**
 **  SHARED DATA                                        **
 **-----------------------------------------------------**/
static guint32 *pixel;
static guint32 *back;
static guint32 *p1, *p2, *tmp;
static guint32 cycle;

guint32 resolx, resoly, buffsize;

void
goom_init (guint32 resx, guint32 resy)
{
#ifdef VERBOSE
  printf ("GOOM: init (%d, %d);\n", resx, resy);
#endif
  resolx = resx;
  resoly = resy;
  buffsize = resx * resy;

  pixel = (guint32 *) malloc (buffsize * sizeof (guint32) + 128);
  back = (guint32 *) malloc (buffsize * sizeof (guint32) + 128);
  RAND_INIT (GPOINTER_TO_INT (pixel));
  cycle = 0;

  p1 = (void *) (((unsigned long) pixel + 0x7f) & (~0x7f));
  p2 = (void *) (((unsigned long) back + 0x7f) & (~0x7f));
}


void
goom_set_resolution (guint32 resx, guint32 resy)
{
  free (pixel);
  free (back);

  resolx = resx;
  resoly = resy;
  buffsize = resx * resy;

  pixel = (guint32 *) malloc (buffsize * sizeof (guint32) + 128);
  memset (pixel, 0, buffsize * sizeof (guint32) + 128);
  back = (guint32 *) malloc (buffsize * sizeof (guint32) + 128);
  memset (back, 0, buffsize * sizeof (guint32) + 128);

  p1 = (void *) (((unsigned long) pixel + 0x7f) & (~0x7f));
  p2 = (void *) (((unsigned long) back + 0x7f) & (~0x7f));
}


guint32 *
goom_update (gint16 data[2][512])
{
  static int lockvar = 0;       /* pour empecher de nouveaux changements */
  static int goomvar = 0;       /* boucle des gooms */
  static int totalgoom = 0;     /* nombre de gooms par seconds */
  static int agoom = 0;         /* un goom a eu lieu..       */
  static int loopvar = 0;       /* mouvement des points */
  static int speedvar = 0;      /* vitesse des particules */
  static int lineMode = 0;      /* l'effet lineaire a dessiner */
  guint32 *return_val;
  guint32 pointWidth;
  guint32 pointHeight;
  int incvar;                   /* volume du son */
  int accelvar;                 /* acceleration des particules */
  int i;
  float largfactor;             /* elargissement de l'intervalle d'évolution des points */
  static char goomlimit = 2;    /* sensibilité du goom */
  static ZoomFilterData zfd = {
    128, 8, 16,
    1, 1, 0, WAVE_MODE,
    0, 0, 0
  };

  ZoomFilterData *pzfd;

  /* test if the config has changed, update it if so */
  pointWidth = (resolx * 2) / 5;
  pointHeight = (resoly * 2) / 5;

  /* ! etude du signal ... */
  incvar = 0;
  for (i = 0; i < 512; i++) {
    if (incvar < data[0][i])
      incvar = data[0][i];
  }

  accelvar = incvar / 5000;
  if (speedvar > 5) {
    accelvar--;
    if (speedvar > 20)
      accelvar--;
    if (speedvar > 40)
      speedvar = 40;
  }
  accelvar--;
  speedvar += accelvar;

  if (speedvar < 0)
    speedvar = 0;
  if (speedvar > 40)
    speedvar = 40;


  /* ! calcul du deplacement des petits points ... */

  largfactor = ((float) speedvar / 40.0f + (float) incvar / 50000.0f) / 1.5f;
  if (largfactor > 1.5f)
    largfactor = 1.5f;

  for (i = 1; i * 15 <= speedvar + 15; i++) {
    loopvar += speedvar + 1;

    pointFilter (p1,
        YELLOW,
        ((pointWidth - 6.0f) * largfactor + 5.0f),
        ((pointHeight - 6.0f) * largfactor + 5.0f),
        i * 152.0f, 128.0f, loopvar + i * 2032);
    pointFilter (p1, ORANGE,
        ((pointWidth / 2) * largfactor) / i + 10.0f * i,
        ((pointHeight / 2) * largfactor) / i + 10.0f * i,
        96.0f, i * 80.0f, loopvar / i);
    pointFilter (p1, VIOLET,
        ((pointHeight / 3 + 5.0f) * largfactor) / i + 10.0f * i,
        ((pointHeight / 3 + 5.0f) * largfactor) / i + 10.0f * i,
        i + 122.0f, 134.0f, loopvar / i);
    pointFilter (p1, BLACK,
        ((pointHeight / 3) * largfactor + 20.0f),
        ((pointHeight / 3) * largfactor + 20.0f),
        58.0f, i * 66.0f, loopvar / i);
    pointFilter (p1, WHITE,
        (pointHeight * largfactor + 10.0f * i) / i,
        (pointHeight * largfactor + 10.0f * i) / i,
        66.0f, 74.0f, loopvar + i * 500);
  }

  /* par défaut pas de changement de zoom */
  pzfd = NULL;

  /* diminuer de 1 le temps de lockage */
  /* note pour ceux qui n'ont pas suivis : le lockvar permet d'empecher un */
  /* changement d'etat du plugins juste apres un autre changement d'etat. oki ? */
  if (--lockvar < 0)
    lockvar = 0;

  /* temps du goom */
  if (--agoom < 0)
    agoom = 0;

  /* on verifie qu'il ne se pas un truc interressant avec le son. */
  if ((accelvar > goomlimit) || (accelvar < -goomlimit)) {
    /* UN GOOM !!! YAHOO ! */
    totalgoom++;
    agoom = 20;                 /* mais pdt 20 cycles, il n'y en aura plus. */
    lineMode = (lineMode + 1) % 20;     /* Tous les 10 gooms on change de mode lineaire */

    /* changement eventuel de mode */
    switch (iRAND (10)) {
      case 0:
      case 1:
      case 2:
        zfd.mode = WAVE_MODE;
        zfd.vitesse = STOP_SPEED - 1;
        zfd.reverse = 0;
        break;
      case 3:
      case 4:
        zfd.mode = CRYSTAL_BALL_MODE;
        break;
      case 5:
        zfd.mode = AMULETTE_MODE;
        break;
      case 6:
        zfd.mode = WATER_MODE;
        break;
      case 7:
        zfd.mode = SCRUNCH_MODE;
        break;
      default:
        zfd.mode = NORMAL_MODE;
    }
  }

  /* tout ceci ne sera fait qu'en cas de non-blocage */
  if (lockvar == 0) {
    /* reperage de goom (acceleration forte de l'acceleration du volume) */
    /* -> coup de boost de la vitesse si besoin.. */
    if ((accelvar > goomlimit) || (accelvar < -goomlimit)) {
      goomvar++;
      /*if (goomvar % 1 == 0) */
      {
        guint32 vtmp;
        guint32 newvit;

        newvit = STOP_SPEED - speedvar / 2;
        /* retablir le zoom avant.. */
        if ((zfd.reverse) && (!(cycle % 12)) && (rand () % 3 == 0)) {
          zfd.reverse = 0;
          zfd.vitesse = STOP_SPEED - 2;
          lockvar = 50;
        }
        if (iRAND (10) == 0) {
          zfd.reverse = 1;
          lockvar = 100;
        }

        /* changement de milieu.. */
        switch (iRAND (20)) {
          case 0:
            zfd.middleY = resoly - 1;
            zfd.middleX = resolx / 2;
            break;
          case 1:
            zfd.middleX = resolx - 1;
            break;
          case 2:
            zfd.middleX = 1;
            break;
          default:
            zfd.middleY = resoly / 2;
            zfd.middleX = resolx / 2;
        }

        if (zfd.mode == WATER_MODE) {
          zfd.middleX = resolx / 2;
          zfd.middleY = resoly / 2;
        }

        switch (vtmp = (iRAND (27))) {
          case 0:
            zfd.vPlaneEffect = iRAND (3);
            zfd.vPlaneEffect -= iRAND (3);
            zfd.hPlaneEffect = iRAND (3);
            zfd.hPlaneEffect -= iRAND (3);
            break;
          case 3:
            zfd.vPlaneEffect = 0;
            zfd.hPlaneEffect = iRAND (8);
            zfd.hPlaneEffect -= iRAND (8);
            break;
          case 4:
          case 5:
          case 6:
          case 7:
            zfd.vPlaneEffect = iRAND (5);
            zfd.vPlaneEffect -= iRAND (5);
            zfd.hPlaneEffect = -zfd.vPlaneEffect;
            break;
          case 8:
            zfd.hPlaneEffect = 5 + iRAND (8);
            zfd.vPlaneEffect = -zfd.hPlaneEffect;
            break;
          case 9:
            zfd.vPlaneEffect = 5 + iRAND (8);
            zfd.hPlaneEffect = -zfd.hPlaneEffect;
            break;
          case 13:
            zfd.hPlaneEffect = 0;
            zfd.vPlaneEffect = iRAND (10);
            zfd.vPlaneEffect -= iRAND (10);
            break;
          default:
            if (vtmp < 10) {
              zfd.vPlaneEffect = 0;
              zfd.hPlaneEffect = 0;
            }
        }

        if (iRAND (3) != 0)
          zfd.noisify = 0;
        else {
          zfd.noisify = iRAND (3) + 2;
          lockvar *= 3;
        }

        if (zfd.mode == AMULETTE_MODE) {
          zfd.vPlaneEffect = 0;
          zfd.hPlaneEffect = 0;
          zfd.noisify = 0;
        }

        if ((zfd.middleX == 1) || (zfd.middleX == resolx - 1)) {
          zfd.vPlaneEffect = 0;
          zfd.hPlaneEffect = iRAND (2) ? 0 : zfd.hPlaneEffect;
        }

        if (newvit < zfd.vitesse) {     /* on accelere */
          pzfd = &zfd;
          if (((newvit < STOP_SPEED - 7) &&
                  (zfd.vitesse < STOP_SPEED - 6) &&
                  (cycle % 3 == 0)) || (iRAND (40) == 0)) {
            zfd.vitesse = STOP_SPEED - 1;
            zfd.reverse = !zfd.reverse;
          } else {
            zfd.vitesse = (newvit + zfd.vitesse * 4) / 5;
          }
          lockvar += 50;
        }
      }
    }
    /* mode mega-lent */
    if (iRAND (1000) == 0) {
      /* 
         printf ("coup du sort...\n") ;
       */
      pzfd = &zfd;
      zfd.vitesse = STOP_SPEED - 1;
      zfd.pertedec = 8;
      zfd.sqrtperte = 16;
      goomvar = 1;
      lockvar += 70;
    }
  }

  /* gros frein si la musique est calme */
  if ((speedvar < 1) && (zfd.vitesse < STOP_SPEED - 4) && (cycle % 16 == 0)) {
    /*
       printf ("++slow part... %i\n", zfd.vitesse) ;
     */
    pzfd = &zfd;
    zfd.vitesse += 3;
    zfd.pertedec = 8;
    zfd.sqrtperte = 16;
    goomvar = 0;
    /*
       printf ("--slow part... %i\n", zfd.vitesse) ;
     */
  }

  /* baisser regulierement la vitesse... */
  if ((cycle % 73 == 0) && (zfd.vitesse < STOP_SPEED - 5)) {
    /*
       printf ("slow down...\n") ;
     */
    pzfd = &zfd;
    zfd.vitesse++;
  }

  /* arreter de decrémenter au bout d'un certain temps */
  if ((cycle % 101 == 0) && (zfd.pertedec == 7)) {
    pzfd = &zfd;
    zfd.pertedec = 8;
    zfd.sqrtperte = 16;
  }
#ifdef VERBOSE
  if (pzfd) {
    printf ("GOOM: pzfd->mode = %d\n", pzfd->mode);
  }
#endif

  /* Zoom here ! */
  zoomFilterFastRGB (p1, p2, pzfd, resolx, resoly);

  /* si on est dans un goom : afficher les lignes... */
  if (agoom > 15)
    goom_lines
        (data, ((zfd.middleX == resolx / 2) && (zfd.middleY == resoly / 2)
            && (zfd.mode != WATER_MODE))
        ? (lineMode / 10) : 0, p2, agoom - 15);

  return_val = p2;
  tmp = p1;
  p1 = p2;
  p2 = tmp;

  /* affichage et swappage des buffers.. */
  cycle++;

  /* tous les 100 cycles : vérifier si le taux de goom est correct */
  /* et le modifier sinon.. */
  if (!(cycle % 100)) {
    if (totalgoom > 15) {
      /*  printf ("less gooms\n") ; */
      goomlimit++;
    } else {
      if ((totalgoom == 0) && (goomlimit > 1))
        goomlimit--;
    }
    totalgoom = 0;
  }
  return return_val;
}

void
goom_close ()
{
  if (pixel != NULL)
    free (pixel);
  if (back != NULL)
    free (back);
  pixel = back = NULL;
  RAND_CLOSE ();
}
