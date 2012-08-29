/*
 * A deterministic clock for GStreamer unit tests
 *
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) 2012 Sebastian Rasmussen <sebastian.rasmussen@axis.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:gsttestclock
 * @short_description: Controllable, deterministic clock for GStreamer unit tests
 * @see_also: #GstSystemClock, #GstClock
 *
 * GstTestClock is an implementation of #GstClock which has different
 * behaviour compared to #GstSystemClock. Time for #GstSystemClock advances
 * according to the system time, while time for #GstTestClock changes only
 * when gst_test_clock_set_time() or gst_test_clock_advance_time() are
 * called. #GstTestClock provides unit tests with the possibility to
 * precisely advance the time in a deterministic manner, independent of the
 * system time or any other external factors.
 *
 * <example>
 * <title>Advancing the time of a #GstTestClock</title>
 *   <programlisting language="c">
 *   #include &lt;gst/gst.h&gt;
 *   #include &lt;gst/check/gsttestclock.h&gt;
 *
 *   GstClock *clock;
 *   GstTestClock *test_clock;
 *
 *   clock = gst_test_clock_new ();
 *   test_clock = GST_TEST_CLOCK (clock);
 *   GST_INFO ("Time: %" GST_TIME_FORMAT, GST_TIME_ARGS (gst_clock_get_time (clock)));
 *   gst_test_clock_advance_time ( test_clock, 1 * GST_SECOND);
 *   GST_INFO ("Time: %" GST_TIME_FORMAT, GST_TIME_ARGS (gst_clock_get_time (clock)));
 *   g_usleep (10 * G_USEC_PER_SEC);
 *   GST_INFO ("Time: %" GST_TIME_FORMAT, GST_TIME_ARGS (gst_clock_get_time (clock)));
 *   gst_test_clock_set_time (test_clock, 42 * GST_SECOND);
 *   GST_INFO ("Time: %" GST_TIME_FORMAT, GST_TIME_ARGS (gst_clock_get_time (clock)));
 *   ...
 *   </programlisting>
 * </example>
 *
 * Since #GstTestClock is only supposed to be used in unit tests it calls
 * g_assert(), g_assert_cmpint() or g_assert_cmpuint() to validate all function
 * arguments. This will highlight any issues with the unit test code itself.
 */

#include "gsttestclock.h"

enum
{
  PROP_0,
  PROP_START_TIME,
};

typedef struct _GstClockEntryContext GstClockEntryContext;

struct _GstClockEntryContext
{
  GstClockEntry *clock_entry;
  GstClockTimeDiff time_diff;
};

struct _GstTestClockPrivate
{
  GstClockTime start_time;
  GstClockTime internal_time;
};

#define GST_TEST_CLOCK_GET_PRIVATE(obj) ((GST_TEST_CLOCK_CAST (obj))->priv)

GST_DEBUG_CATEGORY_STATIC (test_clock_debug);
#define GST_CAT_TEST_CLOCK test_clock_debug

#define _do_init \
G_STMT_START { \
  GST_DEBUG_CATEGORY_INIT (test_clock_debug, "GST_TEST_CLOCK", \
      GST_DEBUG_BOLD, "Test clocks for unit tests"); \
} G_STMT_END

G_DEFINE_TYPE_WITH_CODE (GstTestClock, gst_test_clock,
    GST_TYPE_CLOCK, _do_init);

static GstObjectClass *parent_class = NULL;

static void gst_test_clock_constructed (GObject * object);
static void gst_test_clock_dispose (GObject * object);
static void gst_test_clock_finalize (GObject * object);
static void gst_test_clock_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);
static void gst_test_clock_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec);

static GstClockTime gst_test_clock_get_resolution (GstClock * clock);
static GstClockTime gst_test_clock_get_internal_time (GstClock * clock);

static void
gst_test_clock_class_init (GstTestClockClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstClockClass *gstclock_class = GST_CLOCK_CLASS (klass);
  GParamSpec *pspec;

  parent_class = g_type_class_peek_parent (klass);

  g_type_class_add_private (klass, sizeof (GstTestClockPrivate));

  gobject_class->constructed = GST_DEBUG_FUNCPTR (gst_test_clock_constructed);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_test_clock_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_test_clock_finalize);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_test_clock_get_property);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_test_clock_set_property);

  gstclock_class->get_resolution = GST_DEBUG_FUNCPTR (
      gst_test_clock_get_resolution);
  gstclock_class->get_internal_time = GST_DEBUG_FUNCPTR (
      gst_test_clock_get_internal_time);

  /**
   * GstTestClock:start-time
   *
   * When a #GstTestClock is constructed it will have a certain start time set.
   * If the clock was created using gst_test_clock_new_with_start_time() then
   * this property contains the value of the @start_time argument. If
   * gst_test_clock_new() was called the clock started at time zero, and thus
   * this property contains the value 0.
   */
  pspec = g_param_spec_uint64 ("start-time", "Start Time",
      "Start Time of the Clock", 0, G_MAXUINT64, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (gobject_class, PROP_START_TIME, pspec);
}

static void
gst_test_clock_init (GstTestClock * test_clock)
{
  test_clock->priv = G_TYPE_INSTANCE_GET_PRIVATE (test_clock,
      GST_TYPE_TEST_CLOCK, GstTestClockPrivate);

  GST_OBJECT_FLAG_SET (test_clock,
      GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC |
      GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC |
      GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC |
      GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC);
}

