/*
 * ieeetest.c --- test IDCT code against the IEEE Std 1180-1990 spec
 *
 * Note that this does only one pass of the test.
 * Six invocations of ieeetest are needed to complete the entire spec.
 * The shell script "doieee" performs the complete test.
 *
 * Written by Tom Lane (tgl@cs.cmu.edu).
 * Released to public domain 11/22/93.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <gst/gst.h>
#include <gst/idct/idct.h>
#include "dct.h"


/* prototypes */

void usage (char *msg);
long ieeerand (long L, long H);
void dct_init (void);
void ref_fdct (DCTELEM block[8][8]);
void ref_idct (DCTELEM block[8][8]);

/* error stat accumulators -- assume initialized to 0 */

long sumerrs[DCTSIZE2];
long sumsqerrs[DCTSIZE2];
int maxerr[DCTSIZE2];


char *
meets (double val, double limit)
{
  return ((fabs (val) <= limit) ? "meets" : "FAILS");
}

int
main (int argc, char **argv)
{
  long minpix, maxpix, sign;
  long curiter, niters;
  int i, j;
  double max, total;
  int method;
  DCTELEM block[DCTSIZE2];	/* random source data */
  DCTELEM refcoefs[DCTSIZE2];	/* coefs from reference FDCT */
  DCTELEM refout[DCTSIZE2];	/* output from reference IDCT */
  DCTELEM testout[DCTSIZE2];	/* output from test IDCT */
  GstIDCT *idct;
  guint64 tscstart, tscmin = ~0, tscmax = 0;
  guint64 tscstop;

  /* Argument parsing --- not very bulletproof at all */

  if (argc != 6)
    usage (NULL);

  method = atoi (argv[1]);
  minpix = atoi (argv[2]);
  maxpix = atoi (argv[3]);
  sign = atoi (argv[4]);
  niters = atol (argv[5]);

  gst_library_load ("gstidct");

  idct = gst_idct_new (method);
  if (idct == 0) {
    printf ("method not available\n\n\n");

    return 0;
  }

  dct_init ();

  /* Loop once per generated random-data block */

  for (curiter = 0; curiter < niters; curiter++) {

    /* generate a pseudo-random block of data */
    for (i = 0; i < DCTSIZE2; i++)
      block[i] = (DCTELEM) (ieeerand (-minpix, maxpix) * sign);

    /* perform reference FDCT */
    memcpy (refcoefs, block, sizeof (DCTELEM) * DCTSIZE2);
    ref_fdct ((DCTELEM **) & refcoefs);
    /* clip */
    for (i = 0; i < DCTSIZE2; i++) {
      if (refcoefs[i] < -2048)
	refcoefs[i] = -2048;
      else if (refcoefs[i] > 2047)
	refcoefs[i] = 2047;
    }

    /* perform reference IDCT */
    memcpy (refout, refcoefs, sizeof (DCTELEM) * DCTSIZE2);
    ref_idct (refout);
    /* clip */
    for (i = 0; i < DCTSIZE2; i++) {
      if (refout[i] < -256)
	refout[i] = -256;
      else if (refout[i] > 255)
	refout[i] = 255;
    }

    /* perform test IDCT */
    if (GST_IDCT_TRANSPOSE (idct)) {
      for (j = 0; j < DCTSIZE; j++) {
	for (i = 0; i < DCTSIZE; i++) {
	  testout[i * DCTSIZE + j] = refcoefs[j * DCTSIZE + i];
	}
      }
    } else {
      memcpy (testout, refcoefs, sizeof (DCTELEM) * DCTSIZE2);
    }

    gst_trace_read_tsc (&tscstart);
    gst_idct_convert (idct, testout);
    gst_trace_read_tsc (&tscstop);
    /*printf("time %llu, %llu %lld\n", tscstart, tscstop, tscstop-tscstart); */
    if (tscstop - tscstart < tscmin)
      tscmin = tscstop - tscstart;
    if (tscstop - tscstart > tscmax)
      tscmax = tscstop - tscstart;

    /* clip */
    for (i = 0; i < DCTSIZE2; i++) {
      if (testout[i] < -256)
	testout[i] = -256;
      else if (testout[i] > 255)
	testout[i] = 255;
    }

    /* accumulate error stats */
    for (i = 0; i < DCTSIZE2; i++) {
      register int err = testout[i] - refout[i];

      sumerrs[i] += err;
      sumsqerrs[i] += err * err;
      if (err < 0)
	err = -err;
      if (maxerr[i] < err)
	maxerr[i] = err;
    }

    if (curiter % 100 == 99) {
      fprintf (stderr, ".");
      fflush (stderr);
    }
  }
  fprintf (stderr, "\n");

  /* print results */

  printf
      ("IEEE test conditions: -L = %ld, +H = %ld, sign = %ld, #iters = %ld\n",
      minpix, maxpix, sign, niters);

  printf ("Speed, min time %lld, max %lld\n", tscmin, tscmax);

  printf ("Peak absolute values of errors:\n");
  for (i = 0, j = 0; i < DCTSIZE2; i++) {
    if (j < maxerr[i])
      j = maxerr[i];
    printf ("%4d", maxerr[i]);
    if ((i % DCTSIZE) == DCTSIZE - 1)
      printf ("\n");
  }
  printf ("Worst peak error = %d  (%s spec limit 1)\n\n", j,
      meets ((double) j, 1.0));

  printf ("Mean square errors:\n");
  max = total = 0.0;
  for (i = 0; i < DCTSIZE2; i++) {
    double err = (double) sumsqerrs[i] / ((double) niters);

    total += (double) sumsqerrs[i];
    if (max < err)
      max = err;
    printf (" %8.4f", err);
    if ((i % DCTSIZE) == DCTSIZE - 1)
      printf ("\n");
  }
  printf ("Worst pmse = %.6f  (%s spec limit 0.06)\n", max, meets (max, 0.06));
  total /= (double) (64 * niters);
  printf ("Overall mse = %.6f  (%s spec limit 0.02)\n\n", total,
      meets (total, 0.02));

  printf ("Mean errors:\n");
  max = total = 0.0;
  for (i = 0; i < DCTSIZE2; i++) {
    double err = (double) sumerrs[i] / ((double) niters);

    total += (double) sumerrs[i];
    printf (" %8.4f", err);
    if (err < 0.0)
      err = -err;
    if (max < err)
      max = err;
    if ((i % DCTSIZE) == DCTSIZE - 1)
      printf ("\n");
  }
  printf ("Worst mean error = %.6f  (%s spec limit 0.015)\n", max,
      meets (max, 0.015));
  total /= (double) (64 * niters);
  printf ("Overall mean error = %.6f  (%s spec limit 0.0015)\n\n", total,
      meets (total, 0.0015));

  /* test for 0 input giving 0 output */
  memset (testout, 0, sizeof (DCTELEM) * DCTSIZE2);
  gst_idct_convert (idct, testout);
  for (i = 0, j = 0; i < DCTSIZE2; i++) {
    if (testout[i]) {
      printf ("Position %d of IDCT(0) = %d (FAILS)\n", i, testout[i]);
      j++;
    }
  }
  printf ("%d elements of IDCT(0) were not zero\n\n\n", j);

  exit (0);
  return 0;
}


