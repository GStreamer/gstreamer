
#include <sys/soundcard.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <glib.h>

typedef struct _Probe Probe;
struct _Probe
{
  int fd;
  int format;
  int n_channels;
  GArray *rates;
};

typedef struct _Range Range;
struct _Range
{
  int min;
  int max;
};

static gboolean probe_check (Probe * probe);
static int check_rate (Probe * probe, int irate);
static GList *add_range (GList * list, int min, int max);
static void add_rate (GArray * array, int rate);

int
main (int argc, char *argv[])
{
  int fd;
  int n;
  int *rates;
  int i;
  Probe *probe;

  fd = open ("/dev/dsp", O_RDWR);
  if (fd < 0) {
    perror ("/dev/dsp");
    exit (1);
  }

  probe = g_new0 (Probe, 1);
  probe->fd = fd;
  probe->format = AFMT_S16_LE;
  probe->n_channels = 2;

  probe_check (probe);
  for (i = 0; i < probe->rates->len; i++) {
    g_print ("%d\n", g_array_index (probe->rates, int, i));
  }

#if 0
  probe = g_new0 (Probe, 1);
  probe->fd = fd;
  probe->format = AFMT_S16_LE;
  probe->n_channels = 1;

  probe_check (probe);
  for (i = 0; i < probe->rates->len; i++) {
    g_print ("%d\n", g_array_index (probe->rates, int, i));
  }

  probe = g_new0 (Probe, 1);
  probe->fd = fd;
  probe->format = AFMT_U8;
  probe->n_channels = 2;

  probe_check (probe);
  for (i = 0; i < probe->rates->len; i++) {
    g_print ("%d\n", g_array_index (probe->rates, int, i));
  }

  probe = g_new0 (Probe, 1);
  probe->fd = fd;
  probe->format = AFMT_U8;
  probe->n_channels = 1;

  probe_check (probe);
  for (i = 0; i < probe->rates->len; i++) {
    g_print ("%d\n", g_array_index (probe->rates, int, i));
  }
#endif

  return 0;
}

static gboolean
probe_check (Probe * probe)
{
  GList *item;
  Range *range;
  GList *new_ranges;
  GList *ranges;
  int i;
  int min, max;
  int exact_rates = 0;
  gboolean checking_exact_rates = TRUE;
  int n_checks = 0;

  probe->rates = g_array_new (FALSE, FALSE, sizeof (int));

  min = check_rate (probe, 1000);
  add_rate (probe->rates, min);
  n_checks++;
  max = check_rate (probe, 100000);
  add_rate (probe->rates, max);
  n_checks++;
  ranges = add_range (NULL, min + 1, max - 1);

  for (i = 0; i < 20; i++) {
    new_ranges = NULL;

    if (ranges == NULL) {
      g_print ("no more ranges, probe complete\n");
      g_print ("n_checks = %d\n", n_checks);

      return TRUE;
    }
    for (item = ranges; item; item = item->next) {
      int min1;
      int max1;
      int mid;
      int mid_ret;

      range = item->data;

      g_print ("checking [%d,%d]\n", range->min, range->max);

      mid = (range->min + range->max) / 2;
      mid_ret = check_rate (probe, mid);
      n_checks++;

      if (mid == mid_ret && checking_exact_rates) {
        exact_rates++;
        if (exact_rates > 100) {
          g_print ("got 100 exact rates, assuming all are exact\n");
          return 0;
        }
      } else {
        checking_exact_rates = FALSE;
      }

      add_rate (probe->rates, mid_ret);

#if 1
      /* Assume that the rate is arithmetically rounded to the nearest
       * supported rate. */
      if (mid < mid_ret) {
        min1 = mid - (mid_ret - mid);
        max1 = mid_ret + 1;
      } else {
        min1 = mid_ret - 1;
        max1 = mid + (mid - mid_ret);
      }
#else
      /* Assume that the rate is not rounded past a supported rate */
      if (mid < mid_ret) {
        min1 = mid - 1;
        max1 = mid_ret + 1;
      } else {
        min1 = mid_ret - 1;
        max1 = mid + 1;
      }
#endif

      if (range->min < min1) {
        new_ranges = add_range (new_ranges, range->min, min1);
      }
      if (max1 < range->max) {
        new_ranges = add_range (new_ranges, max1, range->max);
      }
    }

    /* leak ranges */

    ranges = new_ranges;
  }

  return 0;
}

static GList *
add_range (GList * list, int min, int max)
{
  Range *range = g_new0 (Range, 1);

  range->min = min;
  range->max = max;

  return g_list_append (list, range);
}

static int
check_rate (Probe * probe, int irate)
{
  int rate;
  int format;
  int n_channels;

  rate = irate;
  format = probe->format;
  n_channels = probe->n_channels;

  ioctl (probe->fd, SNDCTL_DSP_SETFMT, &format);
  ioctl (probe->fd, SNDCTL_DSP_CHANNELS, &n_channels);
  ioctl (probe->fd, SNDCTL_DSP_SPEED, &rate);

  g_print ("rate %d -> %d\n", irate, rate);

  if (rate == irate - 1 || rate == irate + 1) {
    rate = irate;
  }
  return rate;
}

static void
add_rate (GArray * array, int rate)
{
  int i;
  int val;

  for (i = 0; i < array->len; i++) {
    val = g_array_index (array, int, i);

    if (val == rate)
      return;
  }
  g_print ("supported rate: %d\n", rate);
  g_array_append_val (array, rate);
}
