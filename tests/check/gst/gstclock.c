/* GStreamer
 * Copyright (C) 2010 Alessandro Decina <alessandro.decina@collabora.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* Include the non-public linear regression function */
#include "../../gst/gstclock-linreg.c"
#include <gst/check/gstcheck.h>

typedef struct
{
  GstClock parent;
} TestClock;

typedef struct
{
  GstClockClass parent_class;
} TestClockClass;

#define TYPE_TEST_CLOCK                   (test_clock_get_type ())
#define TEST_CLOCK(obj)                   (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_TEST_CLOCK, TestClock))
#define TEST_CLOCK_CAST(obj)              ((TestClock *)(obj))
#define IS_TEST_CLOCK(obj)                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_TEST_CLOCK))
#define TEST_CLOCK_CLASS(klass)           (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_TEST_CLOCK, TestClockClass))
#define IS_TEST_CLOCK_CLASS(klass)        (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_TEST_CLOCK))
#define TEST_CLOCK_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_TEST_CLOCK, TestClockClass))


GType test_clock_get_type (void);
G_DEFINE_TYPE (TestClock, test_clock, GST_TYPE_CLOCK);

static GstClockReturn
fake_wait_async (GstClock * clock, GstClockEntry * entry)
{
  return GST_CLOCK_OK;
}

static void
test_clock_class_init (TestClockClass * klass)
{
  GstClockClass *clock_class;

  clock_class = GST_CLOCK_CLASS (klass);

  clock_class->wait_async = fake_wait_async;
}

static void
test_clock_init (TestClock * clock)
{
}


static void
weak_notify (gpointer data, GObject * object)
{
  *(gboolean *) data = FALSE;
}

GST_START_TEST (test_set_master_refcount)
{
  GstClock *master, *slave;
  gboolean master_alive = TRUE;

  /* create master and slave */
  master = g_object_new (TYPE_TEST_CLOCK, "name", "TestClockMaster", NULL);
  slave = g_object_new (TYPE_TEST_CLOCK, "name", "TestClockSlave", NULL);
  GST_OBJECT_FLAG_SET (slave, GST_CLOCK_FLAG_CAN_SET_MASTER);

  fail_unless_equals_int (GST_OBJECT_REFCOUNT (master), 1);
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (slave), 1);

  g_object_weak_ref (G_OBJECT (master), weak_notify, &master_alive);
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (master), 1);

  gst_clock_set_master (slave, master);
  /* slave stores master in slave->master */
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (master), 2);
  /* master stores a ref to slave in master->clockid */
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (slave), 2);

  /* discard our ref */
  gst_object_unref (master);

  /* master should still be reffed inside slave */
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (master), 1);
  fail_unless (master_alive);

  /* drop the last ref to mater */
  gst_clock_set_master (slave, NULL);

  fail_if (master_alive);
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (slave), 1);

  gst_object_unref (slave);
}

GST_END_TEST;

