
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#include <resample.h>

#define AMP 16000
#define I_RATE 48000
#define O_RATE 44100
/*#define O_RATE 24000 */

/*#define test_func(x) 1 */
/*#define test_func(x) sin(2*M_PI*(x)*10) */
/*#define test_func(x) sin(2*M_PI*(x)*(x)*1000) */
#define test_func(x) sin(2*M_PI*(x)*(x)*12000)

short i_buf[I_RATE * 2 * 2];
short o_buf[O_RATE * 2 * 2];

static int i_offset;
static int o_offset;

FILE *out;

void test_res1 (void);
void test_res2 (void);
void test_res3 (void);
void test_res4 (void);
void test_res5 (void);
void test_res6 (void);
void test_res7 (void);

int
main (int argc, char *argv[])
{
  out = fopen ("out", "w");

  test_res7 ();

  return 0;
}

void *
get_buffer (void *priv, unsigned int size)
{
  void *ret;

  ret = ((void *) o_buf) + o_offset;
  o_offset += size;
  return ret;
}

struct timeval start_time;
void
start_timer (void)
{
  gettimeofday (&start_time, NULL);
  /*printf("start %ld.%06ld\n",start_time.tv_sec,start_time.tv_usec); */
}

void
end_timer (void)
{
  struct timeval end_time;
  double diff;

  gettimeofday (&end_time, NULL);
  /*printf("end %ld.%06ld\n",end_time.tv_sec,end_time.tv_usec); */
  diff = (end_time.tv_sec - start_time.tv_sec) +
      1e-6 * (end_time.tv_usec - start_time.tv_usec);

  printf ("time %g\n", diff);

}

void
test_res1 (void)
{
  resample_t *r;
  int i;
  double sum10k, sum22k;
  double f;
  int n10k, n22k;
  double x;

  for (i = 0; i < I_RATE; i++) {
    i_buf[i * 2 + 0] = rint (AMP * test_func ((double) i / I_RATE));
    /*i_buf[i*2+1] = rint(AMP * test_func((double)i/I_RATE)); */
    i_buf[i * 2 + 1] = (i < 1000) ? AMP : 0;
  }

  r = malloc (sizeof (resample_t));
  memset (r, 0, sizeof (resample_t));

  r->i_rate = I_RATE;
  r->o_rate = O_RATE;
  /*r->method = RESAMPLE_SINC_SLOW; */
  r->method = RESAMPLE_SINC;
  r->channels = 2;
  /*r->verbose = 1; */
  r->filter_length = 64;
  r->get_buffer = get_buffer;

  resample_init (r);

  start_timer ();
#define blocked
#ifdef blocked
  for (i = 0; i + 256 < I_RATE; i += 256) {
    resample_scale (r, i_buf + i * 2, 256 * 2 * 2);
  }
  if (I_RATE - i) {
    resample_scale (r, i_buf + i * 2, (I_RATE - i) * 2 * 2);
  }
#else
  resample_scale (r, i_buf, I_RATE * 2 * 2);
#endif
  end_timer ();

  for (i = 0; i < O_RATE; i++) {
    f = AMP * test_func ((double) i / O_RATE);
    /*f = rint(AMP*test_func((double)i/O_RATE)); */
    fprintf (out, "%d %d %d %g %g\n", i,
	o_buf[2 * i + 0], o_buf[2 * i + 1], f, o_buf[2 * i + 0] - f);
  }

  sum10k = 0;
  sum22k = 0;
  n10k = 0;
  n22k = 0;
  for (i = 0; i < O_RATE; i++) {
    f = AMP * test_func ((double) i / O_RATE);
    /*f = rint(AMP*test_func((double)i/O_RATE)); */
    x = o_buf[2 * i + 0] - f;
    if (((0.5 * i) / O_RATE * I_RATE) < 10000) {
      sum10k += x * x;
      n10k++;
    }
    if (((0.5 * i) / O_RATE * I_RATE) < 22050) {
      sum22k += x * x;
      n22k++;
    }
  }
  printf ("average error 10k=%g 22k=%g\n",
      sqrt (sum10k / n10k), sqrt (sum22k / n22k));
}


void
test_res2 (void)
{
  functable_t *t;
  int i;
  double x;
  double f1, f2;

  t = malloc (sizeof (*t));
  memset (t, 0, sizeof (*t));

  t->start = -50.0;
  t->offset = 1;
  t->len = 100;

  t->func_x = functable_sinc;
  t->func_dx = functable_dsinc;

  functable_init (t);

  for (i = 0; i < 1000; i++) {
    x = -50.0 + 0.1 * i;
    f1 = functable_sinc (NULL, x);
    f2 = functable_eval (t, x);
    fprintf (out, "%d %g %g %g\n", i, f1, f2, f1 - f2);
  }
}

