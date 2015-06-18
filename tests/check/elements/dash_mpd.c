/* GStreamer unit test for MPEG-DASH
 *
 * Copyright (c) <2015> YouView TV Ltd
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

#include "../../ext/dash/gstmpdparser.c"
#undef GST_CAT_DEFAULT

#include <gst/check/gstcheck.h>

GST_DEBUG_CATEGORY (gst_dash_demux_debug);

/*
 * compute the number of milliseconds contained in a duration value specified by
 * year, month, day, hour, minute, second, millisecond
 *
 * This function must use the same conversion algorithm implemented in
 * gst_mpdparser_get_xml_prop_duration from gstmpdparser.c file.
 */
static gint64
duration_to_ms (guint year, guint month, guint day, guint hour, guint minute,
    guint second, guint millisecond)
{
  gint64 days = (gint64) year * 365 + month * 30 + day;
  gint64 hours = days * 24 + hour;
  gint64 minutes = hours * 60 + minute;
  gint64 seconds = minutes * 60 + second;
  gint64 ms = seconds * 1000 + millisecond;
  return ms;
}

/*
 * Test to ensure a simple mpd file successfully parses.
 *
 */
GST_START_TEST (dash_mpdparser_validsimplempd)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"> </MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);
  /* check that unset elements with default values are properly configured */
  assert_equals_int (mpdclient->mpd_node->type, GST_MPD_FILE_TYPE_STATIC);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the MPD attributes.
 *
 */