void
usage (char *msg)
{
  if (msg != NULL)
    fprintf (stderr, "\nerror: %s\n", msg);

  fprintf (stderr, "\n");
  fprintf (stderr, "usage: ieeetest minpix maxpix sign niters\n");
  fprintf (stderr, "\n");
  fprintf (stderr, "  test = 1 - 5\n");
  fprintf (stderr, "  minpix = -L value per IEEE spec\n");
  fprintf (stderr, "  maxpix =  H value per IEEE spec\n");
  fprintf (stderr, "  sign = +1 for normal, -1 to run negated test\n");
  fprintf (stderr, "  niters = # iterations (10000 for full test)\n");
  fprintf (stderr, "\n");

  exit (1);
}


/* Pseudo-random generator specified by IEEE 1180 */

long
ieeerand (long L, long H)
{
  static long randx = 1;
  static double z = (double) 0x7fffffff;

  long i, j;
  double x;

  randx = (randx * 1103515245) + 12345;
  i = randx & 0x7ffffffe;
  x = ((double) i) / z;
  x *= (L + H + 1);
  j = x;
  return j - L;
}


/* Reference double-precision FDCT and IDCT */


/* The cosine lookup table */
/* coslu[a][b] = C(b)/2 * cos[(2a+1)b*pi/16] */
double coslu[8][8];


/* Routine to initialise the cosine lookup table */
void
dct_init (void)
{
  int a, b;
  double tmp;

  for (a = 0; a < 8; a++)
    for (b = 0; b < 8; b++) {
      tmp = cos ((double) ((a + a + 1) * b) * (3.14159265358979323846 / 16.0));
      if (b == 0)
	tmp /= sqrt (2.0);
      coslu[a][b] = tmp * 0.5;
    }
}


void
ref_fdct (DCTELEM block[8][8])
{
  int x, y, u, v;
  double tmp, tmp2;
  double res[8][8];

  for (v = 0; v < 8; v++) {
    for (u = 0; u < 8; u++) {
      tmp = 0.0;
      for (y = 0; y < 8; y++) {
	tmp2 = 0.0;
	for (x = 0; x < 8; x++) {
	  tmp2 += (double) block[y][x] * coslu[x][u];
	}
	tmp += coslu[y][v] * tmp2;
      }
      res[v][u] = tmp;
    }
  }

  for (v = 0; v < 8; v++) {
    for (u = 0; u < 8; u++) {
      tmp = res[v][u];
      if (tmp < 0.0) {
	x = -((int) (0.5 - tmp));
      } else {
	x = (int) (tmp + 0.5);
      }
      block[v][u] = (DCTELEM) x;
    }
  }
}


void
ref_idct (DCTELEM block[8][8])
{
  int x, y, u, v;
  double tmp, tmp2;
  double res[8][8];

  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      tmp = 0.0;
      for (v = 0; v < 8; v++) {
	tmp2 = 0.0;
	for (u = 0; u < 8; u++) {
	  tmp2 += (double) block[v][u] * coslu[x][u];
	}
	tmp += coslu[y][v] * tmp2;
      }
      res[y][x] = tmp;
    }
  }

  for (v = 0; v < 8; v++) {
    for (u = 0; u < 8; u++) {
      tmp = res[v][u];
      if (tmp < 0.0) {
	x = -((int) (0.5 - tmp));
      } else {
	x = (int) (tmp + 0.5);
      }
      block[v][u] = (DCTELEM) x;
    }
  }
}