void
test_res3 (void)
{
  functable_t *t;
  int i;
  double x;
  double f1, f2;
  int n = 1;

  t = malloc (sizeof (*t));
  memset (t, 0, sizeof (*t));

  t->start = -50.0;
  t->offset = 1.0 / n;
  t->len = 100 * n;

  t->func_x = functable_sinc;
  t->func_dx = functable_dsinc;

  t->func2_x = functable_window_std;
  t->func2_dx = functable_window_dstd;

  t->scale = 1.0;
  t->scale2 = 1.0 / (M_PI * 16);

  functable_init (t);

  for (i = 0; i < 1000 * n; i++) {
    x = -50.0 + 0.1 / n * i;
    f1 = functable_sinc (NULL, t->scale * x) *
	functable_window_std (NULL, t->scale2 * x);
    f2 = functable_eval (t, x);
    fprintf (out, "%d %g %g %g\n", i, f1, f2, f2 - f1);
  }
}

double
sinc_poly (double x)
{
#define INV3FAC 1.66666666666666666e-1
#define INV5FAC 8.33333333333333333e-3
#define INV7FAC 1.984126984e-4
#define INV9FAC 2.755731922e-6
#define INV11FAC 2.505210839e-8
  double x2 = x * x;

  return 1 - x2 * INV3FAC + x2 * x2 * INV5FAC - x2 * x2 * x2 * INV7FAC;
  /*+ x2 * x2 * x2 * x2 * INV9FAC */
		/*- x2 * x2 * x2 * x2 * x2 * INV11FAC; */
}

void
test_res4 (void)
{
  int i;
  double x, f1, f2;

  for (i = 1; i < 100; i++) {
    x = 0.01 * i;
    f1 = 1 - sin (x) / x;
    f2 = 1 - sinc_poly (x);

    fprintf (out, "%g %.20g %.20g %.20g\n", x, f1, f2, f2 - f1);
  }
}


void
test_res5 (void)
{
  int i;
  double sum;

  start_timer ();
  sum = 0;
  for (i = 0; i < I_RATE; i++) {
    sum += i_buf[i * 2];
  }
  end_timer ();
  i_buf[0] = sum;
}


void
short_to_double (double *d, short *x)
{
  *d = *x;
}

void
short_to_float (float *f, short *x)
{
  *f = *x;
}

void
float_to_double (double *f, float *x)
{
  *f = *x;
}

void
double_to_short (short *f, double *x)
{
  *f = *x;
}

double res6_tmp[1000];

void
test_res6 (void)
{
  int i;

  for (i = 0; i < I_RATE; i++) {
    i_buf[i] = rint (AMP * test_func ((double) i / I_RATE));
  }

  conv_double_short_ref (res6_tmp, i_buf, 1000);
  for (i = 0; i < 1000; i++) {
    res6_tmp[i] *= 3.0;
  }
  conv_short_double_ppcasm (o_buf, res6_tmp, 1000);

  for (i = 0; i < 1000; i++) {
    fprintf (out, "%d %d %g %d\n", i, i_buf[i], res6_tmp[i], o_buf[i]);
  }
}

void
test_res7 (void)
{
  resample_t *r;
  int i;
  double sum10k, sum22k;
  double f;
  int n10k, n22k;
  double x;

  for (i = 0; i < I_RATE; i++) {
    i_buf[i] = rint (AMP * test_func ((double) i / I_RATE));
  }

  r = malloc (sizeof (resample_t));
  memset (r, 0, sizeof (resample_t));

  r->i_rate = I_RATE;
  r->o_rate = O_RATE;
  /*r->method = RESAMPLE_SINC_SLOW; */
  r->method = RESAMPLE_SINC;
  r->channels = 1;
  /*r->verbose = 1; */
  r->filter_length = 64;
  r->get_buffer = get_buffer;

  resample_init (r);

  start_timer ();
#define blocked
#ifdef blocked
  for (i = 0; i + 256 < I_RATE; i += 256) {
    resample_scale (r, i_buf + i, 256 * 2);
  }
  if (I_RATE - i) {
    resample_scale (r, i_buf + i, (I_RATE - i) * 2);
  }
#else
  resample_scale (r, i_buf, I_RATE * 2);
#endif
  end_timer ();

  for (i = 0; i < O_RATE; i++) {
    f = AMP * test_func ((double) i / O_RATE);
    /*f = rint(AMP*test_func((double)i/O_RATE)); */
    fprintf (out, "%d %d %d %g %g\n", i, o_buf[i], 0, f, o_buf[i] - f);
  }

  sum10k = 0;
  sum22k = 0;
  n10k = 0;
  n22k = 0;
  for (i = 0; i < O_RATE; i++) {
    f = AMP * test_func ((double) i / O_RATE);
    /*f = rint(AMP*test_func((double)i/O_RATE)); */
    x = o_buf[i] - f;
    if (((0.5 * i) / O_RATE * I_RATE) < 10000) {
      sum10k += x * x;
      n10k++;
    }
    if (((0.5 * i) / O_RATE * I_RATE) < 22050) {
      sum22k += x * x;
      n22k++;
    }
  }
  printf ("average error 10k=%g 22k=%g\n",
      sqrt (sum10k / n10k), sqrt (sum22k / n22k));
}
