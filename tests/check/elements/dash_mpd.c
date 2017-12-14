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
static guint64
duration_to_ms (guint year, guint month, guint day, guint hour, guint minute,
    guint second, guint millisecond)
{
  guint64 days = (guint64) year * 365 + (guint64) month * 30 + day;
  guint64 hours = days * 24 + hour;
  guint64 minutes = hours * 60 + minute;
  guint64 seconds = minutes * 60 + second;
  guint64 ms = seconds * 1000 + millisecond;
  return ms;
}

static GstClockTime
duration_to_clocktime (guint year, guint month, guint day, guint hour,
    guint minute, guint second, guint millisecond)
{
  return (GST_MSECOND * duration_to_ms (year, month, day, hour, minute, second,
          millisecond));
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
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"> </MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
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
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     schemaLocation=\"TestSchemaLocation\""
      "     xmlns:xsi=\"TestNamespaceXSI\""
      "     xmlns:ext=\"TestNamespaceEXT\""
      "     id=\"testId\""
      "     type=\"static\""
      "     availabilityStartTime=\"2015-03-24T1:10:50\""
      "     availabilityEndTime=\"2015-03-24T1:10:50.123456\""
      "     mediaPresentationDuration=\"P0Y1M2DT12H10M20.5S\""
      "     minimumUpdatePeriod=\"P0Y1M2DT12H10M20.5S\""
      "     minBufferTime=\"P0Y1M2DT12H10M20.5S\""
      "     timeShiftBufferDepth=\"P0Y1M2DT12H10M20.5S\""
      "     suggestedPresentationDelay=\"P0Y1M2DT12H10M20.5S\""
      "     maxSegmentDuration=\"P0Y1M2DT12H10M20.5S\""
      "     maxSubsegmentDuration=\"P0Y1M2DT12H10M20.5S\"></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  assert_equals_string (mpdclient->mpd_node->default_namespace,
      "urn:mpeg:dash:schema:mpd:2011");
  assert_equals_string (mpdclient->mpd_node->namespace_xsi, "TestNamespaceXSI");
  assert_equals_string (mpdclient->mpd_node->namespace_ext, "TestNamespaceEXT");
  assert_equals_string (mpdclient->mpd_node->schemaLocation,
      "TestSchemaLocation");
  assert_equals_string (mpdclient->mpd_node->id, "testId");

  assert_equals_int (mpdclient->mpd_node->type, GST_MPD_FILE_TYPE_STATIC);

  availabilityStartTime = mpdclient->mpd_node->availabilityStartTime;
  assert_equals_int (gst_date_time_get_year (availabilityStartTime), 2015);
  assert_equals_int (gst_date_time_get_month (availabilityStartTime), 3);
  assert_equals_int (gst_date_time_get_day (availabilityStartTime), 24);
  assert_equals_int (gst_date_time_get_hour (availabilityStartTime), 1);
  assert_equals_int (gst_date_time_get_minute (availabilityStartTime), 10);
  assert_equals_int (gst_date_time_get_second (availabilityStartTime), 50);
  assert_equals_int (gst_date_time_get_microsecond (availabilityStartTime), 0);

  availabilityEndTime = mpdclient->mpd_node->availabilityEndTime;
  assert_equals_int (gst_date_time_get_year (availabilityEndTime), 2015);
  assert_equals_int (gst_date_time_get_month (availabilityEndTime), 3);
  assert_equals_int (gst_date_time_get_day (availabilityEndTime), 24);
  assert_equals_int (gst_date_time_get_hour (availabilityEndTime), 1);
  assert_equals_int (gst_date_time_get_minute (availabilityEndTime), 10);
  assert_equals_int (gst_date_time_get_second (availabilityEndTime), 50);
  assert_equals_int (gst_date_time_get_microsecond (availabilityEndTime),
      123456);

  assert_equals_uint64 (mpdclient->mpd_node->mediaPresentationDuration,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_uint64 (mpdclient->mpd_node->minimumUpdatePeriod,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_uint64 (mpdclient->mpd_node->minBufferTime,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_uint64 (mpdclient->mpd_node->timeShiftBufferDepth,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_uint64 (mpdclient->mpd_node->suggestedPresentationDelay,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_uint64 (mpdclient->mpd_node->maxSegmentDuration,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_uint64 (mpdclient->mpd_node->maxSubsegmentDuration,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the ProgramInformation attributes
 *
 */
GST_START_TEST (dash_mpdparser_programInformation)
{
  GstProgramInformationNode *program;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <ProgramInformation lang=\"en\""
      "                      moreInformationURL=\"TestMoreInformationUrl\">"
      "    <Title>TestTitle</Title>"
      "    <Source>TestSource</Source>"
      "    <Copyright>TestCopyright</Copyright>"
      "  </ProgramInformation> </MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
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
GST_START_TEST (dash_mpdparser_baseURL)
{
  GstBaseURL *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <BaseURL serviceLocation=\"TestServiceLocation\""
      "     byteRange=\"TestByteRange\">TestBaseURL</BaseURL></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  baseURL = (GstBaseURL *) mpdclient->mpd_node->BaseURLs->data;
  assert_equals_string (baseURL->baseURL, "TestBaseURL");
  assert_equals_string (baseURL->serviceLocation, "TestServiceLocation");
  assert_equals_string (baseURL->byteRange, "TestByteRange");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the Location attributes
 *
 */
GST_START_TEST (dash_mpdparser_location)
{
  const gchar *location;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Location>TestLocation</Location></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  location = (gchar *) mpdclient->mpd_node->Locations->data;
  assert_equals_string (location, "TestLocation");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Metrics attributes
 *
 */
GST_START_TEST (dash_mpdparser_metrics)
{
  GstMetricsNode *metricsNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Metrics metrics=\"TestMetric\"></Metrics></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  metricsNode = (GstMetricsNode *) mpdclient->mpd_node->Metrics->data;
  assert_equals_string (metricsNode->metrics, "TestMetric");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Metrics Range attributes
 *
 */
GST_START_TEST (dash_mpdparser_metrics_range)
{
  GstMetricsNode *metricsNode;
  GstMetricsRangeNode *metricsRangeNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Metrics>"
      "    <Range starttime=\"P0Y1M2DT12H10M20.5S\""
      "           duration=\"P0Y1M2DT12H10M20.1234567S\">"
      "    </Range></Metrics></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  metricsNode = (GstMetricsNode *) mpdclient->mpd_node->Metrics->data;
  assert_equals_pointer (metricsNode->metrics, NULL);
  metricsRangeNode = (GstMetricsRangeNode *) metricsNode->MetricsRanges->data;
  assert_equals_uint64 (metricsRangeNode->starttime,
      duration_to_ms (0, 1, 2, 12, 10, 20, 500));
  assert_equals_uint64 (metricsRangeNode->duration,
      duration_to_ms (0, 1, 2, 12, 10, 20, 123));

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Metrics Reporting attributes
 *
 */
GST_START_TEST (dash_mpdparser_metrics_reporting)
{
  GstMetricsNode *metricsNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Metrics><Reporting></Reporting></Metrics></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  metricsNode = (GstMetricsNode *) mpdclient->mpd_node->Metrics->data;
  assert_equals_pointer (metricsNode->metrics, NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period attributes
 *
 */
GST_START_TEST (dash_mpdparser_period)
{
  GstPeriodNode *periodNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"TestId\""
      "          start=\"P0Y1M2DT12H10M20.1234567S\""
      "          duration=\"P0Y1M2DT12H10M20.7654321S\""
      "          bitstreamSwitching=\"true\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  assert_equals_string (periodNode->id, "TestId");
  assert_equals_uint64 (periodNode->start,
      duration_to_ms (0, 1, 2, 12, 10, 20, 123));
  assert_equals_uint64 (periodNode->duration,
      duration_to_ms (0, 1, 2, 12, 10, 20, 765));
  assert_equals_int (periodNode->bitstreamSwitching, 1);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period baseURL attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_baseURL)
{
  GstPeriodNode *periodNode;
  GstBaseURL *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <BaseURL serviceLocation=\"TestServiceLocation\""
      "             byteRange=\"TestByteRange\">TestBaseURL</BaseURL>"
      "  </Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  baseURL = (GstBaseURL *) periodNode->BaseURLs->data;
  assert_equals_string (baseURL->baseURL, "TestBaseURL");
  assert_equals_string (baseURL->serviceLocation, "TestServiceLocation");
  assert_equals_string (baseURL->byteRange, "TestByteRange");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentBase attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentBase)
{
  GstPeriodNode *periodNode;
  GstSegmentBaseType *segmentBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentBase timescale=\"123456\""
      "                 presentationTimeOffset=\"123456789\""
      "                 indexRange=\"100-200\""
      "                 indexRangeExact=\"true\">"
      "    </SegmentBase></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentBase = periodNode->SegmentBase;
  assert_equals_uint64 (segmentBase->timescale, 123456);
  assert_equals_uint64 (segmentBase->presentationTimeOffset, 123456789);
  assert_equals_uint64 (segmentBase->indexRange->first_byte_pos, 100);
  assert_equals_uint64 (segmentBase->indexRange->last_byte_pos, 200);
  assert_equals_int (segmentBase->indexRangeExact, 1);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentBase Initialization attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentBase_initialization)
{
  GstPeriodNode *periodNode;
  GstSegmentBaseType *segmentBase;
  GstURLType *initialization;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentBase>"
      "      <Initialisation sourceURL=\"TestSourceURL\""
      "                      range=\"100-200\">"
      "      </Initialisation></SegmentBase></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentBase = periodNode->SegmentBase;
  initialization = segmentBase->Initialization;
  assert_equals_string (initialization->sourceURL, "TestSourceURL");
  assert_equals_uint64 (initialization->range->first_byte_pos, 100);
  assert_equals_uint64 (initialization->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentBase RepresentationIndex attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentBase_representationIndex)
{
  GstPeriodNode *periodNode;
  GstSegmentBaseType *segmentBase;
  GstURLType *representationIndex;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentBase>"
      "      <RepresentationIndex sourceURL=\"TestSourceURL\""
      "                           range=\"100-200\">"
      "      </RepresentationIndex></SegmentBase></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentBase = periodNode->SegmentBase;
  representationIndex = segmentBase->RepresentationIndex;
  assert_equals_string (representationIndex->sourceURL, "TestSourceURL");
  assert_equals_uint64 (representationIndex->range->first_byte_pos, 100);
  assert_equals_uint64 (representationIndex->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentList)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period><SegmentList duration=\"1\"></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  fail_if (segmentList == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList MultipleSegmentBaseType attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentList_multipleSegmentBaseType)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstMultSegmentBaseType *multSegBaseType;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentList duration=\"10\""
      "                 startNumber=\"11\">"
      "    </SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  multSegBaseType = segmentList->MultSegBaseType;
  assert_equals_uint64 (multSegBaseType->duration, 10);
  assert_equals_uint64 (multSegBaseType->startNumber, 11);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList MultipleSegmentBaseType SegmentBaseType
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentBaseType)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentBaseType *segBaseType;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentList timescale=\"10\""
      "                 duration=\"1\""
      "                 presentationTimeOffset=\"11\""
      "                 indexRange=\"20-21\""
      "                 indexRangeExact=\"false\">"
      "    </SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  multSegBaseType = segmentList->MultSegBaseType;
  segBaseType = multSegBaseType->SegBaseType;
  assert_equals_uint64 (segBaseType->timescale, 10);
  assert_equals_uint64 (segBaseType->presentationTimeOffset, 11);
  assert_equals_uint64 (segBaseType->indexRange->first_byte_pos, 20);
  assert_equals_uint64 (segBaseType->indexRange->last_byte_pos, 21);
  assert_equals_int (segBaseType->indexRangeExact, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList MultipleSegmentBaseType SegmentTimeline
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentTimeline)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentTimelineNode *segmentTimeline;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentList>"
      "      <SegmentTimeline>"
      "      </SegmentTimeline></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  multSegBaseType = segmentList->MultSegBaseType;
  segmentTimeline = multSegBaseType->SegmentTimeline;
  fail_if (segmentTimeline == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList MultipleSegmentBaseType SegmentTimeline S
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentTimeline_s)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentTimelineNode *segmentTimeline;
  GstSNode *sNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentList>"
      "      <SegmentTimeline>"
      "        <S t=\"1\" d=\"2\" r=\"3\">"
      "        </S></SegmentTimeline></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  multSegBaseType = segmentList->MultSegBaseType;
  segmentTimeline = multSegBaseType->SegmentTimeline;
  sNode = (GstSNode *) g_queue_peek_head (&segmentTimeline->S);
  assert_equals_uint64 (sNode->t, 1);
  assert_equals_uint64 (sNode->d, 2);
  assert_equals_uint64 (sNode->r, 3);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList MultipleSegmentBaseType BitstreamSwitching
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentList_multipleSegmentBaseType_bitstreamSwitching)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstMultSegmentBaseType *multSegBaseType;
  GstURLType *bitstreamSwitching;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentList duration=\"0\">"
      "      <BitstreamSwitching sourceURL=\"TestSourceURL\""
      "                          range=\"100-200\">"
      "      </BitstreamSwitching></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  multSegBaseType = segmentList->MultSegBaseType;
  bitstreamSwitching = multSegBaseType->BitstreamSwitching;
  assert_equals_string (bitstreamSwitching->sourceURL, "TestSourceURL");
  assert_equals_uint64 (bitstreamSwitching->range->first_byte_pos, 100);
  assert_equals_uint64 (bitstreamSwitching->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList SegmentURL attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentList_segmentURL)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstSegmentURLNode *segmentURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentList duration=\"1\">"
      "      <SegmentURL media=\"TestMedia\""
      "                  mediaRange=\"100-200\""
      "                  index=\"TestIndex\""
      "                  indexRange=\"300-400\">"
      "      </SegmentURL></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  segmentURL = (GstSegmentURLNode *) segmentList->SegmentURL->data;
  assert_equals_string (segmentURL->media, "TestMedia");
  assert_equals_uint64 (segmentURL->mediaRange->first_byte_pos, 100);
  assert_equals_uint64 (segmentURL->mediaRange->last_byte_pos, 200);
  assert_equals_string (segmentURL->index, "TestIndex");
  assert_equals_uint64 (segmentURL->indexRange->first_byte_pos, 300);
  assert_equals_uint64 (segmentURL->indexRange->last_byte_pos, 400);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentTemplate)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentTemplate media=\"TestMedia\""
      "                     duration=\"0\""
      "                     index=\"TestIndex\""
      "                     initialization=\"TestInitialization\""
      "                     bitstreamSwitching=\"TestBitstreamSwitching\">"
      "    </SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  assert_equals_string (segmentTemplate->media, "TestMedia");
  assert_equals_string (segmentTemplate->index, "TestIndex");
  assert_equals_string (segmentTemplate->initialization, "TestInitialization");
  assert_equals_string (segmentTemplate->bitstreamSwitching,
      "TestBitstreamSwitching");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate MultipleSegmentBaseType attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  GstMultSegmentBaseType *multSegBaseType;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentTemplate duration=\"10\""
      "                     startNumber=\"11\">"
      "    </SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  multSegBaseType = segmentTemplate->MultSegBaseType;
  assert_equals_uint64 (multSegBaseType->duration, 10);
  assert_equals_uint64 (multSegBaseType->startNumber, 11);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate MultipleSegmentBaseType SegmentBaseType
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentBaseType)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentBaseType *segBaseType;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentTemplate timescale=\"123456\""
      "                     duration=\"1\""
      "                     presentationTimeOffset=\"123456789\""
      "                     indexRange=\"100-200\""
      "                     indexRangeExact=\"true\">"
      "    </SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  multSegBaseType = segmentTemplate->MultSegBaseType;
  segBaseType = multSegBaseType->SegBaseType;
  assert_equals_uint64 (segBaseType->timescale, 123456);
  assert_equals_uint64 (segBaseType->presentationTimeOffset, 123456789);
  assert_equals_uint64 (segBaseType->indexRange->first_byte_pos, 100);
  assert_equals_uint64 (segBaseType->indexRange->last_byte_pos, 200);
  assert_equals_int (segBaseType->indexRangeExact, TRUE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate MultipleSegmentBaseType SegmentTimeline
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentTimeline)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentTimelineNode *segmentTimeline;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentTemplate>"
      "      <SegmentTimeline>"
      "      </SegmentTimeline></SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  multSegBaseType = segmentTemplate->MultSegBaseType;
  segmentTimeline = (GstSegmentTimelineNode *) multSegBaseType->SegmentTimeline;
  fail_if (segmentTimeline == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate MultipleSegmentBaseType SegmentTimeline
 * S attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentTimeline_s)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentTimelineNode *segmentTimeline;
  GstSNode *sNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentTemplate>"
      "      <SegmentTimeline>"
      "        <S t=\"1\" d=\"2\" r=\"3\">"
      "        </S></SegmentTimeline></SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  multSegBaseType = segmentTemplate->MultSegBaseType;
  segmentTimeline = (GstSegmentTimelineNode *) multSegBaseType->SegmentTimeline;
  sNode = (GstSNode *) g_queue_peek_head (&segmentTimeline->S);
  assert_equals_uint64 (sNode->t, 1);
  assert_equals_uint64 (sNode->d, 2);
  assert_equals_uint64 (sNode->r, 3);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate MultipleSegmentBaseType
 * BitstreamSwitching attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_bitstreamSwitching)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  GstMultSegmentBaseType *multSegBaseType;
  GstURLType *bitstreamSwitching;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentTemplate duration=\"1\">"
      "      <BitstreamSwitching sourceURL=\"TestSourceURL\""
      "                          range=\"100-200\">"
      "      </BitstreamSwitching></SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  multSegBaseType = segmentTemplate->MultSegBaseType;
  bitstreamSwitching = multSegBaseType->BitstreamSwitching;
  assert_equals_string (bitstreamSwitching->sourceURL, "TestSourceURL");
  assert_equals_uint64 (bitstreamSwitching->range->first_byte_pos, 100);
  assert_equals_uint64 (bitstreamSwitching->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet id=\"7\""
      "                   group=\"8\""
      "                   lang=\"en\""
      "                   contentType=\"TestContentType\""
      "                   par=\"4:3\""
      "                   minBandwidth=\"100\""
      "                   maxBandwidth=\"200\""
      "                   minWidth=\"1000\""
      "                   maxWidth=\"2000\""
      "                   minHeight=\"1100\""
      "                   maxHeight=\"2100\""
      "                   minFrameRate=\"25/123\""
      "                   maxFrameRate=\"26\""
      "                   segmentAlignment=\"2\""
      "                   subsegmentAlignment=\"false\""
      "                   subsegmentStartsWithSAP=\"6\""
      "                   bitstreamSwitching=\"false\">"
      "    </AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  assert_equals_uint64 (adaptationSet->id, 7);
  assert_equals_uint64 (adaptationSet->group, 8);
  assert_equals_string (adaptationSet->lang, "en");
  assert_equals_string (adaptationSet->contentType, "TestContentType");
  assert_equals_uint64 (adaptationSet->par->num, 4);
  assert_equals_uint64 (adaptationSet->par->den, 3);
  assert_equals_uint64 (adaptationSet->minBandwidth, 100);
  assert_equals_uint64 (adaptationSet->maxBandwidth, 200);
  assert_equals_uint64 (adaptationSet->minWidth, 1000);
  assert_equals_uint64 (adaptationSet->maxWidth, 2000);
  assert_equals_uint64 (adaptationSet->minHeight, 1100);
  assert_equals_uint64 (adaptationSet->maxHeight, 2100);
  assert_equals_uint64 (adaptationSet->RepresentationBase->minFrameRate->num,
      25);
  assert_equals_uint64 (adaptationSet->RepresentationBase->minFrameRate->den,
      123);
  assert_equals_uint64 (adaptationSet->RepresentationBase->maxFrameRate->num,
      26);
  assert_equals_uint64 (adaptationSet->RepresentationBase->maxFrameRate->den,
      1);
  assert_equals_int (adaptationSet->segmentAlignment->flag, 1);
  assert_equals_uint64 (adaptationSet->segmentAlignment->value, 2);
  assert_equals_int (adaptationSet->subsegmentAlignment->flag, 0);
  assert_equals_uint64 (adaptationSet->subsegmentAlignment->value, 0);
  assert_equals_int (adaptationSet->subsegmentStartsWithSAP, 6);
  assert_equals_int (adaptationSet->bitstreamSwitching, 0);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet RepresentationBase attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_representationBase)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationBaseType *representationBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet profiles=\"TestProfiles\""
      "                   width=\"100\""
      "                   height=\"200\""
      "                   sar=\"10:20\""
      "                   frameRate=\"30/40\""
      "                   audioSamplingRate=\"TestAudioSamplingRate\""
      "                   mimeType=\"TestMimeType\""
      "                   segmentProfiles=\"TestSegmentProfiles\""
      "                   codecs=\"TestCodecs\""
      "                   maximumSAPPeriod=\"3.4\""
      "                   startWithSAP=\"0\""
      "                   maxPlayoutRate=\"1.2\""
      "                   codingDependency=\"false\""
      "                   scanType=\"progressive\">"
      "    </AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representationBase = adaptationSet->RepresentationBase;
  assert_equals_string (representationBase->profiles, "TestProfiles");
  assert_equals_uint64 (representationBase->width, 100);
  assert_equals_uint64 (representationBase->height, 200);
  assert_equals_uint64 (representationBase->sar->num, 10);
  assert_equals_uint64 (representationBase->sar->den, 20);
  assert_equals_uint64 (representationBase->frameRate->num, 30);
  assert_equals_uint64 (representationBase->frameRate->den, 40);
  assert_equals_string (representationBase->audioSamplingRate,
      "TestAudioSamplingRate");
  assert_equals_string (representationBase->mimeType, "TestMimeType");
  assert_equals_string (representationBase->segmentProfiles,
      "TestSegmentProfiles");
  assert_equals_string (representationBase->codecs, "TestCodecs");
  assert_equals_float (representationBase->maximumSAPPeriod, 3.4);
  assert_equals_int (representationBase->startWithSAP, GST_SAP_TYPE_0);
  assert_equals_float (representationBase->maxPlayoutRate, 1.2);
  assert_equals_float (representationBase->codingDependency, 0);
  assert_equals_string (representationBase->scanType, "progressive");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet RepresentationBase FramePacking attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representationBase_framePacking) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationBaseType *representationBase;
  GstDescriptorType *framePacking;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <FramePacking schemeIdUri=\"TestSchemeIdUri\""
      "                    value=\"TestValue\">"
      "      </FramePacking></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representationBase = adaptationSet->RepresentationBase;
  framePacking = (GstDescriptorType *) representationBase->FramePacking->data;
  assert_equals_string (framePacking->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (framePacking->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet RepresentationBase
 * AudioChannelConfiguration attributes
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representationBase_audioChannelConfiguration)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationBaseType *representationBase;
  GstDescriptorType *audioChannelConfiguration;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <AudioChannelConfiguration schemeIdUri=\"TestSchemeIdUri\""
      "                                 value=\"TestValue\">"
      "      </AudioChannelConfiguration></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representationBase = adaptationSet->RepresentationBase;
  audioChannelConfiguration =
      (GstDescriptorType *) representationBase->AudioChannelConfiguration->data;
  assert_equals_string (audioChannelConfiguration->schemeIdUri,
      "TestSchemeIdUri");
  assert_equals_string (audioChannelConfiguration->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet RepresentationBase ContentProtection
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representationBase_contentProtection) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationBaseType *representationBase;
  GstDescriptorType *contentProtection;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <ContentProtection schemeIdUri=\"TestSchemeIdUri\""
      "                         value=\"TestValue\">"
      "      </ContentProtection></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representationBase = adaptationSet->RepresentationBase;
  contentProtection =
      (GstDescriptorType *) representationBase->ContentProtection->data;
  assert_equals_string (contentProtection->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (contentProtection->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing ContentProtection element that has no value attribute
 */
GST_START_TEST (dash_mpdparser_contentProtection_no_value)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationBaseType *representationBase;
  GstDescriptorType *contentProtection;
  const gchar *xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     xmlns:mspr=\"urn:microsoft:playready\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <ContentProtection schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" value=\"cenc\"/>"
      "      <ContentProtection xmlns:mas=\"urn:marlin:mas:1-0:services:schemas:mpd\" schemeIdUri=\"urn:uuid:5e629af5-38da-4063-8977-97ffbd9902d4\">"
      "	      <mas:MarlinContentIds>"
      "	        <mas:MarlinContentId>urn:marlin:kid:02020202020202020202020202020202</mas:MarlinContentId>"
      "       </mas:MarlinContentIds>"
      "      </ContentProtection>"
      "      <ContentProtection schemeIdUri=\"urn:uuid:9a04f079-9840-4286-ab92-e65be0885f95\" value=\"MSPR 2.0\">"
      "        <mspr:pro>dGVzdA==</mspr:pro>"
      "     </ContentProtection>" "</AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();
  gchar *str;

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representationBase = adaptationSet->RepresentationBase;
  assert_equals_int (g_list_length (representationBase->ContentProtection), 3);
  contentProtection =
      (GstDescriptorType *) g_list_nth (representationBase->ContentProtection,
      1)->data;
  assert_equals_string (contentProtection->schemeIdUri,
      "urn:uuid:5e629af5-38da-4063-8977-97ffbd9902d4");
  fail_if (contentProtection->value == NULL);
  /* We can't do a simple compare of value (which should be an XML dump
     of the ContentProtection element), because the whitespace
     formatting from xmlDump might differ between versions of libxml */
  str = strstr (contentProtection->value, "<ContentProtection");
  fail_if (str == NULL);
  str = strstr (contentProtection->value, "<mas:MarlinContentIds>");
  fail_if (str == NULL);
  str = strstr (contentProtection->value, "<mas:MarlinContentId>");
  fail_if (str == NULL);
  str =
      strstr (contentProtection->value,
      "urn:marlin:kid:02020202020202020202020202020202");
  fail_if (str == NULL);
  str = strstr (contentProtection->value, "</ContentProtection>");
  fail_if (str == NULL);
  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing ContentProtection element that has no value attribute
 * nor an XML encoding
 */
GST_START_TEST (dash_mpdparser_contentProtection_no_value_no_encoding)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationBaseType *representationBase;
  GstDescriptorType *contentProtection;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <ContentProtection schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" value=\"cenc\"/>"
      "      <ContentProtection xmlns:mas=\"urn:marlin:mas:1-0:services:schemas:mpd\" schemeIdUri=\"urn:uuid:5e629af5-38da-4063-8977-97ffbd9902d4\">"
      "	      <mas:MarlinContentIds>"
      "	        <mas:MarlinContentId>urn:marlin:kid:02020202020202020202020202020202</mas:MarlinContentId>"
      "       </mas:MarlinContentIds>"
      "     </ContentProtection>" "</AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representationBase = adaptationSet->RepresentationBase;
  assert_equals_int (g_list_length (representationBase->ContentProtection), 2);
  contentProtection =
      (GstDescriptorType *) g_list_nth (representationBase->ContentProtection,
      1)->data;
  assert_equals_string (contentProtection->schemeIdUri,
      "urn:uuid:5e629af5-38da-4063-8977-97ffbd9902d4");
  fail_if (contentProtection->value == NULL);
  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Accessibility attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_accessibility)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstDescriptorType *accessibility;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Accessibility schemeIdUri=\"TestSchemeIdUri\""
      "                     value=\"TestValue\">"
      "      </Accessibility></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  accessibility = (GstDescriptorType *) adaptationSet->Accessibility->data;
  assert_equals_string (accessibility->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (accessibility->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Role attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_role)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstDescriptorType *role;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Role schemeIdUri=\"TestSchemeIdUri\""
      "            value=\"TestValue\">"
      "      </Role></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  role = (GstDescriptorType *) adaptationSet->Role->data;
  assert_equals_string (role->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (role->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Rating attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_rating)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstDescriptorType *rating;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Rating schemeIdUri=\"TestSchemeIdUri\""
      "              value=\"TestValue\">"
      "      </Rating></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  rating = (GstDescriptorType *) adaptationSet->Rating->data;
  assert_equals_string (rating->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (rating->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Viewpoint attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_viewpoint)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstDescriptorType *viewpoint;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Viewpoint schemeIdUri=\"TestSchemeIdUri\""
      "                 value=\"TestValue\">"
      "      </Viewpoint></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  viewpoint = (GstDescriptorType *) adaptationSet->Viewpoint->data;
  assert_equals_string (viewpoint->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (viewpoint->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet ContentComponent attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_contentComponent)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstContentComponentNode *contentComponent;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <ContentComponent id=\"1\""
      "                        lang=\"en\""
      "                        contentType=\"TestContentType\""
      "                        par=\"10:20\">"
      "      </ContentComponent></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  contentComponent = (GstContentComponentNode *)
      adaptationSet->ContentComponents->data;
  assert_equals_uint64 (contentComponent->id, 1);
  assert_equals_string (contentComponent->lang, "en");
  assert_equals_string (contentComponent->contentType, "TestContentType");
  assert_equals_uint64 (contentComponent->par->num, 10);
  assert_equals_uint64 (contentComponent->par->den, 20);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet ContentComponent Accessibility attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_contentComponent_accessibility) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstContentComponentNode *contentComponent;
  GstDescriptorType *accessibility;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <ContentComponent>"
      "        <Accessibility schemeIdUri=\"TestSchemeIdUri\""
      "                       value=\"TestValue\">"
      "        </Accessibility>"
      "      </ContentComponent></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  contentComponent = (GstContentComponentNode *)
      adaptationSet->ContentComponents->data;
  accessibility = (GstDescriptorType *) contentComponent->Accessibility->data;
  assert_equals_string (accessibility->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (accessibility->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet ContentComponent Role attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_contentComponent_role)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstContentComponentNode *contentComponent;
  GstDescriptorType *role;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <ContentComponent>"
      "        <Role schemeIdUri=\"TestSchemeIdUri\""
      "              value=\"TestValue\">"
      "        </Role></ContentComponent></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  contentComponent = (GstContentComponentNode *)
      adaptationSet->ContentComponents->data;
  role = (GstDescriptorType *) contentComponent->Role->data;
  assert_equals_string (role->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (role->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet ContentComponent Rating attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_contentComponent_rating)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstContentComponentNode *contentComponent;
  GstDescriptorType *rating;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <ContentComponent>"
      "        <Rating schemeIdUri=\"TestSchemeIdUri\""
      "                value=\"TestValue\">"
      "        </Rating>"
      "      </ContentComponent></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  contentComponent = (GstContentComponentNode *)
      adaptationSet->ContentComponents->data;
  rating = (GstDescriptorType *) contentComponent->Rating->data;
  assert_equals_string (rating->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (rating->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet ContentComponent Viewpoint attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_contentComponent_viewpoint)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstContentComponentNode *contentComponent;
  GstDescriptorType *viewpoint;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <ContentComponent>"
      "        <Viewpoint schemeIdUri=\"TestSchemeIdUri\""
      "                   value=\"TestValue\">"
      "        </Viewpoint>"
      "      </ContentComponent></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  contentComponent = (GstContentComponentNode *)
      adaptationSet->ContentComponents->data;
  viewpoint = (GstDescriptorType *) contentComponent->Viewpoint->data;
  assert_equals_string (viewpoint->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (viewpoint->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet BaseURL attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_baseURL)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstBaseURL *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <BaseURL serviceLocation=\"TestServiceLocation\""
      "               byteRange=\"TestByteRange\">TestBaseURL</BaseURL>"
      "    </AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  baseURL = (GstBaseURL *) adaptationSet->BaseURLs->data;
  assert_equals_string (baseURL->baseURL, "TestBaseURL");
  assert_equals_string (baseURL->serviceLocation, "TestServiceLocation");
  assert_equals_string (baseURL->byteRange, "TestByteRange");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentBase attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_segmentBase)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentBaseType *segmentBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <SegmentBase timescale=\"123456\""
      "                   presentationTimeOffset=\"123456789\""
      "                   indexRange=\"100-200\""
      "                   indexRangeExact=\"true\">"
      "      </SegmentBase></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentBase = adaptationSet->SegmentBase;
  assert_equals_uint64 (segmentBase->timescale, 123456);
  assert_equals_uint64 (segmentBase->presentationTimeOffset, 123456789);
  assert_equals_uint64 (segmentBase->indexRange->first_byte_pos, 100);
  assert_equals_uint64 (segmentBase->indexRange->last_byte_pos, 200);
  assert_equals_int (segmentBase->indexRangeExact, TRUE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentBase Initialization attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_segmentBase_initialization)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentBaseType *segmentBase;
  GstURLType *initialization;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <SegmentBase>"
      "        <Initialisation sourceURL=\"TestSourceURL\""
      "                        range=\"100-200\">"
      "        </Initialisation></SegmentBase></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentBase = adaptationSet->SegmentBase;
  initialization = segmentBase->Initialization;
  assert_equals_string (initialization->sourceURL, "TestSourceURL");
  assert_equals_uint64 (initialization->range->first_byte_pos, 100);
  assert_equals_uint64 (initialization->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentBase RepresentationIndex attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_segmentBase_representationIndex) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentBaseType *segmentBase;
  GstURLType *representationIndex;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <SegmentBase>"
      "        <RepresentationIndex sourceURL=\"TestSourceURL\""
      "                             range=\"100-200\">"
      "        </RepresentationIndex>"
      "      </SegmentBase></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentBase = adaptationSet->SegmentBase;
  representationIndex = segmentBase->RepresentationIndex;
  assert_equals_string (representationIndex->sourceURL, "TestSourceURL");
  assert_equals_uint64 (representationIndex->range->first_byte_pos, 100);
  assert_equals_uint64 (representationIndex->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentList attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_segmentList)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentListNode *segmentList;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <SegmentList duration=\"1\"></SegmentList></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentList = adaptationSet->SegmentList;
  fail_if (segmentList == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentTemplate attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_segmentTemplate)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentTemplateNode *segmentTemplate;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <SegmentTemplate media=\"TestMedia\""
      "                       duration=\"1\""
      "                       index=\"TestIndex\""
      "                       initialization=\"TestInitialization\""
      "                       bitstreamSwitching=\"TestBitstreamSwitching\">"
      "      </SegmentTemplate></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentTemplate = adaptationSet->SegmentTemplate;
  assert_equals_string (segmentTemplate->media, "TestMedia");
  assert_equals_string (segmentTemplate->index, "TestIndex");
  assert_equals_string (segmentTemplate->initialization, "TestInitialization");
  assert_equals_string (segmentTemplate->bitstreamSwitching,
      "TestBitstreamSwitching");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;


GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representation_segmentTemplate_inherit)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSegmentTemplateNode *segmentTemplate;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentTemplate media=\"ParentMedia\" duration=\"1\" "
      "                     initialization=\"ParentInitialization\">"
      "    </SegmentTemplate>"
      "    <AdaptationSet>"
      "      <Representation id=\"1\" bandwidth=\"5000\">"
      "      <SegmentTemplate media=\"TestMedia\""
      "                       index=\"TestIndex\""
      "                       bitstreamSwitching=\"TestBitstreamSwitching\">"
      "      </SegmentTemplate></Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation =
      (GstRepresentationNode *) adaptationSet->Representations->data;
  segmentTemplate = representation->SegmentTemplate;
  assert_equals_string (segmentTemplate->media, "TestMedia");
  assert_equals_string (segmentTemplate->index, "TestIndex");
  assert_equals_string (segmentTemplate->initialization,
      "ParentInitialization");
  assert_equals_string (segmentTemplate->bitstreamSwitching,
      "TestBitstreamSwitching");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representation_segmentBase_inherit) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSegmentBaseType *segmentBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentBase timescale=\"123456\""
      "                 presentationTimeOffset=\"123456789\""
      "                 indexRange=\"100-200\""
      "                 indexRangeExact=\"true\">"
      "      <Initialisation sourceURL=\"TestSourceURL\""
      "                      range=\"100-200\" />"
      "    </SegmentBase>"
      "    <AdaptationSet>"
      "      <Representation id=\"1\" bandwidth=\"5000\">"
      "      <SegmentBase>"
      "      </SegmentBase></Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation =
      (GstRepresentationNode *) adaptationSet->Representations->data;
  segmentBase = representation->SegmentBase;
  assert_equals_int (segmentBase->timescale, 123456);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentTemplate attributes with
 * inheritance
 */
GST_START_TEST (dash_mpdparser_adapt_repr_segmentTemplate_inherit)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentTemplateNode *segmentTemplate;
  GstRepresentationNode *representation;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentBaseType *segBaseType;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period duration=\"PT0H5M0.000S\">"
      "    <AdaptationSet maxWidth=\"1280\" maxHeight=\"720\" maxFrameRate=\"50\">"
      "      <SegmentTemplate initialization=\"set1_init.mp4\"/>"
      "      <Representation id=\"1\" mimeType=\"video/mp4\" codecs=\"avc1.640020\" "
      "          width=\"1280\" height=\"720\" frameRate=\"50\" bandwidth=\"30000\">"
      "        <SegmentTemplate timescale=\"12800\" media=\"track1_$Number$.m4s\" startNumber=\"1\" duration=\"25600\"/>"
      "  </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  segmentTemplate = representation->SegmentTemplate;
  fail_if (segmentTemplate == NULL);
  multSegBaseType = segmentTemplate->MultSegBaseType;
  segBaseType = multSegBaseType->SegBaseType;

  assert_equals_uint64 (segBaseType->timescale, 12800);
  assert_equals_uint64 (multSegBaseType->duration, 25600);
  assert_equals_uint64 (multSegBaseType->startNumber, 1);
  assert_equals_string (segmentTemplate->media, "track1_$Number$.m4s");
  assert_equals_string (segmentTemplate->initialization, "set1_init.mp4");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;
/*
 * Test parsing Period AdaptationSet SegmentTemplate attributes with
 * inheritance
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_segmentTemplate_inherit)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentTemplateNode *segmentTemplate;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <SegmentTemplate media=\"ParentMedia\" duration=\"1\" "
      "                     initialization=\"ParentInitialization\">"
      "    </SegmentTemplate>"
      "    <AdaptationSet>"
      "      <SegmentTemplate media=\"TestMedia\""
      "                       duration=\"1\""
      "                       index=\"TestIndex\""
      "                       bitstreamSwitching=\"TestBitstreamSwitching\">"
      "      </SegmentTemplate></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentTemplate = adaptationSet->SegmentTemplate;
  assert_equals_string (segmentTemplate->media, "TestMedia");
  assert_equals_string (segmentTemplate->index, "TestIndex");
  assert_equals_string (segmentTemplate->initialization,
      "ParentInitialization");
  assert_equals_string (segmentTemplate->bitstreamSwitching,
      "TestBitstreamSwitching");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_representation)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Representation id=\"Test_Id\""
      "                      bandwidth=\"100\""
      "                      qualityRanking=\"200\""
      "                      dependencyId=\"one two three\""
      "                      mediaStreamStructureId=\"\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  assert_equals_string (representation->id, "Test_Id");
  assert_equals_uint64 (representation->bandwidth, 100);
  assert_equals_uint64 (representation->qualityRanking, 200);
  assert_equals_string (representation->dependencyId[0], "one");
  assert_equals_string (representation->dependencyId[1], "two");
  assert_equals_string (representation->dependencyId[2], "three");
  assert_equals_pointer (representation->dependencyId[3], NULL);
  assert_equals_pointer (representation->mediaStreamStructureId[0], NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation RepresentationBaseType attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representation_representationBase) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstRepresentationBaseType *representationBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  representationBase = (GstRepresentationBaseType *)
      representation->RepresentationBase;
  fail_if (representationBase == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation BaseURL attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_representation_baseURL)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstBaseURL *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <BaseURL serviceLocation=\"TestServiceLocation\""
      "                 byteRange=\"TestByteRange\">TestBaseURL</BaseURL>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  baseURL = (GstBaseURL *) representation->BaseURLs->data;
  assert_equals_string (baseURL->baseURL, "TestBaseURL");
  assert_equals_string (baseURL->serviceLocation, "TestServiceLocation");
  assert_equals_string (baseURL->byteRange, "TestByteRange");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation SubRepresentation attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representation_subRepresentation) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSubRepresentationNode *subRepresentation;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SubRepresentation level=\"100\""
      "                           dependencyLevel=\"1 2 3\""
      "                           bandwidth=\"200\""
      "                           contentComponent=\"content1 content2\">"
      "        </SubRepresentation>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  subRepresentation = (GstSubRepresentationNode *)
      representation->SubRepresentations->data;
  assert_equals_uint64 (subRepresentation->level, 100);
  assert_equals_uint64 (subRepresentation->size, 3);
  assert_equals_uint64 (subRepresentation->dependencyLevel[0], 1);
  assert_equals_uint64 (subRepresentation->dependencyLevel[1], 2);
  assert_equals_uint64 (subRepresentation->dependencyLevel[2], 3);
  assert_equals_uint64 (subRepresentation->bandwidth, 200);
  assert_equals_string (subRepresentation->contentComponent[0], "content1");
  assert_equals_string (subRepresentation->contentComponent[1], "content2");
  assert_equals_pointer (subRepresentation->contentComponent[2], NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation SubRepresentation
 * RepresentationBase attributes
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representation_subRepresentation_representationBase)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSubRepresentationNode *subRepresentation;
  GstRepresentationBaseType *representationBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SubRepresentation>"
      "        </SubRepresentation>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  subRepresentation = (GstSubRepresentationNode *)
      representation->SubRepresentations->data;
  representationBase = (GstRepresentationBaseType *)
      subRepresentation->RepresentationBase;
  fail_if (representationBase == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation SegmentBase attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_representation_segmentBase)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSegmentBaseType *segmentBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SegmentBase>"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  segmentBase = representation->SegmentBase;
  fail_if (segmentBase == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation SegmentList attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_representation_segmentList)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSegmentListNode *segmentList;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SegmentList duration=\"1\">"
      "        </SegmentList>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  segmentList = representation->SegmentList;
  fail_if (segmentList == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation SegmentTemplate attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representation_segmentTemplate) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSegmentTemplateNode *segmentTemplate;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SegmentTemplate duration=\"1\">"
      "        </SegmentTemplate>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  segmentTemplate = representation->SegmentTemplate;
  fail_if (segmentTemplate == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period Subset attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_subset)
{
  GstPeriodNode *periodNode;
  GstSubsetNode *subset;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period><Subset contains=\"1 2 3\"></Subset></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  subset = (GstSubsetNode *) periodNode->Subsets->data;
  assert_equals_uint64 (subset->size, 3);
  assert_equals_uint64 (subset->contains[0], 1);
  assert_equals_uint64 (subset->contains[1], 2);
  assert_equals_uint64 (subset->contains[2], 3);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing UTCTiming elements
 *
 */
GST_START_TEST (dash_mpdparser_utctiming)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"http://time.akamai.com/?iso http://example.time/xsdate\"/>"
      "<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:direct:2014\" value=\"2002-05-30T09:30:10Z \"/>"
      "<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:ntp:2014\" value=\"0.europe.pool.ntp.org 1.europe.pool.ntp.org 2.europe.pool.ntp.org 3.europe.pool.ntp.org\"/>"
      "</MPD>";
  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();
  GstMPDUTCTimingType selected_method;
  gchar **urls;

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  fail_if (mpdclient->mpd_node == NULL);
  fail_if (mpdclient->mpd_node->UTCTiming == NULL);
  assert_equals_int (g_list_length (mpdclient->mpd_node->UTCTiming), 3);
  urls =
      gst_mpd_client_get_utc_timing_sources (mpdclient,
      GST_MPD_UTCTIMING_TYPE_HTTP_XSDATE, &selected_method);
  fail_if (urls == NULL);
  assert_equals_int (selected_method, GST_MPD_UTCTIMING_TYPE_HTTP_XSDATE);
  assert_equals_int (g_strv_length (urls), 2);
  assert_equals_string (urls[0], "http://time.akamai.com/?iso");
  assert_equals_string (urls[1], "http://example.time/xsdate");
  urls =
      gst_mpd_client_get_utc_timing_sources (mpdclient,
      GST_MPD_UTCTIMING_TYPE_HTTP_XSDATE | GST_MPD_UTCTIMING_TYPE_HTTP_ISO,
      &selected_method);
  fail_if (urls == NULL);
  assert_equals_int (selected_method, GST_MPD_UTCTIMING_TYPE_HTTP_XSDATE);
  urls =
      gst_mpd_client_get_utc_timing_sources (mpdclient,
      GST_MPD_UTCTIMING_TYPE_DIRECT, NULL);
  fail_if (urls == NULL);
  assert_equals_int (g_strv_length (urls), 1);
  assert_equals_string (urls[0], "2002-05-30T09:30:10Z ");
  urls =
      gst_mpd_client_get_utc_timing_sources (mpdclient,
      GST_MPD_UTCTIMING_TYPE_HTTP_XSDATE | GST_MPD_UTCTIMING_TYPE_DIRECT,
      &selected_method);
  fail_if (urls == NULL);
  assert_equals_int (selected_method, GST_MPD_UTCTIMING_TYPE_HTTP_XSDATE);
  urls =
      gst_mpd_client_get_utc_timing_sources (mpdclient,
      GST_MPD_UTCTIMING_TYPE_NTP, &selected_method);
  fail_if (urls == NULL);
  assert_equals_int (selected_method, GST_MPD_UTCTIMING_TYPE_NTP);
  assert_equals_int (g_strv_length (urls), 4);
  assert_equals_string (urls[0], "0.europe.pool.ntp.org");
  assert_equals_string (urls[1], "1.europe.pool.ntp.org");
  assert_equals_string (urls[2], "2.europe.pool.ntp.org");
  assert_equals_string (urls[3], "3.europe.pool.ntp.org");
  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing invalid UTCTiming values:
 * - elements with no schemeIdUri property should be rejected
 * - elements with no value property should be rejected
 * - elements with unrecognised UTCTiming scheme should be rejected
 * - elements with empty values should be rejected
 *
 */
GST_START_TEST (dash_mpdparser_utctiming_invalid_value)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<UTCTiming invalid_schemeIdUri=\"dummy.uri.scheme\" value=\"dummy value\"/>"
      "<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:ntp:2014\" invalid_value=\"dummy value\"/>"
      "<UTCTiming schemeIdUri=\"dummy.uri.scheme\" value=\"dummy value\"/>"
      "<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:ntp:2014\" value=\"\"/>"
      "</MPD>";
  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  fail_if (mpdclient->mpd_node == NULL);
  fail_if (mpdclient->mpd_node->UTCTiming != NULL);
  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the type property: value "dynamic"
 *
 */
GST_START_TEST (dash_mpdparser_type_dynamic)
{
  gboolean isLive;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD type=\"dynamic\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"> </MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  isLive = gst_mpd_client_is_live (mpdclient);
  assert_equals_int (isLive, 1);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Validate gst_mpdparser_build_URL_from_template function
 *
 */
GST_START_TEST (dash_mpdparser_template_parsing)
{
  const gchar *id = "TestId";
  guint number = 7;
  guint bandwidth = 2500;
  guint64 time = 100;
  gchar *result;

  struct TestUrl
  {
    const gchar *urlTemplate;
    const gchar *expectedResponse;
  };

  /* various test scenarios to attempt */
  struct TestUrl testUrl[] = {
    {"", NULL},                 /* empty string for template */
    {"$$", "$"},                /* escaped $ */
    {"Number", "Number"},       /* string similar with an identifier, but without $ */
    {"Number$Number$", "Number7"},      /* Number identifier */
    {"Number$Number$$$", "Number7$"},   /* Number identifier followed by $$ */
    {"Number$Number$Number$Number$", "Number7Number7"}, /* series of "Number" string and Number identifier */
    {"Representation$RepresentationID$", "RepresentationTestId"},       /* RepresentationID identifier */
    {"TestMedia$Bandwidth$$$test", "TestMedia2500$test"},       /* Bandwidth identifier */
    {"TestMedia$Time$", "TestMedia100"},        /* Time identifier */
    {"TestMedia$Time", NULL},   /* Identifier not finished with $ */
    {"Time$Time%d$", NULL},     /* usage of %d (no width) */
    {"Time$Time%0d$", "Time100"},       /* usage of format smaller than number of digits */
    {"Time$Time%01d$", "Time100"},      /* usage of format smaller than number of digits */
    {"Time$Time%05d$", "Time00100"},    /* usage of format bigger than number of digits */
    {"Time$Time%05dtest$", "Time00100test"},    /* usage extra text in format */
    {"Time$Time%3d$", NULL},    /* incorrect format: width does not start with 0 */
    {"Time$Time%0-4d$", NULL},  /* incorrect format: width is not a number */
    {"Time$Time%0$", NULL},     /* incorrect format: no d, x or u */
    {"Time$Time1%01d$", NULL},  /* incorrect format: does not start with % after identifier */
    {"$Bandwidth%/init.mp4v", NULL},    /* incorrect identifier: not finished with $ */
    {"$Number%/$Time$.mp4v", NULL},     /* incorrect number of $ separators */
    {"$RepresentationID1$", NULL},      /* incorrect identifier */
    {"$Bandwidth1$", NULL},     /* incorrect identifier */
    {"$Number1$", NULL},        /* incorrect identifier */
    {"$RepresentationID%01d$", NULL},   /* incorrect format: RepresentationID does not support formatting */
    {"Time$Time%05u$", NULL},   /* %u format */
    {"Time$Time%05x$", NULL},   /* %x format */
    {"Time$Time%05utest$", NULL},       /* %u format followed by text */
    {"Time$Time%05xtest$", NULL},       /* %x format followed by text */
    {"Time$Time%05xtest%$", NULL},      /* second % character in format */
  };

  guint count = sizeof (testUrl) / sizeof (testUrl[0]);
  gint i;

  for (i = 0; i < count; i++) {
    result =
        gst_mpdparser_build_URL_from_template (testUrl[i].urlTemplate, id,
        number, bandwidth, time);
    assert_equals_string (result, testUrl[i].expectedResponse);
    g_free (result);
  }
}

GST_END_TEST;

/*
 * Test handling isoff ondemand profile
 *
 */
GST_START_TEST (dash_mpdparser_isoff_ondemand_profile)
{
  gboolean hasOnDemandProfile;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\"></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  hasOnDemandProfile = gst_mpd_client_has_isoff_ondemand_profile (mpdclient);
  assert_equals_int (hasOnDemandProfile, 1);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling GstDateTime
 *
 */
GST_START_TEST (dash_mpdparser_GstDateTime)
{
  gint64 delta;
  GstDateTime *time1;
  GstDateTime *time2;
  GstDateTime *time3;
  GDateTime *g_time2;
  GDateTime *g_time3;

  time1 = gst_date_time_new_from_iso8601_string ("2012-06-23T23:30:59Z");
  time2 = gst_date_time_new_from_iso8601_string ("2012-06-23T23:31:00Z");

  delta = gst_mpd_client_calculate_time_difference (time1, time2);
  assert_equals_int64 (delta, 1 * GST_SECOND);

  time3 =
      gst_mpd_client_add_time_difference (time1, GST_TIME_AS_USECONDS (delta));

  /* convert to GDateTime in order to compare time2 and time 3 */
  g_time2 = gst_date_time_to_g_date_time (time2);
  g_time3 = gst_date_time_to_g_date_time (time3);
  fail_if (g_date_time_compare (g_time2, g_time3) != 0);

  gst_date_time_unref (time1);
  gst_date_time_unref (time2);
  gst_date_time_unref (time3);
  g_date_time_unref (g_time2);
  g_date_time_unref (g_time3);
}

GST_END_TEST;

/*
 * Test bitstreamSwitching inheritance from Period to AdaptationSet
 *
 * Description of bistreamSwitching attribute in Period:
 * "When set to ‘true’, this is equivalent as if the
 * AdaptationSet@bitstreamSwitching for each Adaptation Set contained in this
 * Period is set to 'true'. In this case, the AdaptationSet@bitstreamSwitching
 * attribute shall not be set to 'false' for any Adaptation Set in this Period"
 *
 */
GST_START_TEST (dash_mpdparser_bitstreamSwitching_inheritance)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  guint activeStreams;
  GstActiveStream *activeStream;
  GstCaps *caps;
  GstStructure *s;
  gboolean bitstreamSwitchingFlag;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"Period0\""
      "          duration=\"P0Y0M1DT1H1M1S\""
      "          bitstreamSwitching=\"true\">"
      "    <AdaptationSet id=\"1\""
      "                   mimeType=\"video/mp4\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet id=\"2\""
      "                   mimeType=\"audio\""
      "                   bitstreamSwitching=\"false\">"
      "      <Representation id=\"2\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret = gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  /* setup streaming from the second adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 1);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  /* 2 active streams */
  activeStreams = gst_mpdparser_get_nb_active_stream (mpdclient);
  assert_equals_int (activeStreams, 2);

  /* get details of the first active stream */
  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);

  assert_equals_int (activeStream->mimeType, GST_STREAM_VIDEO);
  caps = gst_mpd_client_get_stream_caps (activeStream);
  fail_unless (caps != NULL);
  s = gst_caps_get_structure (caps, 0);
  assert_equals_string (gst_structure_get_name (s), "video/quicktime");
  gst_caps_unref (caps);

  /* inherited from Period's bitstreamSwitching */
  bitstreamSwitchingFlag =
      gst_mpd_client_get_bitstream_switching_flag (activeStream);
  assert_equals_int (bitstreamSwitchingFlag, TRUE);

  /* get details of the second active stream */
  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 1);
  fail_if (activeStream == NULL);

  assert_equals_int (activeStream->mimeType, GST_STREAM_AUDIO);
  caps = gst_mpd_client_get_stream_caps (activeStream);
  fail_unless (caps != NULL);
  s = gst_caps_get_structure (caps, 0);
  assert_equals_string (gst_structure_get_name (s), "audio");
  gst_caps_unref (caps);

  /* set to FALSE in our example, but overwritten to TRUE by Period's
   * bitstreamSwitching
   */
  bitstreamSwitchingFlag =
      gst_mpd_client_get_bitstream_switching_flag (activeStream);
  assert_equals_int (bitstreamSwitchingFlag, TRUE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test various duration formats
 */
GST_START_TEST (dash_mpdparser_various_duration_formats)
{
  GstPeriodNode *periodNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P100Y\">"
      "  <Period id=\"Period0\" start=\"PT1S\"></Period>"
      "  <Period id=\"Period1\" start=\"PT1.5S\"></Period>"
      "  <Period id=\"Period2\" start=\"PT1,7S\"></Period>"
      "  <Period id=\"Period3\" start=\"PT1M\"></Period>"
      "  <Period id=\"Period4\" start=\"PT1H\"></Period>"
      "  <Period id=\"Period5\" start=\"P1D\"></Period>"
      "  <Period id=\"Period6\" start=\"P1M\"></Period>"
      "  <Period id=\"Period7\" start=\"P1Y\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  periodNode =
      (GstPeriodNode *) g_list_nth_data (mpdclient->mpd_node->Periods, 0);
  assert_equals_string (periodNode->id, "Period0");
  assert_equals_uint64 (periodNode->start,
      duration_to_ms (0, 0, 0, 0, 0, 1, 0));

  periodNode =
      (GstPeriodNode *) g_list_nth_data (mpdclient->mpd_node->Periods, 1);
  assert_equals_string (periodNode->id, "Period1");
  assert_equals_uint64 (periodNode->start,
      duration_to_ms (0, 0, 0, 0, 0, 1, 500));

  periodNode =
      (GstPeriodNode *) g_list_nth_data (mpdclient->mpd_node->Periods, 2);
  assert_equals_string (periodNode->id, "Period2");
  assert_equals_uint64 (periodNode->start,
      duration_to_ms (0, 0, 0, 0, 0, 1, 700));

  periodNode =
      (GstPeriodNode *) g_list_nth_data (mpdclient->mpd_node->Periods, 3);
  assert_equals_string (periodNode->id, "Period3");
  assert_equals_uint64 (periodNode->start,
      duration_to_ms (0, 0, 0, 0, 1, 0, 0));

  periodNode =
      (GstPeriodNode *) g_list_nth_data (mpdclient->mpd_node->Periods, 4);
  assert_equals_string (periodNode->id, "Period4");
  assert_equals_uint64 (periodNode->start,
      duration_to_ms (0, 0, 0, 1, 0, 0, 0));

  periodNode =
      (GstPeriodNode *) g_list_nth_data (mpdclient->mpd_node->Periods, 5);
  assert_equals_string (periodNode->id, "Period5");
  assert_equals_uint64 (periodNode->start,
      duration_to_ms (0, 0, 1, 0, 0, 0, 0));

  periodNode =
      (GstPeriodNode *) g_list_nth_data (mpdclient->mpd_node->Periods, 6);
  assert_equals_string (periodNode->id, "Period6");
  assert_equals_uint64 (periodNode->start,
      duration_to_ms (0, 1, 0, 0, 0, 0, 0));

  periodNode =
      (GstPeriodNode *) g_list_nth_data (mpdclient->mpd_node->Periods, 7);
  assert_equals_string (periodNode->id, "Period7");
  assert_equals_uint64 (periodNode->start,
      duration_to_ms (1, 0, 0, 0, 0, 0, 0));

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test media presentation setup
 *
 */
GST_START_TEST (dash_mpdparser_setup_media_presentation)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"Period0\""
      "          duration=\"P0Y0M1DT1H1M1S\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test setting a stream
 *
 */
GST_START_TEST (dash_mpdparser_setup_streaming)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"Period0\""
      "          duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"1\""
      "                   mimeType=\"video/mp4\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the first adaptation set of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);
  adapt_set = (GstAdaptationSetNode *) adaptationSets->data;
  fail_if (adapt_set == NULL);

  /* setup streaming from the adaptation set */
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling Period selection
 *
 */
GST_START_TEST (dash_mpdparser_period_selection)
{
  const gchar *periodName;
  guint periodIndex;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     mediaPresentationDuration=\"P0Y0M1DT1H4M3S\">"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\"></Period>"
      "  <Period id=\"Period1\"></Period>"
      "  <Period id=\"Period2\" start=\"P0Y0M1DT1H3M3S\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* period_idx should be 0 and we should have no active periods */
  assert_equals_uint64 (mpdclient->period_idx, 0);
  fail_unless (mpdclient->periods == NULL);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* check the periods */
  fail_unless (mpdclient->periods != NULL);
  periodName = gst_mpd_client_get_period_id (mpdclient);
  assert_equals_string (periodName, "Period0");

  ret = gst_mpd_client_set_period_index (mpdclient, 1);
  assert_equals_int (ret, TRUE);
  periodName = gst_mpd_client_get_period_id (mpdclient);
  assert_equals_string (periodName, "Period1");

  ret = gst_mpd_client_set_period_index (mpdclient, 2);
  assert_equals_int (ret, TRUE);
  periodName = gst_mpd_client_get_period_id (mpdclient);
  assert_equals_string (periodName, "Period2");

  ret = gst_mpd_client_has_next_period (mpdclient);
  assert_equals_int (ret, FALSE);
  ret = gst_mpd_client_has_previous_period (mpdclient);
  assert_equals_int (ret, TRUE);

  ret = gst_mpd_client_set_period_index (mpdclient, 0);
  assert_equals_int (ret, TRUE);
  ret = gst_mpd_client_has_next_period (mpdclient);
  assert_equals_int (ret, TRUE);
  ret = gst_mpd_client_has_previous_period (mpdclient);
  assert_equals_int (ret, FALSE);

  ret = gst_mpd_client_set_period_id (mpdclient, "Period1");
  assert_equals_int (ret, TRUE);
  periodIndex = gst_mpd_client_get_period_index (mpdclient);
  assert_equals_uint64 (periodIndex, 1);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling Period selection based on time
 *
 */
GST_START_TEST (dash_mpdparser_get_period_at_time)
{
  guint periodIndex;
  GstDateTime *time;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M1DT1H4M3S\">"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\"></Period>"
      "  <Period id=\"Period1\"></Period>"
      "  <Period id=\"Period2\" start=\"P0Y0M1DT1H3M3S\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* request period for a time before availabilityStartTime, expect period index 0 */
  time = gst_date_time_new_from_iso8601_string ("2015-03-23T23:30:59Z");
  periodIndex = gst_mpd_client_get_period_index_at_time (mpdclient, time);
  gst_date_time_unref (time);
  assert_equals_int (periodIndex, 0);

  /* request period for a time from period 0 */
  time = gst_date_time_new_from_iso8601_string ("2015-03-24T23:30:59Z");
  periodIndex = gst_mpd_client_get_period_index_at_time (mpdclient, time);
  gst_date_time_unref (time);
  assert_equals_int (periodIndex, 0);

  /* request period for a time from period 1 */
  time = gst_date_time_new_from_iso8601_string ("2015-03-25T1:1:1Z");
  periodIndex = gst_mpd_client_get_period_index_at_time (mpdclient, time);
  gst_date_time_unref (time);
  assert_equals_int (periodIndex, 1);

  /* request period for a time from period 2 */
  time = gst_date_time_new_from_iso8601_string ("2015-03-25T1:3:3Z");
  periodIndex = gst_mpd_client_get_period_index_at_time (mpdclient, time);
  gst_date_time_unref (time);
  assert_equals_int (periodIndex, 2);

  /* request period for a time after mediaPresentationDuration, expect period index G_MAXUINT */
  time = gst_date_time_new_from_iso8601_string ("2015-03-25T1:4:3Z");
  periodIndex = gst_mpd_client_get_period_index_at_time (mpdclient, time);
  gst_date_time_unref (time);
  assert_equals_int (periodIndex, G_MAXUINT);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling Adaptation sets
 *
 */
GST_START_TEST (dash_mpdparser_adaptationSet_handling)
{
  const gchar *periodName;
  guint adaptation_sets_count;
  GList *adaptationSets, *it;
  guint count = 0;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"1\"></AdaptationSet>"
      "  </Period>"
      "  <Period id=\"Period1\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"10\"></AdaptationSet>"
      "    <AdaptationSet id=\"11\"></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* period0 has 1 adaptation set */
  fail_unless (mpdclient->periods != NULL);
  periodName = gst_mpd_client_get_period_id (mpdclient);
  assert_equals_string (periodName, "Period0");
  adaptation_sets_count = gst_mpdparser_get_nb_adaptationSet (mpdclient);
  assert_equals_int (adaptation_sets_count, 1);

  /* period1 has 2 adaptation set */
  ret = gst_mpd_client_set_period_id (mpdclient, "Period1");
  assert_equals_int (ret, TRUE);
  adaptation_sets_count = gst_mpdparser_get_nb_adaptationSet (mpdclient);
  assert_equals_int (adaptation_sets_count, 2);

  /* check the id for the 2 adaptation sets from period 1 */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  for (it = adaptationSets; it; it = g_list_next (it)) {
    GstAdaptationSetNode *adapt_set;
    adapt_set = (GstAdaptationSetNode *) it->data;
    fail_if (adapt_set == NULL);

    assert_equals_int (adapt_set->id, 10 + count);
    count++;
  }

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
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"1\" mimeType=\"video/mp4\">"
      "      <Representation id=\"v0\" bandwidth=\"500000\"></Representation>"
      "      <Representation id=\"v1\" bandwidth=\"250000\"></Representation>"
      "    </AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
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
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 0, 0, 0, 0,
      1);
  assert_equals_int (represendationIndex, 1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 100000, 0,
      0, 0, 1);
  assert_equals_int (represendationIndex, -1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 300000, 0,
      0, 0, 1);
  assert_equals_int (represendationIndex, 1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 500000, 0,
      0, 0, 1);
  assert_equals_int (represendationIndex, 0);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling Active stream selection
 *
 */
GST_START_TEST (dash_mpdparser_activeStream_selection)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  guint activeStreams;
  GstActiveStream *activeStream;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"1\" mimeType=\"video/mp4\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet id=\"2\" mimeType=\"audio\">"
      "      <Representation id=\"2\" bandwidth=\"250000\">"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet id=\"3\" mimeType=\"application\">"
      "      <Representation id=\"3\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* no active streams yet */
  activeStreams = gst_mpdparser_get_nb_active_stream (mpdclient);
  assert_equals_int (activeStreams, 0);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  /* 1 active streams */
  activeStreams = gst_mpdparser_get_nb_active_stream (mpdclient);
  assert_equals_int (activeStreams, 1);

  /* setup streaming from the second adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 1);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  /* 2 active streams */
  activeStreams = gst_mpdparser_get_nb_active_stream (mpdclient);
  assert_equals_int (activeStreams, 2);

  /* setup streaming from the third adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 2);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  /* 3 active streams */
  activeStreams = gst_mpdparser_get_nb_active_stream (mpdclient);
  assert_equals_int (activeStreams, 3);

  /* get details of the first active stream */
  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);
  assert_equals_int (activeStream->mimeType, GST_STREAM_VIDEO);

  /* get details of the second active stream */
  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 1);
  fail_if (activeStream == NULL);
  assert_equals_int (activeStream->mimeType, GST_STREAM_AUDIO);

  /* get details of the third active stream */
  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 2);
  fail_if (activeStream == NULL);
  assert_equals_int (activeStream->mimeType, GST_STREAM_APPLICATION);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test getting Active stream parameters
 *
 */
GST_START_TEST (dash_mpdparser_activeStream_parameters)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  guint activeStreams;
  GstActiveStream *activeStream;
  GstCaps *caps;
  GstStructure *s;
  gboolean bitstreamSwitchingFlag;
  guint videoStreamWidth;
  guint videoStreamHeight;
  guint audioStreamRate;
  guint audioChannelsCount;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"Period0\""
      "          duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"1\""
      "                   mimeType=\"video/mp4\""
      "                   width=\"320\""
      "                   height=\"240\""
      "                   bitstreamSwitching=\"true\""
      "                   audioSamplingRate=\"48000\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  /* 1 active streams */
  activeStreams = gst_mpdparser_get_nb_active_stream (mpdclient);
  assert_equals_int (activeStreams, 1);

  /* get details of the first active stream */
  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);

  assert_equals_int (activeStream->mimeType, GST_STREAM_VIDEO);
  caps = gst_mpd_client_get_stream_caps (activeStream);
  fail_unless (caps != NULL);
  s = gst_caps_get_structure (caps, 0);
  assert_equals_string (gst_structure_get_name (s), "video/quicktime");
  gst_caps_unref (caps);

  bitstreamSwitchingFlag =
      gst_mpd_client_get_bitstream_switching_flag (activeStream);
  assert_equals_int (bitstreamSwitchingFlag, 1);

  videoStreamWidth = gst_mpd_client_get_video_stream_width (activeStream);
  assert_equals_int (videoStreamWidth, 320);

  videoStreamHeight = gst_mpd_client_get_video_stream_height (activeStream);
  assert_equals_int (videoStreamHeight, 240);

  audioStreamRate = gst_mpd_client_get_audio_stream_rate (activeStream);
  assert_equals_int (audioStreamRate, 48000);

  audioChannelsCount =
      gst_mpd_client_get_audio_stream_num_channels (activeStream);
  assert_equals_int (audioChannelsCount, 0);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test getting number and list of audio languages
 *
 */
GST_START_TEST (dash_mpdparser_get_audio_languages)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  guint activeStreams;
  guint adaptationSetsCount;
  GList *languages = NULL;
  guint languagesCount;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"1\" mimeType=\"audio\" lang=\"en\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet id=\"2\" mimeType=\"video/mp4\">"
      "      <Representation id=\"2\" bandwidth=\"250000\">"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet id=\"3\" mimeType=\"audio\" lang=\"fr\">"
      "      <Representation id=\"3\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();
  gint i;

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from all adaptation sets */
  adaptationSetsCount = gst_mpdparser_get_nb_adaptationSet (mpdclient);
  for (i = 0; i < adaptationSetsCount; i++) {
    adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, i);
    fail_if (adapt_set == NULL);
    ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
    assert_equals_int (ret, TRUE);
  }
  activeStreams = gst_mpdparser_get_nb_active_stream (mpdclient);
  assert_equals_int (activeStreams, adaptationSetsCount);

  languagesCount =
      gst_mpdparser_get_list_and_nb_of_audio_language (mpdclient, &languages);
  assert_equals_int (languagesCount, 2);
  assert_equals_string ((gchar *) g_list_nth_data (languages, 0), "en");
  assert_equals_string ((gchar *) g_list_nth_data (languages, 1), "fr");

  g_list_free (languages);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Tests getting the base URL
 *
 */
static GstMpdClient *
setup_mpd_client (const gchar * xml)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  guint activeStreams;
  guint adaptationSetsCount;
  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();
  gint i;

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from all adaptation sets */
  adaptationSetsCount = gst_mpdparser_get_nb_adaptationSet (mpdclient);
  for (i = 0; i < adaptationSetsCount; i++) {
    adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, i);
    fail_if (adapt_set == NULL);
    ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
    assert_equals_int (ret, TRUE);
  }
  activeStreams = gst_mpdparser_get_nb_active_stream (mpdclient);
  assert_equals_int (activeStreams, adaptationSetsCount);

  return mpdclient;
}

GST_START_TEST (dash_mpdparser_get_baseURL1)
{
  const gchar *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <BaseURL>http://example.com/</BaseURL>"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"1\" mimeType=\"audio\" lang=\"en\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstMpdClient *mpdclient = setup_mpd_client (xml);

  baseURL = gst_mpdparser_get_baseURL (mpdclient, 0);
  fail_if (baseURL == NULL);
  assert_equals_string (baseURL, "http://example.com/");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;


GST_START_TEST (dash_mpdparser_get_baseURL2)
{
  const gchar *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <BaseURL>mpd_base_url/</BaseURL>"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <BaseURL> /period_base_url/</BaseURL>"
      "    <AdaptationSet id=\"1\" mimeType=\"audio\" lang=\"en\">"
      "      <BaseURL>adaptation_base_url</BaseURL>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <BaseURL>representation_base_url</BaseURL>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstMpdClient *mpdclient = setup_mpd_client (xml);

  /* test baseURL. Its value should be computed like this:
   *  - start with xml url (null)
   *  - set it to the value from MPD's BaseURL element: "mpd_base_url/"
   *  - update the value with BaseURL element from Period. Because Period's
   * baseURL is absolute (starts with /) it will overwrite the current value
   * for baseURL. So, baseURL becomes "/period_base_url/"
   *  - update the value with BaseURL element from AdaptationSet. Because this
   * is a relative url, it will update the current value. baseURL becomes
   * "/period_base_url/adaptation_base_url"
   *  - update the value with BaseURL element from Representation. Because this
   * is a relative url, it will update the current value. Because the current
   * value does not end in /, everything after the last / will be overwritten.
   * baseURL becomes "/period_base_url/representation_base_url"
   */
  baseURL = gst_mpdparser_get_baseURL (mpdclient, 0);
  fail_if (baseURL == NULL);
  assert_equals_string (baseURL, "/period_base_url/representation_base_url");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;


GST_START_TEST (dash_mpdparser_get_baseURL3)
{
  const gchar *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <BaseURL>mpd_base_url/</BaseURL>"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <BaseURL> /period_base_url/</BaseURL>"
      "    <AdaptationSet id=\"1\" mimeType=\"audio\" lang=\"en\">"
      "      <BaseURL>adaptation_base_url</BaseURL>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <BaseURL>/representation_base_url</BaseURL>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstMpdClient *mpdclient = setup_mpd_client (xml);

  /* test baseURL. Its value should be computed like this:
   *  - start with xml url (null)
   *  - set it to the value from MPD's BaseURL element: "mpd_base_url/"
   *  - update the value with BaseURL element from Period. Because Period's
   * baseURL is absolute (starts with /) it will overwrite the current value
   * for baseURL. So, baseURL becomes "/period_base_url/"
   *  - update the value with BaseURL element from AdaptationSet. Because this
   * is a relative url, it will update the current value. baseURL becomes
   * "/period_base_url/adaptation_base_url"
   *  - update the value with BaseURL element from Representation. Because this
   * is an absolute url, it will replace everything again"
   */
  baseURL = gst_mpdparser_get_baseURL (mpdclient, 0);
  fail_if (baseURL == NULL);
  assert_equals_string (baseURL, "/representation_base_url");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;


GST_START_TEST (dash_mpdparser_get_baseURL4)
{
  const gchar *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <BaseURL>mpd_base_url/</BaseURL>"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <BaseURL> /period_base_url/</BaseURL>"
      "    <AdaptationSet id=\"1\" mimeType=\"audio\" lang=\"en\">"
      "      <BaseURL>adaptation_base_url/</BaseURL>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <BaseURL>representation_base_url/</BaseURL>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstMpdClient *mpdclient = setup_mpd_client (xml);

  /* test baseURL. Its value should be computed like this:
   *  - start with xml url (null)
   *  - set it to the value from MPD's BaseURL element: "mpd_base_url/"
   *  - update the value with BaseURL element from Period. Because Period's
   * baseURL is absolute (starts with /) it will overwrite the current value
   * for baseURL. So, baseURL becomes "/period_base_url/"
   *  - update the value with BaseURL element from AdaptationSet. Because this
   * is a relative url, it will update the current value. baseURL becomes
   * "/period_base_url/adaptation_base_url/"
   *  - update the value with BaseURL element from Representation. Because this
   * is an relative url, it will update the current value."
   */
  baseURL = gst_mpdparser_get_baseURL (mpdclient, 0);
  fail_if (baseURL == NULL);
  assert_equals_string (baseURL,
      "/period_base_url/adaptation_base_url/representation_base_url/");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/* test multiple BaseUrl entries per section */
GST_START_TEST (dash_mpdparser_get_baseURL5)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  const gchar *baseURL;
  GstBaseURL *gstBaseURL;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <BaseURL>/mpd_base_url1/</BaseURL>"
      "  <BaseURL>/mpd_base_url2/</BaseURL>"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <BaseURL> period_base_url1/</BaseURL>"
      "    <BaseURL> period_base_url2/</BaseURL>"
      "    <BaseURL> period_base_url3/</BaseURL>"
      "    <AdaptationSet id=\"1\" mimeType=\"audio\" lang=\"en\">"
      "      <BaseURL>adaptation_base_url1/</BaseURL>"
      "      <BaseURL>adaptation_base_url2/</BaseURL>"
      "      <BaseURL>adaptation_base_url3/</BaseURL>"
      "      <BaseURL>adaptation_base_url4/</BaseURL>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <BaseURL>representation_base_url1/</BaseURL>"
      "        <BaseURL>representation_base_url2/</BaseURL>"
      "        <BaseURL>representation_base_url3/</BaseURL>"
      "        <BaseURL>representation_base_url4/</BaseURL>"
      "        <BaseURL>representation_base_url5/</BaseURL>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstMpdClient *mpdclient = setup_mpd_client (xml);

  assert_equals_int (g_list_length (mpdclient->mpd_node->BaseURLs), 2);
  gstBaseURL = g_list_nth_data (mpdclient->mpd_node->BaseURLs, 0);
  assert_equals_string (gstBaseURL->baseURL, "/mpd_base_url1/");
  gstBaseURL = g_list_nth_data (mpdclient->mpd_node->BaseURLs, 1);
  assert_equals_string (gstBaseURL->baseURL, "/mpd_base_url2/");

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  assert_equals_int (g_list_length (periodNode->BaseURLs), 3);
  gstBaseURL = g_list_nth_data (periodNode->BaseURLs, 0);
  assert_equals_string (gstBaseURL->baseURL, " period_base_url1/");
  gstBaseURL = g_list_nth_data (periodNode->BaseURLs, 1);
  assert_equals_string (gstBaseURL->baseURL, " period_base_url2/");
  gstBaseURL = g_list_nth_data (periodNode->BaseURLs, 2);
  assert_equals_string (gstBaseURL->baseURL, " period_base_url3/");

  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  assert_equals_int (g_list_length (adaptationSet->BaseURLs), 4);
  gstBaseURL = g_list_nth_data (adaptationSet->BaseURLs, 0);
  assert_equals_string (gstBaseURL->baseURL, "adaptation_base_url1/");
  gstBaseURL = g_list_nth_data (adaptationSet->BaseURLs, 1);
  assert_equals_string (gstBaseURL->baseURL, "adaptation_base_url2/");
  gstBaseURL = g_list_nth_data (adaptationSet->BaseURLs, 2);
  assert_equals_string (gstBaseURL->baseURL, "adaptation_base_url3/");
  gstBaseURL = g_list_nth_data (adaptationSet->BaseURLs, 3);
  assert_equals_string (gstBaseURL->baseURL, "adaptation_base_url4/");

  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  assert_equals_int (g_list_length (representation->BaseURLs), 5);
  gstBaseURL = g_list_nth_data (representation->BaseURLs, 0);
  assert_equals_string (gstBaseURL->baseURL, "representation_base_url1/");
  gstBaseURL = g_list_nth_data (representation->BaseURLs, 1);
  assert_equals_string (gstBaseURL->baseURL, "representation_base_url2/");
  gstBaseURL = g_list_nth_data (representation->BaseURLs, 2);
  assert_equals_string (gstBaseURL->baseURL, "representation_base_url3/");
  gstBaseURL = g_list_nth_data (representation->BaseURLs, 3);
  assert_equals_string (gstBaseURL->baseURL, "representation_base_url4/");
  gstBaseURL = g_list_nth_data (representation->BaseURLs, 4);
  assert_equals_string (gstBaseURL->baseURL, "representation_base_url5/");

  /* test baseURL. Its value should be computed like this:
   *  - start with xml url (null)
   *  - set it to the value from MPD's BaseURL element: "/mpd_base_url1/"
   *  - update the value with BaseURL element from Period. Because this
   * is a relative url, it will update the current value. baseURL becomes
   * "/mpd_base_url1/period_base_url1/"
   *  - update the value with BaseURL element from AdaptationSet. Because this
   * is a relative url, it will update the current value. baseURL becomes
   * "/mpd_base_url1/period_base_url1/adaptation_base_url1/"
   *  - update the value with BaseURL element from Representation. Because this
   * is an relative url, it will update the current value."
   */
  baseURL = gst_mpdparser_get_baseURL (mpdclient, 0);
  fail_if (baseURL == NULL);
  assert_equals_string (baseURL,
      "/mpd_base_url1/period_base_url1/adaptation_base_url1/representation_base_url1/");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/* test no BaseURL */
GST_START_TEST (dash_mpdparser_get_baseURL6)
{
  const gchar *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"1\" mimeType=\"audio\" lang=\"en\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstMpdClient *mpdclient = setup_mpd_client (xml);

  baseURL = gst_mpdparser_get_baseURL (mpdclient, 0);
  fail_if (baseURL == NULL);
  assert_equals_string (baseURL, "");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/* BaseURL: test that the path is made absolute (a / is prepended if needed */
GST_START_TEST (dash_mpdparser_get_baseURL7)
{
  const gchar *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <BaseURL>x/example.com/</BaseURL>"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"1\" mimeType=\"audio\" lang=\"en\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstMpdClient *mpdclient;

  mpdclient = setup_mpd_client (xml);

  baseURL = gst_mpdparser_get_baseURL (mpdclient, 0);
  fail_if (baseURL == NULL);
  assert_equals_string (baseURL, "/x/example.com/");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/* BaseURL: test that a / is not prepended if the string contains ':'
 * This tests uris with schema present */
GST_START_TEST (dash_mpdparser_get_baseURL8)
{
  const gchar *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <BaseURL>x:y/example.com/</BaseURL>"
      "  <Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "    <AdaptationSet id=\"1\" mimeType=\"audio\" lang=\"en\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstMpdClient *mpdclient = setup_mpd_client (xml);

  baseURL = gst_mpdparser_get_baseURL (mpdclient, 0);
  fail_if (baseURL == NULL);
  assert_equals_string (baseURL, "x:y/example.com/");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test getting mediaPresentationDuration
 *
 */
GST_START_TEST (dash_mpdparser_get_mediaPresentationDuration)
{
  GstClockTime mediaPresentationDuration;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     mediaPresentationDuration=\"P0Y0M0DT0H0M3S\"></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  mediaPresentationDuration =
      gst_mpd_client_get_media_presentation_duration (mpdclient);
  assert_equals_uint64 (mediaPresentationDuration, 3000000000);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test getting streamPresentationOffset
 *
 */
GST_START_TEST (dash_mpdparser_get_streamPresentationOffset)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  GstClockTime offset;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"video/mp4\">"
      "      <SegmentBase timescale=\"1000\" presentationTimeOffset=\"3000\">"
      "      </SegmentBase>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  /* test the stream presentation time offset */
  offset = gst_mpd_parser_get_stream_presentation_offset (mpdclient, 0);
  /* seems to be set only for template segments, so here it is 0 */
  assert_equals_int (offset, 0);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling segments
 *
 */
GST_START_TEST (dash_mpdparser_segments)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  gboolean hasNextSegment;
  GstActiveStream *activeStream;
  GstFlowReturn flow;
  GstDateTime *segmentAvailability;
  GstDateTime *gst_time;
  GDateTime *g_time;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     type=\"dynamic\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period id=\"Period0\" start=\"P0Y0M0DT0H0M10S\">"
      "    <AdaptationSet mimeType=\"video/mp4\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SegmentList duration=\"45\">"
      "          <SegmentURL media=\"TestMedia1\""
      "                      mediaRange=\"10-20\""
      "                      index=\"TestIndex1\""
      "                      indexRange=\"30-40\">"
      "          </SegmentURL>"
      "          <SegmentURL media=\"TestMedia2\""
      "                      mediaRange=\"20-30\""
      "                      index=\"TestIndex2\""
      "                      indexRange=\"40-50\">"
      "          </SegmentURL>"
      "        </SegmentList>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);

  /* segment_index 0, segment_count 2.
   * Has next segment and can advance to next segment
   */
  hasNextSegment =
      gst_mpd_client_has_next_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (hasNextSegment, 1);
  flow = gst_mpd_client_advance_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (flow, GST_FLOW_OK);

  /* segment_index 1, segment_count 2.
   * Does not have next segment and can not advance to next segment
   */
  hasNextSegment =
      gst_mpd_client_has_next_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (hasNextSegment, 0);
  flow = gst_mpd_client_advance_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (flow, GST_FLOW_EOS);

  /* go to first segment */
  gst_mpd_client_seek_to_first_segment (mpdclient);

  /* segment_index 0, segment_count 2.
   * Has next segment and can advance to next segment
   */
  hasNextSegment =
      gst_mpd_client_has_next_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (hasNextSegment, 1);
  flow = gst_mpd_client_advance_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (flow, GST_FLOW_OK);

  /* segment_index 1, segment_count 2
   * Does not have next segment
   */
  hasNextSegment =
      gst_mpd_client_has_next_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (hasNextSegment, 0);

  /* segment index is still 1 */
  hasNextSegment =
      gst_mpd_client_has_next_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (hasNextSegment, 0);

  /* each segment has a duration of 0 hours, 0 min 45 seconds
   * segment index is 1.
   * Start time is at the beginning of segment 1, so 1 * segment_duration = 1 * 45s
   * Availability start time is at the end of the segment, so we add duration (45s)
   * We also add period start time (10s)
   * So, availability start time for segment 1 is: 10 (period start) +
   * 45 (segment start) + 45 (duration) = 1'40s
   */
  segmentAvailability =
      gst_mpd_client_get_next_segment_availability_start_time (mpdclient,
      activeStream);
  assert_equals_int (gst_date_time_get_year (segmentAvailability), 2015);
  assert_equals_int (gst_date_time_get_month (segmentAvailability), 3);
  assert_equals_int (gst_date_time_get_day (segmentAvailability), 24);
  assert_equals_int (gst_date_time_get_hour (segmentAvailability), 0);
  assert_equals_int (gst_date_time_get_minute (segmentAvailability), 1);
  assert_equals_int (gst_date_time_get_second (segmentAvailability), 40);
  gst_date_time_unref (segmentAvailability);

  /* seek to time */
  gst_time = gst_date_time_new_from_iso8601_string ("2015-03-24T0:0:20Z");
  g_time = gst_date_time_to_g_date_time (gst_time);
  ret = gst_mpd_client_seek_to_time (mpdclient, g_time);
  assert_equals_int (ret, 1);
  gst_date_time_unref (gst_time);
  g_date_time_unref (g_time);

  /* segment index is now 0 */
  hasNextSegment =
      gst_mpd_client_has_next_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (hasNextSegment, 1);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling headers
 *
 */
GST_START_TEST (dash_mpdparser_headers)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  gchar *uri;
  gint64 range_start;
  gint64 range_end;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     type=\"dynamic\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period id=\"Period0\">"
      "    <AdaptationSet mimeType=\"video/mp4\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SegmentBase indexRange=\"10-20\">"
      "          <Initialization sourceURL=\"TestSourceUrl\""
      "                          range=\"100-200\">"
      "          </Initialization>"
      "          <RepresentationIndex sourceURL=\"TestSourceIndex\">"
      "          </RepresentationIndex>"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  /* get segment url and range from segment Initialization */
  ret =
      gst_mpd_client_get_next_header (mpdclient, &uri, 0, &range_start,
      &range_end);
  assert_equals_int (ret, TRUE);
  assert_equals_string (uri, "TestSourceUrl");
  assert_equals_int64 (range_start, 100);
  assert_equals_int64 (range_end, 200);
  g_free (uri);

  /* get segment url and range from segment indexRange */
  ret =
      gst_mpd_client_get_next_header_index (mpdclient, &uri, 0, &range_start,
      &range_end);
  assert_equals_int (ret, TRUE);
  assert_equals_string (uri, "TestSourceIndex");
  assert_equals_int64 (range_start, 10);
  assert_equals_int64 (range_end, 20);
  g_free (uri);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling fragments
 *
 */
GST_START_TEST (dash_mpdparser_fragments)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  GstMediaFragmentInfo fragment;
  GstActiveStream *activeStream;
  GstClockTime nextFragmentDuration;
  GstClockTime nextFragmentTimestamp;
  GstClockTime nextFragmentTimestampEnd;
  GstClockTime periodStartTime;
  GstClockTime expectedDuration;
  GstClockTime expectedTimestamp;
  GstClockTime expectedTimestampEnd;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period id=\"Period0\" start=\"P0Y0M0DT0H0M10S\">"
      "    <AdaptationSet mimeType=\"video/mp4\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);
  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);

  /* expected duration of the next fragment */
  expectedDuration = duration_to_ms (0, 0, 0, 3, 3, 20, 0);
  expectedTimestamp = duration_to_ms (0, 0, 0, 0, 0, 0, 0);
  expectedTimestampEnd = duration_to_ms (0, 0, 0, 3, 3, 20, 0);

  ret = gst_mpd_client_get_next_fragment (mpdclient, 0, &fragment);
  assert_equals_int (ret, TRUE);
  assert_equals_string (fragment.uri, "");
  assert_equals_int64 (fragment.range_start, 0);
  assert_equals_int64 (fragment.range_end, -1);
  assert_equals_uint64 (fragment.duration, expectedDuration * GST_MSECOND);
  assert_equals_uint64 (fragment.timestamp, expectedTimestamp * GST_MSECOND);
  gst_media_fragment_info_clear (&fragment);

  periodStartTime = gst_mpd_parser_get_period_start_time (mpdclient);
  assert_equals_uint64 (periodStartTime, 10 * GST_SECOND);

  nextFragmentDuration =
      gst_mpd_client_get_next_fragment_duration (mpdclient, activeStream);
  assert_equals_uint64 (nextFragmentDuration, expectedDuration * GST_MSECOND);

  ret =
      gst_mpd_client_get_next_fragment_timestamp (mpdclient, 0,
      &nextFragmentTimestamp);
  assert_equals_int (ret, TRUE);
  assert_equals_uint64 (nextFragmentTimestamp, expectedTimestamp * GST_MSECOND);

  ret =
      gst_mpd_client_get_last_fragment_timestamp_end (mpdclient, 0,
      &nextFragmentTimestampEnd);
  assert_equals_int (ret, TRUE);
  assert_equals_uint64 (nextFragmentTimestampEnd,
      expectedTimestampEnd * GST_MSECOND);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test inheriting segmentBase from parent
 *
 */
GST_START_TEST (dash_mpdparser_inherited_segmentBase)
{
  GstPeriodNode *periodNode;
  GstSegmentBaseType *segmentBase;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period>"
      "    <AdaptationSet>"
      "      <SegmentBase timescale=\"100\">"
      "      </SegmentBase>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SegmentBase timescale=\"200\">"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;

  /* test segment base from adaptation set */
  segmentBase = adaptationSet->SegmentBase;
  assert_equals_uint64 (segmentBase->timescale, 100);

  /* test segment base from representation */
  segmentBase = representation->SegmentBase;
  assert_equals_uint64 (segmentBase->timescale, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test inheriting segmentURL from parent
 *
 */
GST_START_TEST (dash_mpdparser_inherited_segmentURL)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  GstActiveStream *activeStream;
  GstMediaFragmentInfo fragment;
  GstClockTime expectedDuration;
  GstClockTime expectedTimestamp;
  GstFlowReturn flow;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period start=\"P0Y0M0DT0H0M10S\">"
      "    <AdaptationSet mimeType=\"video/mp4\">"
      "      <SegmentList duration=\"100\">"
      "        <SegmentURL media=\"TestMediaAdaptation\""
      "                    mediaRange=\"10-20\""
      "                    index=\"TestIndexAdaptation\""
      "                    indexRange=\"30-40\">"
      "        </SegmentURL>"
      "      </SegmentList>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SegmentList duration=\"110\">"
      "          <SegmentURL media=\"TestMediaRep\""
      "                      mediaRange=\"100-200\""
      "                      index=\"TestIndexRep\""
      "                      indexRange=\"300-400\">"
      "          </SegmentURL>"
      "        </SegmentList>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);

  /* expected duration of the next fragment
   * Segment duration was set to 100 in AdaptationSet and to 110 in Representation
   * We expect duration to be 110
   */
  expectedDuration = duration_to_ms (0, 0, 0, 0, 0, 110, 0);
  expectedTimestamp = duration_to_ms (0, 0, 0, 0, 0, 0, 0);

  /* the representation contains 1 segment (the one from Representation) */

  /* check first segment */
  ret = gst_mpd_client_get_next_fragment (mpdclient, 0, &fragment);
  assert_equals_int (ret, TRUE);
  assert_equals_string (fragment.uri, "/TestMediaRep");
  assert_equals_int64 (fragment.range_start, 100);
  assert_equals_int64 (fragment.range_end, 200);
  assert_equals_string (fragment.index_uri, "/TestIndexRep");
  assert_equals_int64 (fragment.index_range_start, 300);
  assert_equals_int64 (fragment.index_range_end, 400);
  assert_equals_uint64 (fragment.duration, expectedDuration * GST_MSECOND);
  assert_equals_uint64 (fragment.timestamp, expectedTimestamp * GST_MSECOND);
  gst_media_fragment_info_clear (&fragment);

  /* try to advance to next segment. Should fail */
  flow = gst_mpd_client_advance_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (flow, GST_FLOW_EOS);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test segment list
 *
 */
GST_START_TEST (dash_mpdparser_segment_list)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  GstActiveStream *activeStream;
  GstMediaFragmentInfo fragment;
  GstClockTime expectedDuration;
  GstClockTime expectedTimestamp;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period start=\"P0Y0M0DT0H0M10S\">"
      "    <AdaptationSet mimeType=\"video/mp4\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SegmentList duration=\"12000\">"
      "          <SegmentURL media=\"TestMedia\""
      "                      mediaRange=\"100-200\""
      "                      index=\"TestIndex\""
      "                      indexRange=\"300-400\">"
      "          </SegmentURL>"
      "        </SegmentList>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);

  /* expected duration of the next fragment
   * Segment duration was set larger than period duration (12000 vs 11000).
   * We expect it to be limited to period duration.
   */
  expectedDuration = duration_to_ms (0, 0, 0, 3, 3, 20, 0);
  expectedTimestamp = duration_to_ms (0, 0, 0, 0, 0, 0, 0);

  ret = gst_mpd_client_get_next_fragment (mpdclient, 0, &fragment);
  assert_equals_int (ret, TRUE);
  assert_equals_string (fragment.uri, "/TestMedia");
  assert_equals_int64 (fragment.range_start, 100);
  assert_equals_int64 (fragment.range_end, 200);
  assert_equals_string (fragment.index_uri, "/TestIndex");
  assert_equals_int64 (fragment.index_range_start, 300);
  assert_equals_int64 (fragment.index_range_end, 400);
  assert_equals_uint64 (fragment.duration, expectedDuration * GST_MSECOND);
  assert_equals_uint64 (fragment.timestamp, expectedTimestamp * GST_MSECOND);

  gst_media_fragment_info_clear (&fragment);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test segment template
 *
 */
GST_START_TEST (dash_mpdparser_segment_template)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  GstActiveStream *activeStream;
  GstMediaFragmentInfo fragment;
  GstClockTime expectedDuration;
  GstClockTime expectedTimestamp;
  GstClockTime periodStartTime;
  GstClockTime offset;
  GstClockTime lastFragmentTimestampEnd;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period start=\"P0Y0M0DT0H0M10S\">"
      "    <AdaptationSet mimeType=\"video/mp4\">"
      "      <Representation id=\"repId\" bandwidth=\"250000\">"
      "        <SegmentTemplate duration=\"12000\""
      "                         presentationTimeOffset=\"15\""
      "                         media=\"TestMedia_rep=$RepresentationID$number=$Number$bandwidth=$Bandwidth$time=$Time$\""
      "                         index=\"TestIndex\">"
      "        </SegmentTemplate>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);

  /* expected duration of the next fragment
   * Segment duration was set larger than period duration (12000 vs 11000).
   * We expect it to not be limited to period duration.
   */
  expectedDuration = duration_to_ms (0, 0, 0, 0, 0, 12000, 0);

  /* while the period starts at 10ms, the fragment timestamp is supposed to be
   * 0ms. timestamps are starting from 0 at every period, and only the overall
   * composition of periods should consider the period start timestamp. In
   * dashdemux this is done by mapping the 0 fragment timestamp to a stream
   * time equal to the period start time.
   */
  expectedTimestamp = duration_to_ms (0, 0, 0, 0, 0, 0, 0);

  ret = gst_mpd_client_get_next_fragment (mpdclient, 0, &fragment);
  assert_equals_int (ret, TRUE);
  assert_equals_string (fragment.uri,
      "/TestMedia_rep=repIdnumber=1bandwidth=250000time=0");
  assert_equals_int64 (fragment.range_start, 0);
  assert_equals_int64 (fragment.range_end, -1);
  assert_equals_string (fragment.index_uri, "/TestIndex");
  assert_equals_int64 (fragment.index_range_start, 0);
  assert_equals_int64 (fragment.index_range_end, -1);
  assert_equals_uint64 (fragment.duration, expectedDuration * GST_MSECOND);
  assert_equals_uint64 (fragment.timestamp, expectedTimestamp * GST_MSECOND);

  periodStartTime = gst_mpd_parser_get_period_start_time (mpdclient);
  assert_equals_uint64 (periodStartTime, 10 * GST_SECOND);

  offset = gst_mpd_parser_get_stream_presentation_offset (mpdclient, 0);
  assert_equals_uint64 (offset, 15 * GST_SECOND);

  gst_media_fragment_info_clear (&fragment);

  /*
   * Period starts at 10s.
   * MPD has a duration of 3h3m30s, so period duration is 3h3m20s.
   * We expect the last fragment to end at period start + period duration: 3h3m30s
   */
  expectedTimestamp = duration_to_ms (0, 0, 0, 3, 3, 30, 0);
  gst_mpd_client_get_last_fragment_timestamp_end (mpdclient, 0,
      &lastFragmentTimestampEnd);
  assert_equals_uint64 (lastFragmentTimestampEnd,
      expectedTimestamp * GST_MSECOND);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test segment timeline
 *
 */
GST_START_TEST (dash_mpdparser_segment_timeline)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  GstActiveStream *activeStream;
  GstMediaFragmentInfo fragment;
  GstClockTime expectedDuration;
  GstClockTime expectedTimestamp;
  GstFlowReturn flow;
  GstDateTime *segmentAvailability;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period start=\"P0Y0M0DT0H0M10S\">"
      "    <AdaptationSet mimeType=\"video/mp4\">"
      "      <SegmentList>"
      "        <SegmentTimeline>"
      "          <S t=\"10\"  d=\"20\" r=\"30\"></S>"
      "        </SegmentTimeline>"
      "      </SegmentList>"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SegmentList>"
      "          <SegmentTimeline>"
      "            <S t=\"3\"  d=\"2\" r=\"1\"></S>"
      "            <S t=\"10\" d=\"3\" r=\"0\"></S>"
      "          </SegmentTimeline>"
      "          <SegmentURL media=\"TestMedia0\""
      "                      index=\"TestIndex0\">"
      "          </SegmentURL>"
      "          <SegmentURL media=\"TestMedia1\""
      "                      index=\"TestIndex1\">"
      "          </SegmentURL>"
      "        </SegmentList>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);

  /* expected duration of the next fragment */
  expectedDuration = duration_to_ms (0, 0, 0, 0, 0, 2, 0);
  expectedTimestamp = duration_to_ms (0, 0, 0, 0, 0, 3, 0);

  ret = gst_mpd_client_get_next_fragment (mpdclient, 0, &fragment);
  assert_equals_int (ret, TRUE);
  assert_equals_string (fragment.uri, "/TestMedia0");
  assert_equals_string (fragment.index_uri, "/TestIndex0");
  assert_equals_uint64 (fragment.duration, expectedDuration * GST_MSECOND);
  assert_equals_uint64 (fragment.timestamp, expectedTimestamp * GST_MSECOND);
  gst_media_fragment_info_clear (&fragment);

  /* first segment starts at 3s and has a duration of 2s.
   * We also add period start time (10s) so we expect a segment availability
   * start time of 15s
   */
  segmentAvailability =
      gst_mpd_client_get_next_segment_availability_start_time (mpdclient,
      activeStream);
  fail_unless (segmentAvailability != NULL);
  assert_equals_int (gst_date_time_get_year (segmentAvailability), 2015);
  assert_equals_int (gst_date_time_get_month (segmentAvailability), 3);
  assert_equals_int (gst_date_time_get_day (segmentAvailability), 24);
  assert_equals_int (gst_date_time_get_hour (segmentAvailability), 0);
  assert_equals_int (gst_date_time_get_minute (segmentAvailability), 0);
  assert_equals_int (gst_date_time_get_second (segmentAvailability), 15);
  gst_date_time_unref (segmentAvailability);

  /* advance to next segment */
  flow = gst_mpd_client_advance_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (flow, GST_FLOW_OK);

  /* second segment starts after first ends */
  expectedTimestamp = expectedTimestamp + expectedDuration;

  /* check second segment.
   * It is a repeat of first segmentURL, because "r" in SegmentTimeline is 1
   */
  ret = gst_mpd_client_get_next_fragment (mpdclient, 0, &fragment);
  assert_equals_int (ret, TRUE);
  assert_equals_string (fragment.uri, "/TestMedia0");
  assert_equals_string (fragment.index_uri, "/TestIndex0");
  assert_equals_uint64 (fragment.duration, expectedDuration * GST_MSECOND);
  assert_equals_uint64 (fragment.timestamp, expectedTimestamp * GST_MSECOND);
  gst_media_fragment_info_clear (&fragment);

  /* first segment starts at 3s and has a duration of 2s.
   * Second segment starts when the first ends (5s) and has a duration of 2s,
   * so it ends at 7s.
   * We also add period start time (10s) so we expect a segment availability
   * start time of 17s
   */
  segmentAvailability =
      gst_mpd_client_get_next_segment_availability_start_time (mpdclient,
      activeStream);
  fail_unless (segmentAvailability != NULL);
  assert_equals_int (gst_date_time_get_year (segmentAvailability), 2015);
  assert_equals_int (gst_date_time_get_month (segmentAvailability), 3);
  assert_equals_int (gst_date_time_get_day (segmentAvailability), 24);
  assert_equals_int (gst_date_time_get_hour (segmentAvailability), 0);
  assert_equals_int (gst_date_time_get_minute (segmentAvailability), 0);
  assert_equals_int (gst_date_time_get_second (segmentAvailability), 17);
  gst_date_time_unref (segmentAvailability);

  /* advance to next segment */
  flow = gst_mpd_client_advance_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (flow, GST_FLOW_OK);

  /* third segment has a small gap after the second ends  (t=10) */
  expectedDuration = duration_to_ms (0, 0, 0, 0, 0, 3, 0);
  expectedTimestamp = duration_to_ms (0, 0, 0, 0, 0, 10, 0);

  /* check third segment */
  ret = gst_mpd_client_get_next_fragment (mpdclient, 0, &fragment);
  assert_equals_int (ret, TRUE);
  assert_equals_string (fragment.uri, "/TestMedia1");
  assert_equals_string (fragment.index_uri, "/TestIndex1");
  assert_equals_uint64 (fragment.duration, expectedDuration * GST_MSECOND);
  assert_equals_uint64 (fragment.timestamp, expectedTimestamp * GST_MSECOND);
  gst_media_fragment_info_clear (&fragment);

  /* Third segment starts at 10s and has a duration of 3s so it ends at 13s.
   * We also add period start time (10s) so we expect a segment availability
   * start time of 23s
   */
  segmentAvailability =
      gst_mpd_client_get_next_segment_availability_start_time (mpdclient,
      activeStream);
  fail_unless (segmentAvailability != NULL);
  assert_equals_int (gst_date_time_get_year (segmentAvailability), 2015);
  assert_equals_int (gst_date_time_get_month (segmentAvailability), 3);
  assert_equals_int (gst_date_time_get_day (segmentAvailability), 24);
  assert_equals_int (gst_date_time_get_hour (segmentAvailability), 0);
  assert_equals_int (gst_date_time_get_minute (segmentAvailability), 0);
  assert_equals_int (gst_date_time_get_second (segmentAvailability), 23);
  gst_date_time_unref (segmentAvailability);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test SegmentList with multiple inherited segmentURLs
 *
 */
GST_START_TEST (dash_mpdparser_multiple_inherited_segmentURL)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  GstActiveStream *activeStream;
  GstMediaFragmentInfo fragment;
  GstClockTime expectedDuration;
  GstClockTime expectedTimestamp;
  GstFlowReturn flow;

  /*
   * Period duration is 30 seconds
   * Period start is 10 seconds. Thus, period duration is 20 seconds.
   *
   * There are 2 segments in the AdaptationSet segment list and 2 in the
   * Representation's segment list.
   * Segment duration is 5s for the Adaptation segments and 8s for
   * Representation segments.
   * Separately, each segment list (duration 2*5=10 or 2*8=16) fits comfortably
   * in the Period's 20s duration.
   *
   * We expect the Representation segments to overwrite the AdaptationSet segments.
   */
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      " availabilityStartTime=\"2015-03-24T0:0:0\""
      " mediaPresentationDuration=\"P0Y0M0DT0H0M30S\">"
      "<Period start=\"P0Y0M0DT0H0M10S\">"
      "  <AdaptationSet mimeType=\"video/mp4\">"
      "    <SegmentList duration=\"5\">"
      "      <SegmentURL"
      "         media=\"TestMedia0\" mediaRange=\"10-20\""
      "         index=\"TestIndex0\" indexRange=\"100-200\""
      "      ></SegmentURL>"
      "      <SegmentURL"
      "         media=\"TestMedia1\" mediaRange=\"20-30\""
      "         index=\"TestIndex1\" indexRange=\"200-300\""
      "      ></SegmentURL>"
      "    </SegmentList>"
      "    <Representation id=\"1\" bandwidth=\"250000\">"
      "      <SegmentList duration=\"8\">"
      "        <SegmentURL"
      "           media=\"TestMedia2\" mediaRange=\"30-40\""
      "           index=\"TestIndex2\" indexRange=\"300-400\""
      "        ></SegmentURL>"
      "        <SegmentURL"
      "           media=\"TestMedia3\" mediaRange=\"40-50\""
      "           index=\"TestIndex3\" indexRange=\"400-500\""
      "        ></SegmentURL>"
      "      </SegmentList>"
      "    </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret = gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);

  expectedDuration = duration_to_ms (0, 0, 0, 0, 0, 8, 0);
  expectedTimestamp = duration_to_ms (0, 0, 0, 0, 0, 0, 0);

  /* the representation contains 2 segments defined in the Representation
   *
   * Both will have the duration specified in the Representation (8)
   */

  /* check first segment */
  ret = gst_mpd_client_get_next_fragment (mpdclient, 0, &fragment);
  assert_equals_int (ret, TRUE);
  assert_equals_string (fragment.uri, "/TestMedia2");
  assert_equals_int64 (fragment.range_start, 30);
  assert_equals_int64 (fragment.range_end, 40);
  assert_equals_string (fragment.index_uri, "/TestIndex2");
  assert_equals_int64 (fragment.index_range_start, 300);
  assert_equals_int64 (fragment.index_range_end, 400);
  assert_equals_uint64 (fragment.duration, expectedDuration * GST_MSECOND);
  assert_equals_uint64 (fragment.timestamp, expectedTimestamp * GST_MSECOND);
  gst_media_fragment_info_clear (&fragment);

  /* advance to next segment */
  flow = gst_mpd_client_advance_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (flow, GST_FLOW_OK);

  /* second segment starts after previous ends */
  expectedTimestamp = expectedTimestamp + expectedDuration;

  /* check second segment */
  ret = gst_mpd_client_get_next_fragment (mpdclient, 0, &fragment);
  assert_equals_int (ret, TRUE);
  assert_equals_string (fragment.uri, "/TestMedia3");
  assert_equals_int64 (fragment.range_start, 40);
  assert_equals_int64 (fragment.range_end, 50);
  assert_equals_string (fragment.index_uri, "/TestIndex3");
  assert_equals_int64 (fragment.index_range_start, 400);
  assert_equals_int64 (fragment.index_range_end, 500);
  assert_equals_uint64 (fragment.duration, expectedDuration * GST_MSECOND);
  assert_equals_uint64 (fragment.timestamp, expectedTimestamp * GST_MSECOND);
  gst_media_fragment_info_clear (&fragment);

  /* try to advance to the next segment. There isn't any, so it should fail */
  flow = gst_mpd_client_advance_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (flow, GST_FLOW_EOS);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test SegmentList with multiple segmentURL
 *
 */
GST_START_TEST (dash_mpdparser_multipleSegmentURL)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;
  GstActiveStream *activeStream;
  GstMediaFragmentInfo fragment;
  GstClockTime expectedDuration;
  GstClockTime expectedTimestamp;
  GstFlowReturn flow;

  /*
   * Period duration is 30 seconds
   * Period start is 10 seconds. Thus, period duration is 20 seconds.
   *
   * Segment duration is 25 seconds. There are 2 segments in the list.
   * We expect first segment to have a duration of 20 seconds (limited by the period)
   * and the second segment to not exist.
   */
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      " availabilityStartTime=\"2015-03-24T0:0:0\""
      " mediaPresentationDuration=\"P0Y0M0DT0H0M30S\">"
      "<Period start=\"P0Y0M0DT0H0M10S\">"
      "  <AdaptationSet mimeType=\"video/mp4\">"
      "    <Representation id=\"1\" bandwidth=\"250000\">"
      "      <SegmentList duration=\"25\">"
      "        <SegmentURL"
      "           media=\"TestMedia0\" mediaRange=\"10-20\""
      "           index=\"TestIndex0\" indexRange=\"100-200\""
      "        ></SegmentURL>"
      "        <SegmentURL"
      "           media=\"TestMedia1\" mediaRange=\"20-30\""
      "           index=\"TestIndex1\" indexRange=\"200-300\""
      "        ></SegmentURL>"
      "      </SegmentList>"
      "    </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  /* setup streaming from the first adaptation set */
  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, TRUE);

  activeStream = gst_mpdparser_get_active_stream_by_index (mpdclient, 0);
  fail_if (activeStream == NULL);

  expectedDuration = duration_to_ms (0, 0, 0, 0, 0, 20, 0);
  expectedTimestamp = duration_to_ms (0, 0, 0, 0, 0, 0, 0);

  /* the representation contains 2 segments. The first is partially
   * clipped, and the second entirely (and thus discarded).
   */

  /* check first segment */
  ret = gst_mpd_client_get_next_fragment (mpdclient, 0, &fragment);
  assert_equals_int (ret, TRUE);
  assert_equals_string (fragment.uri, "/TestMedia0");
  assert_equals_int64 (fragment.range_start, 10);
  assert_equals_int64 (fragment.range_end, 20);
  assert_equals_string (fragment.index_uri, "/TestIndex0");
  assert_equals_int64 (fragment.index_range_start, 100);
  assert_equals_int64 (fragment.index_range_end, 200);
  assert_equals_uint64 (fragment.duration, expectedDuration * GST_MSECOND);
  assert_equals_uint64 (fragment.timestamp, expectedTimestamp * GST_MSECOND);
  gst_media_fragment_info_clear (&fragment);

  /* advance to next segment */
  flow = gst_mpd_client_advance_segment (mpdclient, activeStream, TRUE);
  assert_equals_int (flow, GST_FLOW_EOS);

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

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
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

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
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
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"> </NPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
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
 * Test handling wrong period duration during attempts to
 * infer a period duration from the start time of the next period
 */
GST_START_TEST (dash_mpdparser_wrong_period_duration_inferred_from_next_period)
{
  const gchar *periodName;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period id=\"Period0\" duration=\"P0Y0M0DT1H1M0S\"></Period>"
      "  <Period id=\"Period1\"></Period>"
      "  <Period id=\"Period2\" start=\"P0Y0M0DT0H0M10S\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* period_idx should be 0 and we should have no active periods */
  assert_equals_uint64 (mpdclient->period_idx, 0);
  fail_unless (mpdclient->periods == NULL);

  /* process the xml data */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* Period0 should be present */
  fail_unless (mpdclient->periods != NULL);
  periodName = gst_mpd_client_get_period_id (mpdclient);
  assert_equals_string (periodName, "Period0");

  /* Period1 should not be present due to wrong duration */
  ret = gst_mpd_client_set_period_index (mpdclient, 1);
  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling wrong period duration during attempts to
 * infer a period duration from the mediaPresentationDuration
 */
GST_START_TEST
    (dash_mpdparser_wrong_period_duration_inferred_from_next_mediaPresentationDuration)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period id=\"Period0\" start=\"P0Y0M0DT4H0M0S\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* period_idx should be 0 and we should have no active periods */
  assert_equals_uint64 (mpdclient->period_idx, 0);
  fail_unless (mpdclient->periods == NULL);

  /* process the xml data
   * should fail due to wrong duration in Period0 (start > mediaPresentationDuration)
   */
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

GST_START_TEST (dash_mpdparser_whitespace_strings)
{
  fail_unless (gst_mpdparser_validate_no_whitespace ("") == TRUE);
  fail_unless (gst_mpdparser_validate_no_whitespace ("/") == TRUE);
  fail_unless (gst_mpdparser_validate_no_whitespace (" ") == FALSE);
  fail_unless (gst_mpdparser_validate_no_whitespace ("aaaaaaaa ") == FALSE);
  fail_unless (gst_mpdparser_validate_no_whitespace ("a\ta") == FALSE);
  fail_unless (gst_mpdparser_validate_no_whitespace ("a\ra") == FALSE);
  fail_unless (gst_mpdparser_validate_no_whitespace ("a\na") == FALSE);
}

GST_END_TEST;

GST_START_TEST (dash_mpdparser_rfc1738_strings)
{
  fail_unless (gst_mpdparser_validate_rfc1738_url ("/") == TRUE);
  fail_unless (gst_mpdparser_validate_rfc1738_url (" ") == FALSE);
  fail_unless (gst_mpdparser_validate_rfc1738_url ("aaaaaaaa ") == FALSE);

  fail_unless (gst_mpdparser_validate_rfc1738_url ("") == TRUE);
  fail_unless (gst_mpdparser_validate_rfc1738_url ("a") == TRUE);
  fail_unless (gst_mpdparser_validate_rfc1738_url
      (";:@&=aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ0123456789$-_.+!*'(),%AA")
      == TRUE);
  fail_unless (gst_mpdparser_validate_rfc1738_url
      (";:@&=aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ0123456789$-_.+!*'(),/%AA")
      == TRUE);
  fail_unless (gst_mpdparser_validate_rfc1738_url
      (";:@&=aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ0123456789$-_.+!*'(),% ")
      == FALSE);
  fail_unless (gst_mpdparser_validate_rfc1738_url ("%AA") == TRUE);
  fail_unless (gst_mpdparser_validate_rfc1738_url ("%A") == FALSE);
  fail_unless (gst_mpdparser_validate_rfc1738_url ("%") == FALSE);
  fail_unless (gst_mpdparser_validate_rfc1738_url ("%XA") == FALSE);
  fail_unless (gst_mpdparser_validate_rfc1738_url ("%AX") == FALSE);
  fail_unless (gst_mpdparser_validate_rfc1738_url ("%XX") == FALSE);
  fail_unless (gst_mpdparser_validate_rfc1738_url ("\001") == FALSE);
}

GST_END_TEST;

/*
 * Test negative period duration
 */
GST_START_TEST (dash_mpdparser_negative_period_duration)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period id=\"Period0\""
      "          start=\"P0Y0M0DT1H0M0S\""
      "          duration=\"-PT10S\">"
      "  </Period><Period id=\"Period1\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data
   * should fail due to negative duration of Period0
   */
  ret = gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing negative values from attributes that should be unsigned
 *
 */
GST_START_TEST (dash_mpdparser_read_unsigned_from_negative_values)
{
  GstPeriodNode *periodNode;
  GstSegmentBaseType *segmentBase;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSubRepresentationNode *subRepresentation;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015--1-13T12:25:37\">"
      "  <Period start=\"-P-2015Y\" duration=\"-P-5M\">"
      "    <SegmentBase presentationTimeOffset=\"-10\""
      "                 timescale=\"-5\""
      "                 indexRange=\"1--10\">"
      "    </SegmentBase>"
      "    <AdaptationSet par=\"-1:7\""
      "                   minFrameRate=\" -1\""
      "                   segmentAlignment=\"-4\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SubRepresentation dependencyLevel=\"1 -2 3\">"
      "        </SubRepresentation>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentBase = periodNode->SegmentBase;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  subRepresentation = (GstSubRepresentationNode *)
      representation->SubRepresentations->data;

  /* availabilityStartTime parsing should fail */
  fail_if (mpdclient->mpd_node->availabilityStartTime != NULL);

  /* Period start parsing should fail */
  assert_equals_int64 (periodNode->start, -1);

  /* Period duration parsing should fail */
  assert_equals_int64 (periodNode->duration, -1);

  /* expect negative value to be rejected and presentationTimeOffset to be 0 */
  assert_equals_uint64 (segmentBase->presentationTimeOffset, 0);
  assert_equals_uint64 (segmentBase->timescale, 1);
  fail_if (segmentBase->indexRange != NULL);

  /* par ratio parsing should fail */
  fail_if (adaptationSet->par != NULL);

  /* minFrameRate parsing should fail */
  fail_if (adaptationSet->RepresentationBase->minFrameRate != NULL);

  /* segmentAlignment parsing should fail */
  fail_if (adaptationSet->segmentAlignment != NULL);

  /* dependency level parsing should fail */
  fail_if (subRepresentation->dependencyLevel != NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test negative mediaPresentationDuration duration
 */
GST_START_TEST (dash_mpdparser_negative_mediaPresentationDuration)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"-P0Y0M0DT3H3M30S\">"
      "  <Period id=\"Period0\" start=\"P0Y0M0DT1H0M0S\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data
   * should fail due to negative duration of mediaPresentationDuration
   */
  ret = gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing an MPD with no profiles
 */
GST_START_TEST (dash_mpdparser_no_profiles)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\"></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test S node list greater than SegmentURL list
 *
 */
GST_START_TEST (dash_mpdparser_unmatched_segmentTimeline_segmentURL)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adapt_set;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "  <Period start=\"P0Y0M0DT0H0M10S\">"
      "    <AdaptationSet mimeType=\"video/mp4\">"
      "      <Representation id=\"1\" bandwidth=\"250000\">"
      "        <SegmentList>"
      "          <SegmentTimeline>"
      "            <S t=\"3\"  d=\"2\" r=\"1\"></S>"
      "            <S t=\"10\" d=\"3\" r=\"0\"></S>"
      "          </SegmentTimeline>"
      "          <SegmentURL media=\"TestMedia0\""
      "                      index=\"TestIndex0\">"
      "          </SegmentURL>"
      "        </SegmentList>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret = gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  /* get the list of adaptation sets of the first period */
  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  adapt_set = (GstAdaptationSetNode *) g_list_nth_data (adaptationSets, 0);
  fail_if (adapt_set == NULL);

  /* setup streaming from the first adaptation set.
   * Should fail because the second S node does not have a  matching
   * SegmentURL node
   */
  ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set);
  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing of the default presentation delay property
 */
GST_START_TEST (dash_mpdparser_default_presentation_delay)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     maxSegmentDuration=\"PT2S\">"
      "  <Period id=\"Period0\" start=\"P0S\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();
  gint64 value;

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);
  value = gst_mpd_client_parse_default_presentation_delay (mpdclient, "5s");
  assert_equals_int64 (value, 5000);
  value = gst_mpd_client_parse_default_presentation_delay (mpdclient, "5S");
  assert_equals_int64 (value, 5000);
  value =
      gst_mpd_client_parse_default_presentation_delay (mpdclient, "5 seconds");
  assert_equals_int64 (value, 5000);
  value = gst_mpd_client_parse_default_presentation_delay (mpdclient, "2500ms");
  assert_equals_int64 (value, 2500);
  value = gst_mpd_client_parse_default_presentation_delay (mpdclient, "3f");
  assert_equals_int64 (value, 6000);
  value = gst_mpd_client_parse_default_presentation_delay (mpdclient, "3F");
  assert_equals_int64 (value, 6000);
  value = gst_mpd_client_parse_default_presentation_delay (mpdclient, "");
  assert_equals_int64 (value, 0);
  value = gst_mpd_client_parse_default_presentation_delay (mpdclient, "10");
  assert_equals_int64 (value, 0);
  value =
      gst_mpd_client_parse_default_presentation_delay (mpdclient,
      "not a number");
  assert_equals_int64 (value, 0);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

GST_START_TEST (dash_mpdparser_duration)
{
  guint64 v;

  fail_unless (gst_mpdparser_parse_duration ("", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration (" ", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("0", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("D-1", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("T", &v) == FALSE);

  fail_unless (gst_mpdparser_parse_duration ("P", &v) == TRUE);
  fail_unless (gst_mpdparser_parse_duration ("PT", &v) == TRUE);
  fail_unless (gst_mpdparser_parse_duration ("PX", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PPT", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PTT", &v) == FALSE);

  fail_unless (gst_mpdparser_parse_duration ("P1D", &v) == TRUE);
  fail_unless (gst_mpdparser_parse_duration ("P1D1D", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("P1D1M", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("P1M1D", &v) == TRUE);
  fail_unless (gst_mpdparser_parse_duration ("P1M1D1M", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("P1M1D1D", &v) == FALSE);

  fail_unless (gst_mpdparser_parse_duration ("P0M0D", &v) == TRUE);
  fail_unless (gst_mpdparser_parse_duration ("P-1M", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("P15M", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("P-1D", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("P35D", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("P-1Y", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PT-1H", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PT25H", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PT-1M", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PT65M", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PT-1S", &v) == FALSE);
  /* seconds are allowed to be larger than 60 */
  fail_unless (gst_mpdparser_parse_duration ("PT65S", &v) == TRUE);

  fail_unless (gst_mpdparser_parse_duration ("PT1.1H", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PT1-1H", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PT1-H", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PT-H", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PTH", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PT0", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("PT1.1S", &v) == TRUE);
  fail_unless (gst_mpdparser_parse_duration ("PT1.1.1S", &v) == FALSE);

  fail_unless (gst_mpdparser_parse_duration ("P585Y", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("P584Y", &v) == TRUE);

  fail_unless (gst_mpdparser_parse_duration (" P10DT8H", &v) == TRUE);
  fail_unless (gst_mpdparser_parse_duration ("P10D T8H", &v) == FALSE);
  fail_unless (gst_mpdparser_parse_duration ("P10DT8H ", &v) == TRUE);
}

GST_END_TEST;

/*
 * Test that the maximum_segment_duration correctly implements the
 * rules in the DASH specification
 */
GST_START_TEST (dash_mpdparser_maximum_segment_duration)
{
  const gchar *xml_template =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     availabilityStartTime=\"2015-03-24T0:0:0\""
      "     %s "
      "     mediaPresentationDuration=\"P100Y\">"
      "  <Period id=\"Period0\" start=\"PT0S\">"
      "    <AdaptationSet mimeType=\"video/mp4\" >"
      "      <SegmentTemplate timescale=\"90000\" initialization=\"$RepresentationID$/Header.m4s\" media=\"$RepresentationID$/$Number$.m4s\" duration=\"360000\" />"
      "      <Representation id=\"video1\" width=\"576\" height=\"324\" frameRate=\"25\" sar=\"1:1\" bandwidth=\"900000\" codecs=\"avc1.4D401E\"/>"
      "    </AdaptationSet>"
      "      <AdaptationSet mimeType=\"audio/mp4\" >"
      "        <SegmentTemplate timescale=\"90000\" initialization=\"$RepresentationID$/Header.m4s\" media=\"$RepresentationID$/$Number$.m4s\" duration=\"340000\" />"
      "        <Representation id=\"audio1\" audioSamplingRate=\"22050\" bandwidth=\"29600\" codecs=\"mp4a.40.2\">"
      "        <AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"2\"/>"
      "      </Representation>" "    </AdaptationSet>" "  </Period></MPD>";
  gboolean ret;
  GstMpdClient *mpdclient;
  gchar *xml;
  GstClockTime dur;
  GList *adapt_sets, *iter;

  xml = g_strdup_printf (xml_template, "maxSegmentDuration=\"PT4.5S\"");
  mpdclient = gst_mpd_client_new ();
  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  g_free (xml);
  assert_equals_int (ret, TRUE);

  assert_equals_uint64 (mpdclient->mpd_node->maxSegmentDuration,
      duration_to_ms (0, 0, 0, 0, 0, 4, 500));
  dur = gst_mpd_client_get_maximum_segment_duration (mpdclient);
  assert_equals_uint64 (dur, duration_to_clocktime (0, 0, 0, 0, 0, 4, 500));
  gst_mpd_client_free (mpdclient);

  /* now parse without the maxSegmentDuration attribute, to check that
     gst_mpd_client_get_maximum_segment_duration uses the maximum
     duration of any segment
   */
  xml = g_strdup_printf (xml_template, "");
  mpdclient = gst_mpd_client_new ();
  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  g_free (xml);
  assert_equals_int (ret, TRUE);
  ret =
      gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);
  adapt_sets = gst_mpd_client_get_adaptation_sets (mpdclient);
  for (iter = adapt_sets; iter; iter = g_list_next (iter)) {
    GstAdaptationSetNode *adapt_set_node = iter->data;

    ret = gst_mpd_client_setup_streaming (mpdclient, adapt_set_node);
    assert_equals_int (ret, TRUE);
  }
  dur = gst_mpd_client_get_maximum_segment_duration (mpdclient);
  assert_equals_uint64 (dur, duration_to_clocktime (0, 0, 0, 0, 0, 4, 0));
  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing of Perioud using @xlink:href attribute
 */

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_ (x)
#define REMOTEDIR STRINGIFY (DASH_MPD_DATADIR)
#define XLINK_SINGLE_PERIOD_FILENAME REMOTEDIR "/xlink_single_period.period"
#define XLINK_DOUBLE_PERIOD_FILENAME REMOTEDIR "/xlink_double_period.period"

GST_START_TEST (dash_mpdparser_xlink_period)
{
  GstPeriodNode *periodNode;
  GstUriDownloader *downloader;
  GstMpdClient *mpdclient;
  GList *period_list, *iter;
  gboolean ret;
  gchar *xml_joined, *file_uri_single_period, *file_uri_double_period;
  const gchar *xml_frag_start =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "  <Period id=\"Period0\" duration=\"PT5S\"></Period>";

  const gchar *xml_uri_front = "  <Period xlink:href=\"";

  const gchar *xml_uri_rear =
      "\""
      "          xlink:actuate=\"onRequest\""
      "          xmlns:xlink=\"http://www.w3.org/1999/xlink\"></Period>";

  const gchar *xml_frag_end = "</MPD>";

  /* XLINK_ONE_PERIOD_FILENAME
   *
   * <Period id="xlink-single-period-Period1" duration="PT10S" xmlns="urn:mpeg:dash:schema:mpd:2011"></Period>
   */

  /* XLINK_TWO_PERIODS_FILENAME
   *
   * <Period id="xlink-double-period-Period1" duration="PT10S" xmlns="urn:mpeg:dash:schema:mpd:2011"></Period>
   * <Period id="xlink-double-period-Period2" duration="PT20S" xmlns="urn:mpeg:dash:schema:mpd:2011"></Period>
   */


  mpdclient = gst_mpd_client_new ();
  downloader = gst_uri_downloader_new ();

  gst_mpd_client_set_uri_downloader (mpdclient, downloader);

  file_uri_single_period =
      gst_filename_to_uri (XLINK_SINGLE_PERIOD_FILENAME, NULL);
  file_uri_double_period =
      gst_filename_to_uri (XLINK_DOUBLE_PERIOD_FILENAME, NULL);

  /* constructs inital mpd using external xml uri */
  /* For invalid URI, mpdparser should be ignore it */
  xml_joined = g_strjoin ("", xml_frag_start,
      xml_uri_front, "http://404/ERROR/XML.period", xml_uri_rear,
      xml_uri_front, (const char *) file_uri_single_period, xml_uri_rear,
      xml_uri_front, (const char *) file_uri_double_period, xml_uri_rear,
      xml_frag_end, NULL);

  ret = gst_mpd_parse (mpdclient, xml_joined, (gint) strlen (xml_joined));
  assert_equals_int (ret, TRUE);

  period_list = mpdclient->mpd_node->Periods;
  /* only count periods on initial mpd (external xml does not parsed yet) */
  assert_equals_int (g_list_length (period_list), 4);

  /* process the xml data */
  ret = gst_mpd_client_setup_media_presentation (mpdclient, GST_CLOCK_TIME_NONE,
      -1, NULL);
  assert_equals_int (ret, TRUE);

  period_list = mpdclient->mpd_node->Periods;
  assert_equals_int (g_list_length (period_list), 4);

  iter = period_list;
  periodNode = (GstPeriodNode *) iter->data;
  assert_equals_string (periodNode->id, "Period0");

  iter = iter->next;
  periodNode = (GstPeriodNode *) iter->data;
  assert_equals_string (periodNode->id, "xlink-single-period-Period1");

  iter = iter->next;
  periodNode = (GstPeriodNode *) iter->data;
  assert_equals_string (periodNode->id, "xlink-double-period-Period1");

  iter = iter->next;
  periodNode = (GstPeriodNode *) iter->data;
  assert_equals_string (periodNode->id, "xlink-double-period-Period2");

  gst_mpd_client_free (mpdclient);
  g_object_unref (downloader);
  g_free (file_uri_single_period);
  g_free (file_uri_double_period);
  g_free (xml_joined);
}

GST_END_TEST;


/*
 * Test parsing xsd:datetime with timezoneoffset.
 *
 */
GST_START_TEST (dash_mpdparser_datetime_with_tz_offset)
{
  GstDateTime *availabilityStartTime;
  GstDateTime *availabilityEndTime;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      "     schemaLocation=\"TestSchemaLocation\""
      "     xmlns:xsi=\"TestNamespaceXSI\""
      "     xmlns:ext=\"TestNamespaceEXT\""
      "     id=\"testId\""
      "     type=\"static\""
      "     availabilityStartTime=\"2015-03-24T1:10:50+08:00\""
      "     availabilityEndTime=\"2015-03-24T1:10:50.123456-04:30\""
      "     mediaPresentationDuration=\"P0Y1M2DT12H10M20.5S\""
      "     minimumUpdatePeriod=\"P0Y1M2DT12H10M20.5S\""
      "     minBufferTime=\"P0Y1M2DT12H10M20.5S\""
      "     timeShiftBufferDepth=\"P0Y1M2DT12H10M20.5S\""
      "     suggestedPresentationDelay=\"P0Y1M2DT12H10M20.5S\""
      "     maxSegmentDuration=\"P0Y1M2DT12H10M20.5S\""
      "     maxSubsegmentDuration=\"P0Y1M2DT12H10M20.5S\"></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  availabilityStartTime = mpdclient->mpd_node->availabilityStartTime;
  assert_equals_int (gst_date_time_get_year (availabilityStartTime), 2015);
  assert_equals_int (gst_date_time_get_month (availabilityStartTime), 3);
  assert_equals_int (gst_date_time_get_day (availabilityStartTime), 24);
  assert_equals_int (gst_date_time_get_hour (availabilityStartTime), 1);
  assert_equals_int (gst_date_time_get_minute (availabilityStartTime), 10);
  assert_equals_int (gst_date_time_get_second (availabilityStartTime), 50);
  assert_equals_int (gst_date_time_get_microsecond (availabilityStartTime), 0);
  assert_equals_float (gst_date_time_get_time_zone_offset (availabilityStartTime), 8.0);

  availabilityEndTime = mpdclient->mpd_node->availabilityEndTime;
  assert_equals_int (gst_date_time_get_year (availabilityEndTime), 2015);
  assert_equals_int (gst_date_time_get_month (availabilityEndTime), 3);
  assert_equals_int (gst_date_time_get_day (availabilityEndTime), 24);
  assert_equals_int (gst_date_time_get_hour (availabilityEndTime), 1);
  assert_equals_int (gst_date_time_get_minute (availabilityEndTime), 10);
  assert_equals_int (gst_date_time_get_second (availabilityEndTime), 50);
  assert_equals_int (gst_date_time_get_microsecond (availabilityEndTime),
      123456);
  assert_equals_float (gst_date_time_get_time_zone_offset (availabilityEndTime), -4.5);

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
  TCase *tc_stringTests = tcase_create ("stringTests");
  TCase *tc_duration = tcase_create ("duration");

  GST_DEBUG_CATEGORY_INIT (gst_dash_demux_debug, "gst_dash_demux_debug", 0,
      "mpeg dash tests");

  /* test parsing the simplest possible mpd */
  tcase_add_test (tc_simpleMPD, dash_mpdparser_validsimplempd);

  /* tests parsing attributes from each element type */
  tcase_add_test (tc_simpleMPD, dash_mpdparser_mpd);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_datetime_with_tz_offset);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_programInformation);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_baseURL);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_location);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_metrics);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_metrics_range);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_metrics_reporting);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_baseURL);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_segmentBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentBase_initialization);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentBase_representationIndex);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_segmentList);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentList_multipleSegmentBaseType);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentBaseType);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentTimeline);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentTimeline_s);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentList_multipleSegmentBaseType_bitstreamSwitching);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_segmentList_segmentURL);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_segmentTemplate);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentBaseType);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentTimeline);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentTimeline_s);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_bitstreamSwitching);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_adaptationSet);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representationBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representationBase_framePacking);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_adapt_repr_segmentTemplate_inherit);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representationBase_audioChannelConfiguration);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representationBase_contentProtection);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_contentProtection_no_value);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_contentProtection_no_value_no_encoding);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_accessibility);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_adaptationSet_role);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_adaptationSet_rating);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_adaptationSet_viewpoint);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_contentComponent);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_contentComponent_accessibility);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_contentComponent_role);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_contentComponent_rating);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_contentComponent_viewpoint);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_adaptationSet_baseURL);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentBase_initialization);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentBase_representationIndex);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentList);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentTemplate);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentTemplate_inherit);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_representationBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_baseURL);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_subRepresentation);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_subRepresentation_representationBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_segmentBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_segmentList);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_segmentTemplate);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_segmentTemplate_inherit);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_segmentBase_inherit);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_subset);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_utctiming);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_utctiming_invalid_value);

  /* tests checking other possible values for attributes */
  tcase_add_test (tc_simpleMPD, dash_mpdparser_type_dynamic);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_template_parsing);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_isoff_ondemand_profile);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_GstDateTime);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_bitstreamSwitching_inheritance);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_various_duration_formats);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_default_presentation_delay);

  /* tests checking xlink attributes */
  tcase_add_test (tc_simpleMPD, dash_mpdparser_xlink_period);

  /* tests checking the MPD management
   * (eg. setting active streams, obtaining attributes values)
   */
  tcase_add_test (tc_complexMPD, dash_mpdparser_setup_media_presentation);
  tcase_add_test (tc_complexMPD, dash_mpdparser_setup_streaming);
  tcase_add_test (tc_complexMPD, dash_mpdparser_period_selection);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_period_at_time);
  tcase_add_test (tc_complexMPD, dash_mpdparser_adaptationSet_handling);
  tcase_add_test (tc_complexMPD, dash_mpdparser_representation_selection);
  tcase_add_test (tc_complexMPD, dash_mpdparser_multipleSegmentURL);
  tcase_add_test (tc_complexMPD, dash_mpdparser_activeStream_selection);
  tcase_add_test (tc_complexMPD, dash_mpdparser_activeStream_parameters);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_audio_languages);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_baseURL1);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_baseURL2);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_baseURL3);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_baseURL4);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_baseURL5);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_baseURL6);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_baseURL7);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_baseURL8);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_mediaPresentationDuration);
  tcase_add_test (tc_complexMPD, dash_mpdparser_get_streamPresentationOffset);
  tcase_add_test (tc_complexMPD, dash_mpdparser_segments);
  tcase_add_test (tc_complexMPD, dash_mpdparser_headers);
  tcase_add_test (tc_complexMPD, dash_mpdparser_fragments);
  tcase_add_test (tc_complexMPD, dash_mpdparser_inherited_segmentBase);
  tcase_add_test (tc_complexMPD, dash_mpdparser_inherited_segmentURL);
  tcase_add_test (tc_complexMPD, dash_mpdparser_segment_list);
  tcase_add_test (tc_complexMPD, dash_mpdparser_segment_template);
  tcase_add_test (tc_complexMPD, dash_mpdparser_segment_timeline);
  tcase_add_test (tc_complexMPD, dash_mpdparser_multiple_inherited_segmentURL);

  /* tests checking the parsing of missing/incomplete attributes of xml */
  tcase_add_test (tc_negativeTests, dash_mpdparser_missing_xml);
  tcase_add_test (tc_negativeTests, dash_mpdparser_missing_mpd);
  tcase_add_test (tc_negativeTests, dash_mpdparser_no_end_tag);
  tcase_add_test (tc_negativeTests, dash_mpdparser_no_profiles);
  tcase_add_test (tc_negativeTests, dash_mpdparser_no_default_namespace);
  tcase_add_test (tc_negativeTests,
      dash_mpdparser_wrong_period_duration_inferred_from_next_period);
  tcase_add_test (tc_negativeTests,
      dash_mpdparser_wrong_period_duration_inferred_from_next_mediaPresentationDuration);
  tcase_add_test (tc_negativeTests, dash_mpdparser_negative_period_duration);
  tcase_add_test (tc_negativeTests,
      dash_mpdparser_read_unsigned_from_negative_values);
  tcase_add_test (tc_negativeTests,
      dash_mpdparser_negative_mediaPresentationDuration);
  tcase_add_test (tc_negativeTests,
      dash_mpdparser_unmatched_segmentTimeline_segmentURL);

  tcase_add_test (tc_stringTests, dash_mpdparser_whitespace_strings);
  tcase_add_test (tc_stringTests, dash_mpdparser_rfc1738_strings);

  tcase_add_test (tc_duration, dash_mpdparser_duration);
  tcase_add_test (tc_duration, dash_mpdparser_maximum_segment_duration);

  suite_add_tcase (s, tc_simpleMPD);
  suite_add_tcase (s, tc_complexMPD);
  suite_add_tcase (s, tc_negativeTests);
  suite_add_tcase (s, tc_stringTests);
  suite_add_tcase (s, tc_duration);

  return s;
}

GST_CHECK_MAIN (dash);