static void
gst_test_clock_constructed (GObject * object)
{
  GstTestClock *test_clock = GST_TEST_CLOCK (object);
  GstTestClockPrivate *priv = GST_TEST_CLOCK_GET_PRIVATE (test_clock);

  priv->internal_time = priv->start_time;
}

static void
gst_test_clock_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_test_clock_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_test_clock_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstTestClock *test_clock = GST_TEST_CLOCK (object);
  GstTestClockPrivate *priv = GST_TEST_CLOCK_GET_PRIVATE (test_clock);

  switch (property_id) {
    case PROP_START_TIME:
      g_value_set_uint64 (value, priv->start_time);
      break;
    default:
      G_OBJECT_CLASS (parent_class)->set_property (object, property_id, value,
          pspec);
      break;
  }
}

static void
gst_test_clock_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTestClock *test_clock = GST_TEST_CLOCK (object);
  GstTestClockPrivate *priv = GST_TEST_CLOCK_GET_PRIVATE (test_clock);

  switch (property_id) {
    case PROP_START_TIME:
      priv->start_time = g_value_get_uint64 (value);
      GST_CAT_TRACE_OBJECT (GST_CAT_TEST_CLOCK, test_clock,
          "test clock start time initialized at %" GST_TIME_FORMAT,
          GST_TIME_ARGS (priv->start_time));
      break;
    default:
      G_OBJECT_CLASS (parent_class)->set_property (object, property_id, value,
          pspec);
      break;
  }
}

static GstClockTime
gst_test_clock_get_resolution (GstClock * clock)
{
  return 1;
}

static GstClockTime
gst_test_clock_get_internal_time (GstClock * clock)
{
  GstTestClock *test_clock = GST_TEST_CLOCK (clock);
  GstTestClockPrivate *priv = GST_TEST_CLOCK_GET_PRIVATE (test_clock);
  GstClockTime result;

  GST_OBJECT_LOCK (test_clock);

  GST_CAT_TRACE_OBJECT (GST_CAT_TEST_CLOCK, test_clock,
      "retrieving test clock time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (priv->internal_time));
  result = priv->internal_time;

  GST_OBJECT_UNLOCK (test_clock);

  return result;
}

/**
 * gst_test_clock_new:
 *
 * Creates a new test clock with its time set to zero.
 *
 * MT safe.
 *
 * Returns: (transfer full): a #GstTestClock cast to #GstClock.
 */
GstClock *
gst_test_clock_new (void)
{
  return gst_test_clock_new_with_start_time (0);
}

/**
 * gst_test_clock_new_with_start_time:
 * @start_time: a #GstClockTime set to the desired start time of the clock.
 *
 * Creates a new test clock with its time set to the specified time.
 *
 * MT safe.
 *
 * Returns: (transfer full): a #GstTestClock cast to #GstClock.
 */
GstClock *
gst_test_clock_new_with_start_time (GstClockTime start_time)
{
  g_assert_cmpuint (start_time, !=, GST_CLOCK_TIME_NONE);
  return g_object_new (GST_TYPE_TEST_CLOCK, "start-time", start_time, NULL);
}

/**
 * gst_test_clock_set_time:
 * @test_clock: a #GstTestClock of which to set the time
 * @new_time: a #GstClockTime later than that returned by gst_clock_get_time()
 *
 * Sets the time of @test_clock to the time given by @new_time. The time of
 * @test_clock is monotonically increasing, therefore providing a @new_time
 * which is earlier or equal to the time of the clock as given by
 * gst_clock_get_time() is a programming error.
 *
 * MT safe.
 */
void
gst_test_clock_set_time (GstTestClock * test_clock, GstClockTime new_time)
{
  GstTestClockPrivate *priv = GST_TEST_CLOCK_GET_PRIVATE (test_clock);

  g_assert (GST_IS_TEST_CLOCK (test_clock));
  g_assert_cmpuint (new_time, !=, GST_CLOCK_TIME_NONE);

  GST_OBJECT_LOCK (test_clock);

  g_assert_cmpuint (new_time, >=, priv->internal_time);

  priv->internal_time = new_time;
  GST_CAT_DEBUG_OBJECT (GST_CAT_TEST_CLOCK, test_clock,
      "clock set to %" GST_TIME_FORMAT, GST_TIME_ARGS (new_time));

  GST_OBJECT_UNLOCK (test_clock);
}

/**
 * gst_test_clock_advance_time:
 * @test_clock: a #GstTestClock for which to increase the time
 * @delta: a positive #GstClockTimeDiff to be added to the time of the clock
 *
 * Advances the time of the @test_clock by the amount given by @delta. The
 * time of @test_clock is monotonically increasing, therefore providing a
 * @delta which is negative or zero is a programming error.
 *
 * MT safe.
 */
void
gst_test_clock_advance_time (GstTestClock * test_clock,
    GstClockTimeDiff delta)
{
  GstTestClockPrivate *priv = GST_TEST_CLOCK_GET_PRIVATE (test_clock);

  g_assert (GST_IS_TEST_CLOCK (test_clock));
  g_assert_cmpint (delta, >=, 0);
  g_assert_cmpuint (delta, <, G_MAXUINT64 - delta);

  GST_OBJECT_LOCK (test_clock);

  GST_CAT_DEBUG_OBJECT (GST_CAT_TEST_CLOCK, test_clock,
      "advancing clock by %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (delta), GST_TIME_ARGS (priv->internal_time + delta));
  priv->internal_time += delta;

  GST_OBJECT_UNLOCK (test_clock);
}