GstClockTime times1[] = {
  257116899087539, 0, 120632754291904, 0,
  257117935914250, 0, 120633825367344, 0,
  257119448289434, 0, 120635306141271, 0,
  257120493671524, 0, 120636384357825, 0,
  257121550784861, 0, 120637417438878, 0,
  257123042669403, 0, 120638895344150, 0,
  257124089184865, 0, 120639971729651, 0,
  257125545836474, 0, 120641406788243, 0,
  257127030618490, 0, 120642885914220, 0,
  257128033712770, 0, 120643888843907, 0,
  257129081768074, 0, 120644981892002, 0,
  257130145383845, 0, 120646016376867, 0,
  257131532530200, 0, 120647389850987, 0,
  257132578136034, 0, 120648472767247, 0,
  257134102475722, 0, 120649953785315, 0,
  257135142994788, 0, 120651028858556, 0,
  257136585079868, 0, 120652441303624, 0,
  257137618260656, 0, 120653491627112, 0,
  257139108694546, 0, 120654963978184, 0,
  257140644022048, 0, 120656500233068, 0,
  257141685671825, 0, 120657578510655, 0,
  257142741238288, 0, 120658610889805, 0,
  257144243633074, 0, 120660093098060, 0,
  257145287962271, 0, 120661172901525, 0,
  257146740596716, 0, 120662591572179, 0,
  257147757607150, 0, 120663622822179, 0,
  257149263992401, 0, 120665135578527, 0,
  257150303719290, 0, 120666176166905, 0,
  257151355569906, 0, 120667217304601, 0,
  257152430578406, 0, 120668326099768, 0,
  257153490501095, 0, 120669360554111, 0,
  257154512360784, 0, 120670365497960, 0,
  257155530610577, 0, 120671399006259, 0,
  257156562091659, 0, 120672432728185, 0,
  257157945388742, 0, 120673800312414, 0,
  257159287547073, 0, 120675142444983, 0,
  257160324912880, 0, 120676215076817, 0,
  257345408328042, 0, 120861261738196, 0,
  257346412270919, 0, 120862265613926, 0,
  257347420532284, 0, 120863278644933, 0,
  257348431187638, 0, 120864284412754, 0,
  257349439018028, 0, 120865293110265, 0,
  257351796217938, 0, 120867651111973, 0,
  257352803038092, 0, 120868659107578, 0,
  257354152688899, 0, 120870008594883, 0,
  257355157088906, 0, 120871011097327, 0,
  257356162439182, 0, 120872016346348, 0,
  257357167872040, 0, 120873021656407, 0,
  257358182440058, 0, 120874048633945, 0,
  257359198881356, 0, 120875052265538, 0,
  257100756525466, 0, 120616619282139, 0,
  257101789337770, 0, 120617655475988, 0,
  257102816323472, 0, 120618674000157, 0,
  257103822485250, 0, 120619679005039, 0,
  257104840760423, 0, 120620710743321, 0,
  257105859459496, 0, 120621715351476, 0,
  257106886662470, 0, 120622764942539, 0,
  257108387497864, 0, 120624244221106, 0,
  257109428859191, 0, 120625321461096, 0,
  257110485892785, 0, 120626356892003, 0,
  257111869872141, 0, 120627726459874, 0,
  257112915903774, 0, 120628813190830, 0,
  257114329982208, 0, 120630187061682, 0,
  257115376666026, 0, 120631271992101, 0
};


GstClockTime times2[] = {
  291678579009762, 0, 162107345029507, 0,
  291679770464405, 0, 162108597684538, 0,
  291680972924370, 0, 162109745816863, 0,
  291682278949629, 0, 162111000577605, 0,
  291683590706117, 0, 162112357724822, 0,
  291684792322541, 0, 162113613156950, 0,
  291685931362506, 0, 162114760556854, 0,
  291687132156589, 0, 162115909238493, 0,
  291688265012060, 0, 162117120603240, 0,
  291689372183047, 0, 162118126279508, 0,
  291705506022294, 0, 162134329373992, 0,
  291667914301004, 0, 162096795553658, 0,
  291668119537668, 0, 162096949051905, 0,
  291668274671455, 0, 162097049238371, 0,
  291668429435600, 0, 162097256356719, 0,
  291668586128535, 0, 162097355689763, 0,
  291668741306233, 0, 162097565678460, 0,
  291668893789203, 0, 162097661044916, 0,
  291669100256555, 0, 162097865694145, 0,
  291669216417563, 0, 162098069214693, 0,
  291669836394620, 0, 162098677275530, 0,
  291669990447821, 0, 162098792601263, 0,
  291670149426086, 0, 162098916899184, 0,
  291670300232152, 0, 162099114225621, 0,
  291670411261917, 0, 162099236784112, 0,
  291670598483507, 0, 162099402158751, 0,
  291671716582687, 0, 162100558744122, 0,
  291672600759788, 0, 162101499326359, 0,
  291673919988307, 0, 162102751981384, 0,
  291675174441643, 0, 162104005551939, 0,
  291676271562197, 0, 162105105252898, 0,
  291677376345374, 0, 162106195737516, 0
};