GST_START_TEST (dash_mpdparser_mpd)
{
  GstDateTime *availabilityStartTime;
  GstDateTime *availabilityEndTime;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      " id=\"testId\""
      " type=\"static\""
      " availabilityStartTime=\"2015-03-24T1:10:50\""
      " availabilityEndTime=\"2015-03-24T1:10:50\""
      " mediaPresentationDuration=\"P0Y1M2DT12H10M20.5S\""
      " minimumUpdatePeriod=\"P0Y1M2DT12H10M20.5S\""
      " minBufferTime=\"P0Y1M2DT12H10M20.5S\""
      " timeShiftBufferDepth=\"P0Y1M2DT12H10M20.5S\""
      " suggestedPresentationDelay=\"P0Y1M2DT12H10M20.5S\""
      " maxSegmentDuration=\"P0Y1M2DT12H10M20.5S\""
      " maxSubsegmentDuration=\"P0Y1M2DT12H10M20.5S\"></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);

  assert_equals_string (mpdclient->mpd_node->id, "testId");

  assert_equals_int (mpdclient->mpd_node->type, GST_MPD_FILE_TYPE_STATIC);

  availabilityStartTime = mpdclient->mpd_node->availabilityStartTime;
  assert_equals_int (gst_date_time_get_year (availabilityStartTime), 2015);
  assert_equals_int (gst_date_time_get_month (availabilityStartTime), 3);
  assert_equals_int (gst_date_time_get_day (availabilityStartTime), 24);
  assert_equals_int (gst_date_time_get_hour (availabilityStartTime), 1);
  assert_equals_int (gst_date_time_get_minute (availabilityStartTime), 10);
  assert_equals_int (gst_date_time_get_second (availabilityStartTime), 50);

  availabilityEndTime = mpdclient->mpd_node->availabilityEndTime;
  assert_equals_int (gst_date_time_get_year (availabilityEndTime), 2015);
  assert_equals_int (gst_date_time_get_month (availabilityEndTime), 3);
  assert_equals_int (gst_date_time_get_day (availabilityEndTime), 24);
  assert_equals_int (gst_date_time_get_hour (availabilityEndTime), 1);
  assert_equals_int (gst_date_time_get_minute (availabilityEndTime), 10);
  assert_equals_int (gst_date_time_get_second (availabilityEndTime), 50);

  assert_equals_int64 (mpdclient->mpd_node->mediaPresentationDuration,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->minimumUpdatePeriod,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->minBufferTime,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->timeShiftBufferDepth,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->suggestedPresentationDelay,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->maxSegmentDuration,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->maxSubsegmentDuration,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the ProgramInformation attributes
 *
 */
GST_START_TEST (dash_mpdparser_program_information)
{
  GstProgramInformationNode *program;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<ProgramInformation lang=\"en\""
      " moreInformationURL=\"TestMoreInformationUrl\">"
      "<Title>TestTitle</Title>"
      "<Source>TestSource</Source>"
      "<Copyright>TestCopyright</Copyright> </ProgramInformation> </MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);
  program =
      (GstProgramInformationNode *) mpdclient->mpd_node->ProgramInfo->data;
  assert_equals_string (program->lang, "en");
  assert_equals_string (program->moreInformationURL, "TestMoreInformationUrl");
  assert_equals_string (program->Title, "TestTitle");
  assert_equals_string (program->Source, "TestSource");
  assert_equals_string (program->Copyright, "TestCopyright");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the BaseURL attributes
 *
 */
GST_START_TEST (dash_mpdparser_base_URL)
{
  GstBaseURL *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<BaseURL serviceLocation=\"TestServiceLocation\""
      " byteRange=\"TestByteRange\">TestBaseURL</BaseURL></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);
  baseURL = (GstBaseURL *) mpdclient->mpd_node->BaseURLs->data;
  assert_equals_string (baseURL->baseURL, "TestBaseURL");
  assert_equals_string (baseURL->serviceLocation, "TestServiceLocation");
  assert_equals_string (baseURL->byteRange, "TestByteRange");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the location attributes
 *
 */
GST_START_TEST (dash_mpdparser_location)
{
  const gchar *location;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Location>TestLocation</Location></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);
  location = (gchar *) mpdclient->mpd_node->Locations->data;
  assert_equals_string (location, "TestLocation");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing metrics attributes
 *
 */
GST_START_TEST (dash_mpdparser_metrics)
{
  GstMetricsNode *metricsNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Metrics metrics=\"TestMetric\"></Metrics></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);
  metricsNode = (GstMetricsNode *) mpdclient->mpd_node->Metrics->data;
  assert_equals_string (metricsNode->metrics, "TestMetric");
  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing metrics range attributes
 *
 */
GST_START_TEST (dash_mpdparser_metrics_range)
{
  GstMetricsNode *metricsNode;
  GstMetricsRangeNode *metricsRangeNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Metrics><Range starttime=\"P0Y1M2DT12H10M20.5S\""
      " duration=\"P0Y1M2DT12H10M20.1234567S\"></Range></Metrics></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);
  metricsNode = (GstMetricsNode *) mpdclient->mpd_node->Metrics->data;
  assert_equals_pointer (metricsNode->metrics, NULL);
  metricsRangeNode = (GstMetricsRangeNode *) metricsNode->MetricsRanges->data;
  assert_equals_int64 (metricsRangeNode->starttime,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));
  assert_equals_int64 (metricsRangeNode->duration,
      duration_to_ms (0, 1, 2, 12, 10, 20, 123));

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing metrics reporting attributes
 *
 */
GST_START_TEST (dash_mpdparser_metrics_reporting)
{
  GstMetricsNode *metricsNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Metrics><Reporting></Reporting></Metrics></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);
  metricsNode = (GstMetricsNode *) mpdclient->mpd_node->Metrics->data;
  assert_equals_pointer (metricsNode->metrics, NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the type property: value "dynamic"
 *
 */
GST_START_TEST (dash_mpdparser_type_dynamic)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD type=\"dynamic\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"> </MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);
  assert_equals_int (mpdclient->mpd_node->type, GST_MPD_FILE_TYPE_DYNAMIC);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling Representation selection
 *
 */
GST_START_TEST (dash_mpdparser_representation_selection)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adaptationSetNode;
  GList *representations;
  gint represendationIndex;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "<AdaptationSet id=\"1\" mimeType=\"video/mp4\">"
      "<Representation id=\"v0\" bandwidth=\"500000\"></Representation>"
      "<Representation id=\"v1\" bandwidth=\"250000\"></Representation>"
      "</AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret = gst_mpd_client_setup_media_presentation (mpdclient);
  assert_equals_int (ret, TRUE);

  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  adaptationSetNode = adaptationSets->data;
  fail_if (adaptationSetNode == NULL);
  assert_equals_int (adaptationSetNode->id, 1);

  representations = adaptationSetNode->Representations;
  fail_if (representations == NULL);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_min_bandwidth (representations);
  assert_equals_int (represendationIndex, 1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 0);
  assert_equals_int (represendationIndex, 1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 100000);
  assert_equals_int (represendationIndex, -1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 300000);
  assert_equals_int (represendationIndex, 1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 500000);
  assert_equals_int (represendationIndex, 0);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing empty xml string
 *
 */
GST_START_TEST (dash_mpdparser_missing_xml)
{
  const gchar *xml = "";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing an xml with no mpd tag
 *
 */
GST_START_TEST (dash_mpdparser_missing_mpd)
{
  const gchar *xml = "<?xml version=\"1.0\"?>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing an MPD with a wrong end tag
 */
GST_START_TEST (dash_mpdparser_no_end_tag)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"> </NPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing an MPD with no default namespace
 */
GST_START_TEST (dash_mpdparser_no_default_namespace)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * create a test suite containing all dash testcases
 */
static Suite *
dash_suite (void)
{
  Suite *s = suite_create ("dash");
  TCase *tc_simpleMPD = tcase_create ("simpleMPD");
  TCase *tc_complexMPD = tcase_create ("complexMPD");
  TCase *tc_negativeTests = tcase_create ("negativeTests");

  GST_DEBUG_CATEGORY_INIT (gst_dash_demux_debug, "gst_dash_demux_debug", 0,
      "mpeg dash tests");

  /* test parsing the simplest possible mpd */
  tcase_add_test (tc_simpleMPD, dash_mpdparser_validsimplempd);

  /* tests parsing attributes from each element type */
  tcase_add_test (tc_simpleMPD, dash_mpdparser_mpd);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_program_information);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_base_URL);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_location);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_metrics);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_metrics_range);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_metrics_reporting);

  /* tests checking other possible values for attributes */
  tcase_add_test (tc_simpleMPD, dash_mpdparser_type_dynamic);

  tcase_add_test (tc_complexMPD, dash_mpdparser_representation_selection);
  /* tests checking the parsing of missing/incomplete attributes of xml */
  tcase_add_test (tc_negativeTests, dash_mpdparser_missing_xml);
  tcase_add_test (tc_negativeTests, dash_mpdparser_missing_mpd);
  tcase_add_test (tc_negativeTests, dash_mpdparser_no_end_tag);
  tcase_add_test (tc_negativeTests, dash_mpdparser_no_default_namespace);

  suite_add_tcase (s, tc_simpleMPD);
  suite_add_tcase (s, tc_complexMPD);
  suite_add_tcase (s, tc_negativeTests);

  return s;
}

GST_CHECK_MAIN (dash);