GstClockTime times3[] = {
  291881924291688, 0, 162223997578228, 0,
  291883318122262, 0, 162224167198360, 0,
  291884786394838, 0, 162224335172501, 0,
  291886004374386, 0, 162224503695531, 0,
  291887224353285, 0, 162224673560021, 0,
  291888472403367, 0, 162224843760361, 0,
  291889727977561, 0, 162225014479362, 0,
  291890989982306, 0, 162225174554558, 0,
  291892247875763, 0, 162225339753039, 0,
  291893502163547, 0, 162225673230987, 0,
  291894711382216, 0, 162225829494101, 0,
  291895961021506, 0, 162225964530832, 0,
  291897251690854, 0, 162226127287981, 0,
  291898508630785, 0, 162226303710406, 0,
  291899740172868, 0, 162226472478047, 0,
  291900998878873, 0, 162226637402085, 0,
  291902334919875, 0, 162226797873245, 0,
  291903572196610, 0, 162226964352963, 0,
  291904727342699, 0, 162227125312525, 0,
  291906071189108, 0, 162228361337153, 0,
  291907308146005, 0, 162229560625638, 0,
  291908351925126, 0, 162230604986650, 0,
  291909396411423, 0, 162231653690543, 0,
  291910453965348, 0, 162232698550995, 0,
  291912096870744, 0, 162233475264947, 0,
  291913234148395, 0, 162233606516855, 0,
  291915448096576, 0, 162233921145559, 0,
  291916707748827, 0, 162234047154298, 0,
  291918737451070, 0, 162234370837425, 0,
  291919896016205, 0, 162234705504337, 0,
  291921098663980, 0, 162234872320397, 0,
  291922315691409, 0, 162235031023366, 0
};

struct test_entry
{
  gint n;
  GstClockTime *v;
  GstClockTime expect_internal;
  GstClockTime expect_external;
  guint64 expect_num;
  guint64 expect_denom;
} times[] = {
  {
  32, times1, 257154512360784, 120670380469753, 4052622913376634109,
        4052799313904261962}, {
  64, times1, 257359198881356, 120875054227405, 2011895759027682422,
        2012014931360215503}, {
  32, times2, 291705506022294, 162134297192792, 2319535707505209857,
        2321009753483354451}, {
  32, times3, 291922315691409, 162234934150296, 1370930728180888261,
        4392719527011673456}
};

GST_START_TEST (test_regression)
{
  GstClockTime m_num, m_den, internal, external;
  gdouble r_squared, rate, expect_rate;
  gint i;

  for (i = 0; i < G_N_ELEMENTS (times); i++) {
    fail_unless (_priv_gst_do_linear_regression (times[i].v, times[i].n,
            &m_num, &m_den, &external, &internal, &r_squared));

    GST_LOG ("xbase %" G_GUINT64_FORMAT " ybase %" G_GUINT64_FORMAT " rate = %"
        G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT " = %.10f r_squared %f\n",
        internal, external, m_num, m_den, (gdouble) (m_num) / (m_den),
        r_squared);

    /* Require high correlation */
    fail_unless (r_squared >= 0.9);

    fail_unless (internal == times[i].expect_internal,
        "Regression params %d fail. internal %" G_GUINT64_FORMAT
        " != expected %" G_GUINT64_FORMAT, i, internal,
        times[i].expect_internal);
    /* Rate must be within 1% tolerance */
    expect_rate = ((gdouble) (times[i].expect_num) / times[i].expect_denom);
    rate = ((gdouble) (m_num) / m_den);
    fail_unless ((expect_rate - rate) >= -0.1 && (expect_rate - rate) <= 0.1,
        "Regression params %d fail. Rate out of range. Expected %f, got %f",
        i, expect_rate, rate);
    fail_unless (external >= times[i].expect_external * 0.99 &&
        external <= times[i].expect_external * 1.01,
        "Regression params %d fail. external %" G_GUINT64_FORMAT
        " != expected %" G_GUINT64_FORMAT, i, external,
        times[i].expect_external);
  }
}

GST_END_TEST;

static Suite *
gst_clock_suite (void)
{
  Suite *s = suite_create ("GstClock");
  TCase *tc_chain = tcase_create ("clock");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_set_master_refcount);
  tcase_add_test (tc_chain, test_regression);

  return s;
}

GST_CHECK_MAIN (gst_clock);
