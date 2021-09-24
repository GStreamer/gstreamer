/* *INDENT-OFF* */
/*
 * Copyright (c) 2013-2016 Jan Schmidt <jan@centricular.com>
Portions:
Copyright (c) 2013, Broadcom Europe Ltd
Copyright (c) 2013, James Hughes
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * \file RaspiCapture.c
 *
 * Modification of the RaspiVid command line capture program for GStreamer
 * use.
 *
 * \date 28th Feb 2013, 11 Oct 2013, 5 Mar 2015
 * \Author: James Hughes, Jan Schmidt
 *
 * Description
 *
 * 3 components are created; camera, preview and video encoder.
 * Camera component has three ports, preview, video and stills.
 * This program connects preview and stills to the preview and video
 * encoder. Using mmal we don't need to worry about buffers between these
 * components, but we do need to handle buffers from the encoder, which
 * are simply written straight to the file in the requisite buffer callback.
 *
 * We use the RaspiCamControl code to handle the specific camera settings.
 * We use the RaspiPreview code to handle the (generic) preview window
 */

// We use some GNU extensions (basename, asprintf)
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <sysexits.h>

#include <gst/gst.h>

#include "bcm_host.h"
#include "interface/vcos/vcos.h"

#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_logging.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"

#include "RaspiCapture.h"
#include "RaspiCamControl.h"
#include "RaspiPreview.h"
#include "RaspiCLI.h"

#include <semaphore.h>

// Standard port setting for the camera component
#define MMAL_CAMERA_PREVIEW_PORT 0
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_CAPTURE_PORT 2

// Video format information
// 0 implies variable
#define VIDEO_FRAME_RATE_NUM 30
#define VIDEO_FRAME_RATE_DEN 1

/// Video render needs at least 2 buffers.
#define VIDEO_OUTPUT_BUFFERS_NUM 3

// Max bitrate we allow for recording
const int MAX_BITRATE = 25000000; // 25Mbits/s

/// Interval at which we check for an failure abort during capture
const int ABORT_INTERVAL = 100; // ms


int mmal_status_to_int(MMAL_STATUS_T status);

/** Struct used to pass information in encoder port userdata to callback
 */
typedef struct
{
   RASPIVID_STATE *state; /// pointer to our state in case required in callback
   int abort;             /// Set to 1 in callback if an error occurs to attempt to abort the capture
} PORT_USERDATA;

struct RASPIVID_STATE_T
{
   RASPIVID_CONFIG config;

   FILE *output_file;

   MMAL_COMPONENT_T *camera_component;    /// Pointer to the camera component
   MMAL_COMPONENT_T *encoder_component;   /// Pointer to the encoder component
   MMAL_CONNECTION_T *preview_connection; /// Pointer to the connection from camera to preview
   MMAL_CONNECTION_T *encoder_connection; /// Pointer to the connection from camera to encoder

   MMAL_PORT_T *camera_video_port;
   MMAL_PORT_T *camera_still_port;
   MMAL_PORT_T *encoder_output_port;

   MMAL_POOL_T *encoder_pool; /// Pointer to the pool of buffers used by encoder output port

   PORT_USERDATA callback_data;

   MMAL_QUEUE_T *encoded_buffer_q;

   int64_t base_time;
   int64_t last_second;

   RASPIPREVIEW_STATE preview_state;
};


/// Structure to cross reference H264 profile strings against the MMAL parameter equivalent
static XREF_T  profile_map[] =
{
   {"baseline",     MMAL_VIDEO_PROFILE_H264_BASELINE},
   {"main",         MMAL_VIDEO_PROFILE_H264_MAIN},
   {"high",         MMAL_VIDEO_PROFILE_H264_HIGH},
//   {"constrained",  MMAL_VIDEO_PROFILE_H264_CONSTRAINED_BASELINE} // Does anyone need this?
};

static int profile_map_size = sizeof(profile_map) / sizeof(profile_map[0]);

#if 0
static XREF_T  initial_map[] =
{
   {"record",     0},
   {"pause",      1},
};

static int initial_map_size = sizeof(initial_map) / sizeof(initial_map[0]);
#endif

static XREF_T  intra_refresh_map[] =
{
   {"cyclic",       MMAL_VIDEO_INTRA_REFRESH_CYCLIC},
   {"adaptive",     MMAL_VIDEO_INTRA_REFRESH_ADAPTIVE},
   {"both",         MMAL_VIDEO_INTRA_REFRESH_BOTH},
   {"cyclicrows",   MMAL_VIDEO_INTRA_REFRESH_CYCLIC_MROWS},
//   {"random",       MMAL_VIDEO_INTRA_REFRESH_PSEUDO_RAND} Cannot use random, crashes the encoder. No idea why.
};

static int intra_refresh_map_size = sizeof(intra_refresh_map) / sizeof(intra_refresh_map[0]);

#if 0

static void display_valid_parameters(char *app_name);

/// Command ID's and Structure defining our command line options
#define CommandHelp         0
#define CommandWidth        1
#define CommandHeight       2
#define CommandBitrate      3
#define CommandOutput       4
#define CommandVerbose      5
#define CommandTimeout      6
#define CommandDemoMode     7
#define CommandFramerate    8
#define CommandPreviewEnc   9
#define CommandIntraPeriod  10
#define CommandProfile      11
#define CommandTimed        12
#define CommandSignal       13
#define CommandKeypress     14
#define CommandInitialState 15
#define CommandQP           16
#define CommandInlineHeaders 17
#define CommandSegmentFile  18
#define CommandSegmentWrap  19
#define CommandSegmentStart 20
#define CommandSplitWait    21
#define CommandCircular     22
#define CommandIMV          23
#define CommandCamSelect    24
#define CommandSettings     25
#define CommandSensorMode   26
#define CommandIntraRefreshType 27

static COMMAND_LIST cmdline_commands[] =
{
   { CommandHelp,          "-help",       "?",  "This help information", 0 },
   { CommandWidth,         "-width",      "w",  "Set image width <size>. Default 1920", 1 },
   { CommandHeight,        "-height",     "h",  "Set image height <size>. Default 1080", 1 },
   { CommandBitrate,       "-bitrate",    "b",  "Set bitrate. Use bits per second (e.g. 10MBits/s would be -b 10000000)", 1 },
   { CommandOutput,        "-output",     "o",  "Output filename <filename> (to write to stdout, use '-o -')", 1 },
   { CommandVerbose,       "-verbose",    "v",  "Output verbose information during run", 0 },
   { CommandTimeout,       "-timeout",    "t",  "Time (in ms) to capture for. If not specified, set to 5s. Zero to disable", 1 },
   { CommandDemoMode,      "-demo",       "d",  "Run a demo mode (cycle through range of camera options, no capture)", 1},
   { CommandFramerate,     "-framerate",  "fps","Specify the frames per second to record", 1},
   { CommandPreviewEnc,    "-penc",       "e",  "Display preview image *after* encoding (shows compression artifacts)", 0},
   { CommandIntraPeriod,   "-intra",      "g",  "Specify the intra refresh period (key frame rate/GoP size). Zero to produce an initial I-frame and then just P-frames.", 1},
   { CommandProfile,       "-profile",    "pf", "Specify H264 profile to use for encoding", 1},
   { CommandTimed,         "-timed",      "td", "Cycle between capture and pause. -cycle on,off where on is record time and off is pause time in ms", 0},
   { CommandSignal,        "-signal",     "s",  "Cycle between capture and pause on Signal", 0},
   { CommandKeypress,      "-keypress",   "k",  "Cycle between capture and pause on ENTER", 0},
   { CommandInitialState,  "-initial",    "i",  "Initial state. Use 'record' or 'pause'. Default 'record'", 1},
   { CommandQP,            "-qp",         "qp", "Quantisation parameter. Use approximately 10-40. Default 0 (off)", 1},
   { CommandInlineHeaders, "-inline",     "ih", "Insert inline headers (SPS, PPS) to stream", 0},
   { CommandSegmentFile,   "-segment",    "sg", "Segment output file in to multiple files at specified interval <ms>", 1},
   { CommandSegmentWrap,   "-wrap",       "wr", "In segment mode, wrap any numbered filename back to 1 when reach number", 1},
   { CommandSegmentStart,  "-start",      "sn", "In segment mode, start with specified segment number", 1},
   { CommandSplitWait,     "-split",      "sp", "In wait mode, create new output file for each start event", 0},
   { CommandCircular,      "-circular",   "c",  "Run encoded data through circular buffer until triggered then save", 0},
   { CommandIMV,           "-vectors",    "x",  "Output filename <filename> for inline motion vectors", 1 },
   { CommandCamSelect,     "-camselect",  "cs", "Select camera <number>. Default 0", 1 },
   { CommandSettings,      "-settings",   "set","Retrieve camera settings and write to stdout", 0},
   { CommandSensorMode,    "-mode",       "md", "Force sensor mode. 0=auto. See docs for other modes available", 1},
   { CommandIntraRefreshType,"-irefresh", "if", "Set intra refresh type", 1},
};

static int cmdline_commands_size = sizeof(cmdline_commands) / sizeof(cmdline_commands[0]);
#endif

static void dump_state(RASPIVID_STATE *state);

/**
 * Assign a default set of parameters to the state passed in
 *
 * @param state Pointer to state structure to assign defaults to
 */
void raspicapture_default_config(RASPIVID_CONFIG *config)
{
   if (!config)
   {
      vcos_assert(0);
      return;
   }

   // Default everything to zero
   memset(config, 0, sizeof(RASPIVID_CONFIG));

   // Now set anything non-zero
   config->timeout = 5000;     // 5s delay before take image
   config->width = 1920;       // Default to 1080p
   config->height = 1080;
   config->bitrate = 17000000; // This is a decent default bitrate for 1080p
   config->fps_n = VIDEO_FRAME_RATE_NUM;
   config->fps_d = VIDEO_FRAME_RATE_DEN;
   config->intraperiod = -1;    // Not set
   config->quantisationParameter = 0;
   config->demoMode = 0;
   config->demoInterval = 250; // ms
   config->immutableInput = 1;
   config->profile = MMAL_VIDEO_PROFILE_H264_HIGH;
   config->encoding = MMAL_ENCODING_H264;

   config->bInlineHeaders = 0;

   config->inlineMotionVectors = 0;

   config->cameraNum = 0;
   config->settings = 0;
   config->sensor_mode = 0;

   config->intra_refresh_type = -1;

   // Setup preview window defaults
   raspipreview_set_defaults(&config->preview_parameters);

   // Set up the camera_parameters to default
   raspicamcontrol_set_defaults(&config->camera_parameters);

}


/**
 * Dump image state parameters to printf. Used for debugging
 *
 * @param state Pointer to state structure to assign defaults to
 */
static void dump_state(RASPIVID_STATE *state)
{
   RASPIVID_CONFIG *config;

   if (!state)
   {
      vcos_assert(0);
      return;
   }

   config = &state->config;

   fprintf(stderr, "Width %d, Height %d\n", config->width, config->height);
   fprintf(stderr, "bitrate %d, framerate %d/%d, time delay %d\n",
       config->bitrate, config->fps_n, config->fps_d, config->timeout);
   //fprintf(stderr, "H264 Profile %s\n", raspicli_unmap_xref(config->profile, profile_map, profile_map_size));

   raspipreview_dump_parameters(&config->preview_parameters);
   raspicamcontrol_dump_parameters(&config->camera_parameters);
}

#if 0
/**
 * Parse the incoming command line and put resulting parameters in to the state
 *
 * @param argc Number of arguments in command line
 * @param argv Array of pointers to strings from command line
 * @param state Pointer to state structure to assign any discovered parameters to
 * @return Non-0 if failed for some reason, 0 otherwise
 */
static int parse_cmdline(int argc, const char **argv, RASPIVID_STATE *state)
{
   // Parse the command line arguments.
   // We are looking for --<something> or -<abreviation of something>

   int valid = 1;
   int i;

   for (i = 1; i < argc && valid; i++)
   {
      int command_id, num_parameters;

      if (!argv[i])
         continue;

      if (argv[i][0] != '-')
      {
         valid = 0;
         continue;
      }

      // Assume parameter is valid until proven otherwise
      valid = 1;

      command_id = raspicli_get_command_id(cmdline_commands, cmdline_commands_size, &argv[i][1], &num_parameters);

      // If we found a command but are missing a parameter, continue (and we will drop out of the loop)
      if (command_id != -1 && num_parameters > 0 && (i + 1 >= argc) )
         continue;

      //  We are now dealing with a command line option
      switch (command_id)
      {
      case CommandHelp:
         display_valid_parameters(basename(argv[0]));
         return -1;

      case CommandWidth: // Width > 0
         if (sscanf(argv[i + 1], "%u", &state->width) != 1)
            valid = 0;
         else
            i++;
         break;

      case CommandHeight: // Height > 0
         if (sscanf(argv[i + 1], "%u", &state->height) != 1)
            valid = 0;
         else
            i++;
         break;

      case CommandBitrate: // 1-100
         if (sscanf(argv[i + 1], "%u", &state->bitrate) == 1)
         {
            if (state->bitrate > MAX_BITRATE)
            {
               state->bitrate = MAX_BITRATE;
            }
            i++;
         }
         else
            valid = 0;

         break;

      case CommandOutput:  // output filename
      {
         int len = strlen(argv[i + 1]);
         if (len)
         {
            state->filename = malloc(len + 1);
            vcos_assert(state->filename);
            if (state->filename)
               strncpy(state->filename, argv[i + 1], len+1);
            i++;
         }
         else
            valid = 0;
         break;
      }

      case CommandVerbose: // display lots of data during run
         state->verbose = 1;
         break;

      case CommandTimeout: // Time to run viewfinder/capture
      {
         if (sscanf(argv[i + 1], "%u", &state->timeout) == 1)
         {
            // Ensure that if previously selected a waitMethod we dont overwrite it
            if (state->timeout == 0 && state->waitMethod == WAIT_METHOD_NONE)
               state->waitMethod = WAIT_METHOD_FOREVER;

            i++;
         }
         else
            valid = 0;
         break;
      }

      case CommandDemoMode: // Run in demo mode - no capture
      {
         // Demo mode might have a timing parameter
         // so check if a) we have another parameter, b) its not the start of the next option
         if (i + 1 < argc  && argv[i+1][0] != '-')
         {
            if (sscanf(argv[i + 1], "%u", &state->demoInterval) == 1)
            {
               // TODO : What limits do we need for timeout?
               if (state->demoInterval == 0)
                  state->demoInterval = 250; // ms

               state->demoMode = 1;
               i++;
            }
            else
               valid = 0;
         }
         else
         {
            state->demoMode = 1;
         }

         break;
      }

      case CommandFramerate: // fps to record
      {
         if (sscanf(argv[i + 1], "%u", &state->framerate) == 1)
         {
            // TODO : What limits do we need for fps 1 - 30 - 120??
            i++;
         }
         else
            valid = 0;
         break;
      }

      case CommandPreviewEnc:
         state->immutableInput = 0;
         break;

      case CommandIntraPeriod: // key frame rate
      {
         if (sscanf(argv[i + 1], "%u", &state->intraperiod) == 1)
            i++;
         else
            valid = 0;
         break;
      }

      case CommandQP: // quantisation parameter
      {
         if (sscanf(argv[i + 1], "%u", &state->quantisationParameter) == 1)
            i++;
         else
            valid = 0;
         break;
      }

      case CommandProfile: // H264 profile
      {
         state->profile = raspicli_map_xref(argv[i + 1], profile_map, profile_map_size);

         if( state->profile == -1)
            state->profile = MMAL_VIDEO_PROFILE_H264_HIGH;

         i++;
         break;
      }

      case CommandInlineHeaders: // H264 inline headers
      {
         state->bInlineHeaders = 1;
         break;
      }

      case CommandTimed:
      {
         if (sscanf(argv[i + 1], "%u,%u", &state->onTime, &state->offTime) == 2)
         {
            i++;

            if (state->onTime < 1000)
               state->onTime = 1000;

            if (state->offTime < 1000)
               state->offTime = 1000;

            state->waitMethod = WAIT_METHOD_TIMED;
         }
         else
            valid = 0;
         break;
      }

      case CommandKeypress:
         state->waitMethod = WAIT_METHOD_KEYPRESS;
         break;

      case CommandSignal:
         state->waitMethod = WAIT_METHOD_SIGNAL;
         // Reenable the signal
         signal(SIGUSR1, signal_handler);
         break;

      case CommandInitialState:
      {
         state->bCapturing = raspicli_map_xref(argv[i + 1], initial_map, initial_map_size);

         if( state->bCapturing == -1)
            state->bCapturing = 0;

         i++;
         break;
      }

      case CommandSegmentFile: // Segment file in to chunks of specified time
      {
         if (sscanf(argv[i + 1], "%u", &state->segmentSize) == 1)
         {
            // Must enable inline headers for this to work
            state->bInlineHeaders = 1;
            i++;
         }
         else
            valid = 0;
         break;
      }

      case CommandSegmentWrap: // segment wrap value
      {
         if (sscanf(argv[i + 1], "%u", &state->segmentWrap) == 1)
            i++;
         else
            valid = 0;
         break;
      }

      case CommandSegmentStart: // initial segment number
      {
         if((sscanf(argv[i + 1], "%u", &state->segmentNumber) == 1) && (!state->segmentWrap || (state->segmentNumber <= state->segmentWrap)))
            i++;
         else
            valid = 0;
         break;
      }

      case CommandSplitWait: // split files on restart
      {
         // Must enable inline headers for this to work
         state->bInlineHeaders = 1;
         state->splitWait = 1;
         break;
      }

      case CommandCircular:
      {
         state->bCircularBuffer = 1;
         break;
      }

      case CommandIMV:  // output filename
      {
         state->inlineMotionVectors = 1;
         int len = strlen(argv[i + 1]);
         if (len)
         {
            state->imv_filename = malloc(len + 1);
            vcos_assert(state->imv_filename);
            if (state->imv_filename)
               strncpy(state->imv_filename, argv[i + 1], len+1);
            i++;
         }
         else
            valid = 0;
         break;
      }
      case CommandCamSelect:  //Select camera input port
      {
         if (sscanf(argv[i + 1], "%u", &state->cameraNum) == 1)
         {
            i++;
         }
         else
            valid = 0;
         break;
      }

      case CommandSettings:
         state->settings = 1;
         break;

      case CommandSensorMode:
      {
         if (sscanf(argv[i + 1], "%u", &state->sensor_mode) == 1)
         {
            i++;
         }
         else
            valid = 0;
         break;
      }

      case CommandIntraRefreshType:
      {
         state->config.intra_refresh_type = raspicli_map_xref(argv[i + 1], intra_refresh_map, intra_refresh_map_size);
         i++;
         break;
      }

      default:
      {
         // Try parsing for any image specific parameters
         // result indicates how many parameters were used up, 0,1,2
         // but we adjust by -1 as we have used one already
         const char *second_arg = (i + 1 < argc) ? argv[i + 1] : NULL;
         int parms_used = (raspicamcontrol_parse_cmdline(&state->camera_parameters, &argv[i][1], second_arg));

         // Still unused, try preview options
         if (!parms_used)
            parms_used = raspipreview_parse_cmdline(&state->preview_parameters, &argv[i][1], second_arg);


         // If no parms were used, this must be a bad parameters
         if (!parms_used)
            valid = 0;
         else
            i += parms_used - 1;

         break;
      }
      }
   }

   if (!valid)
   {
      fprintf(stderr, "Invalid command line option (%s)\n", argv[i-1]);
      return 1;
   }

   // Always disable verbose if output going to stdout
   if (state->filename && state->filename[0] == '-')
   {
      state->verbose = 0;
   }

   return 0;
}

/**
 * Display usage information for the application to stdout
 *
 * @param app_name String to display as the application name
 */
static void display_valid_parameters(char *app_name)
{
   int i;

   fprintf(stderr, "Display camera output to display, and optionally saves an H264 capture at requested bitrate\n\n");
   fprintf(stderr, "\nusage: %s [options]\n\n", app_name);

   fprintf(stderr, "Image parameter commands\n\n");

   raspicli_display_help(cmdline_commands, cmdline_commands_size);

   // Profile options
   fprintf(stderr, "\n\nH264 Profile options :\n%s", profile_map[0].mode );

   for (i=1;i<profile_map_size;i++)
   {
      fprintf(stderr, ",%s", profile_map[i].mode);
   }

   fprintf(stderr, "\n");

   // Intra refresh options
   fprintf(stderr, "\n\nH264 Intra refresh options :\n%s", intra_refresh_map[0].mode );

   for (i=1;i<intra_refresh_map_size;i++)
   {
      fprintf(stderr, ",%s", intra_refresh_map[i].mode);
   }

   fprintf(stderr, "\n");

   // Help for preview options
   raspipreview_display_help();

   // Now display any help information from the camcontrol code
   raspicamcontrol_display_help();

   fprintf(stderr, "\n");

   return;
}
#endif

/**
 *  buffer header callback function for camera control
 *
 *  Callback will dump buffer data to the specific file
 *
 * @param port Pointer to port from which callback originated
 * @param buffer mmal buffer header pointer
 */
static void camera_control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
   if (buffer->cmd == MMAL_EVENT_PARAMETER_CHANGED)
   {
      MMAL_EVENT_PARAMETER_CHANGED_T *param = (MMAL_EVENT_PARAMETER_CHANGED_T *)buffer->data;
      switch (param->hdr.id) {
         case MMAL_PARAMETER_CAMERA_SETTINGS:
         {
            MMAL_PARAMETER_CAMERA_SETTINGS_T *settings = (MMAL_PARAMETER_CAMERA_SETTINGS_T*)param;
            vcos_log_error("Exposure now %u, analog gain %u/%u, digital gain %u/%u",
            settings->exposure,
                        settings->analog_gain.num, settings->analog_gain.den,
                        settings->digital_gain.num, settings->digital_gain.den);
            vcos_log_error("AWB R=%u/%u, B=%u/%u",
                        settings->awb_red_gain.num, settings->awb_red_gain.den,
                        settings->awb_blue_gain.num, settings->awb_blue_gain.den
                        );
         }
         break;
      }
   }
   else if (buffer->cmd == MMAL_EVENT_ERROR) {
      vcos_log_error("Camera control callback got an error");
   } else {
      vcos_log_error("Received unexpected camera control callback event, 0x%08x", buffer->cmd);
   }

   mmal_buffer_header_release(buffer);
}

#if 0
/**
 * Open a file based on the settings in state
 *
 * @param state Pointer to state
 */
static FILE *open_filename(RASPIVID_STATE *pState)
{
   FILE *new_handle = NULL;
   char *tempname = NULL, *filename = NULL;

   if (pState->segmentSize || pState->splitWait)
   {
      // Create a new filename string
      asprintf(&tempname, pState->filename, pState->segmentNumber);
      filename = tempname;
   }
   else
   {
      filename = pState->filename;
   }

   if (filename)
      new_handle = fopen(filename, "wb");

   if (pState->verbose)
   {
      if (new_handle)
         fprintf(stderr, "Opening output file \"%s\"\n", filename);
      else
         fprintf(stderr, "Failed to open new file \"%s\"\n", filename);
   }

   if (tempname)
      free(tempname);

   return new_handle;
}

/**
 * Open a file based on the settings in state
 *
 * This time for the imv output file
 *
 * @param state Pointer to state
 */
static FILE *open_imv_filename(RASPIVID_STATE *pState)
{
   FILE *new_handle = NULL;
   char *tempname = NULL, *filename = NULL;

   if (pState->segmentSize || pState->splitWait)
   {
      // Create a new filename string
      asprintf(&tempname, pState->imv_filename, pState->segmentNumber);
      filename = tempname;
   }
   else
   {
      filename = pState->imv_filename;
   }

   if (filename)
      new_handle = fopen(filename, "wb");

   if (pState->verbose)
   {
      if (new_handle)
         fprintf(stderr, "Opening imv output file \"%s\"\n", filename);
      else
         fprintf(stderr, "Failed to open new imv file \"%s\"\n", filename);
   }

   if (tempname)
      free(tempname);

   return new_handle;
}
#endif

/**
 * Update any annotation data specific to the video.
 * This simply passes on the setting from cli, or
 * if application defined annotate requested, updates
 * with the H264 parameters
 *
 * @param state Pointer to state control struct
 *
 */
static void update_annotation_data(RASPIVID_STATE *state)
{
   RASPIVID_CONFIG *config = &state->config;

   // So, if we have asked for a application supplied string, set it to the H264 parameters
   if (config->camera_parameters.enable_annotate & ANNOTATE_APP_TEXT)
   {
      char *text;
      const char *refresh = raspicli_unmap_xref(config->intra_refresh_type, intra_refresh_map, intra_refresh_map_size);

      asprintf(&text,  "%dk,%ff,%s,%d,%s",
            config->bitrate / 1000,  ((float)(config->fps_n) / config->fps_d),
            refresh ? refresh : "(none)",
            config->intraperiod,
            raspicli_unmap_xref(config->profile, profile_map, profile_map_size));

      raspicamcontrol_set_annotate(state->camera_component, config->camera_parameters.enable_annotate, text,
                       config->camera_parameters.annotate_text_size,
                       config->camera_parameters.annotate_text_colour,
                       config->camera_parameters.annotate_bg_colour);

      free(text);
   }
   else
   {
      raspicamcontrol_set_annotate(state->camera_component, config->camera_parameters.enable_annotate,
                       config->camera_parameters.annotate_string,
                       config->camera_parameters.annotate_text_size,
                       config->camera_parameters.annotate_text_colour,
                       config->camera_parameters.annotate_bg_colour);
   }
}



/**
 *  buffer header callback function for encoder
 *
 *  Callback will dump buffer data to the specific file
 *
 * @param port Pointer to port from which callback originated
 * @param buffer mmal buffer header pointer
 */
static void encoder_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
   PORT_USERDATA *pData = (PORT_USERDATA *)port->userdata;
   RASPIVID_STATE *state  = pData->state;
   int64_t current_time;

   // All our segment times based on the receipt of the first encoder callback
   if (state->base_time == -1)
      state->base_time = vcos_getmicrosecs64()/1000;

   if (pData == NULL)
   {
      vcos_log_error("Received a encoder buffer callback with no state");
      // release buffer back to the pool
      mmal_buffer_header_release(buffer);
      return;
   }

   current_time = vcos_getmicrosecs64()/1000;
   if (state->base_time == -1)
     state->base_time = current_time;

   // See if the second count has changed and we need to update any annotation
   if (current_time/1000 != state->last_second)
   {
      update_annotation_data(state);
      state->last_second = current_time/1000;
   }

   /* Send buffer to GStreamer element for pushing to the pipeline */
   mmal_queue_put(state->encoded_buffer_q, buffer);
}

GstFlowReturn
raspi_capture_fill_buffer(RASPIVID_STATE *state, GstBuffer **bufp,
    GstClock *clock, GstClockTime base_time)
{
  RASPIVID_CONFIG *config = &state->config;
  GstBuffer *buf;
  MMAL_BUFFER_HEADER_T *buffer;
  GstFlowReturn ret = GST_FLOW_ERROR;
  /* No timestamps if no clockm or invalid PTS */
  GstClockTime gst_pts = GST_CLOCK_TIME_NONE;

  do {
    buffer = mmal_queue_timedwait(state->encoded_buffer_q, 500);
    // Work around a bug where mmal_queue_timedwait() might return
    // immediately if the internal timeout time aligns exactly
    // with a 1 second rollover boundary by checking errno.
    if (errno == EINVAL) {
      GST_WARNING ("Retrying mmal_queue_timedwait() due to spurious failure.");
      continue;
    }
  } while (0);

  if (G_UNLIKELY(buffer == NULL)) {
      return GST_FLOW_ERROR_TIMEOUT;
  }

  if (G_LIKELY (config->useSTC && clock)) {
    MMAL_PARAMETER_INT64_T param;
    GstClockTime runtime;

    runtime = gst_clock_get_time (clock) - base_time;

    param.hdr.id = MMAL_PARAMETER_SYSTEM_TIME;
    param.hdr.size = sizeof(param);
    param.value = -1;

    mmal_port_parameter_get(state->encoder_output_port, &param.hdr);

    if (buffer->pts != -1 && param.value != -1 && param.value >= buffer->pts) {
      /* Convert microsecond RPi TS to GStreamer clock: */
      GstClockTime offset = (param.value - buffer->pts) * 1000;
      if (runtime >= offset)
        gst_pts = runtime - offset;
    }
    GST_LOG ("Buf %05u bytes FLAGS 0x%05x (uS) PTS %" G_GINT64_FORMAT
        " DTS %" G_GINT64_FORMAT " STC %" G_GINT64_FORMAT
        " (latency %" G_GINT64_FORMAT "uS) TS %" GST_TIME_FORMAT,
        buffer->length, buffer->flags, buffer->pts, buffer->dts, param.value,
        param.value - buffer->pts, GST_TIME_ARGS (gst_pts));
  }
  else {
    GST_LOG ("use-stc=false. Not applying STC to buffer");
  }

  mmal_buffer_header_mem_lock(buffer);
  buf = gst_buffer_new_allocate(NULL, buffer->length, NULL);
  if (buf) {
    if (config->useSTC)
        GST_BUFFER_DTS(buf) = GST_BUFFER_PTS(buf) = gst_pts;
    /* FIXME: Can we avoid copies and give MMAL our own buffers to fill? */
    gst_buffer_fill(buf, 0, buffer->data + buffer->offset, buffer->length);

    if ((buffer->flags & MMAL_BUFFER_HEADER_FLAG_CONFIG))
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_HEADER);
    else if ((buffer->flags & MMAL_BUFFER_HEADER_FLAG_KEYFRAME))
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    else
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

    /* NAL_END is bogus and can't be trusted */
    if ((buffer->flags & MMAL_BUFFER_HEADER_FLAG_FRAME_END))
      ret = GST_FLOW_OK;
    else
      ret = GST_FLOW_KEEP_ACCUMULATING;
  }

  mmal_buffer_header_mem_unlock(buffer);

  *bufp = buf;
  // release buffer back to the pool
  mmal_buffer_header_release(buffer);

  // and send one back to the port (if still open)
  if (state->encoder_output_port->is_enabled)
  {
     MMAL_STATUS_T status = MMAL_SUCCESS;

     buffer = mmal_queue_get(state->encoder_pool->queue);
     if (buffer)
        status = mmal_port_send_buffer(state->encoder_output_port, buffer);

     if (!buffer || status != MMAL_SUCCESS) {
       vcos_log_error("Unable to return a buffer to the encoder port");
       ret = GST_FLOW_ERROR;
     }
  }

  return ret;
}

/**
 * Create the camera component, set up its ports
 *
 * @param state Pointer to state control struct
 *
 * @return MMAL_SUCCESS if all OK, something else otherwise
 *
 */
static MMAL_STATUS_T create_camera_component(RASPIVID_STATE *state)
{
   MMAL_COMPONENT_T *camera = NULL;
   MMAL_STATUS_T status;
   RASPIVID_CONFIG *config = &state->config;

   /* Create the component */
   status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Failed to create camera component");
      goto error;
   }

   MMAL_PARAMETER_INT32_T camera_num =
      {{MMAL_PARAMETER_CAMERA_NUM, sizeof(camera_num)}, config->cameraNum};

   status = mmal_port_parameter_set(camera->control, &camera_num.hdr);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Could not select camera : error %d", status);
      goto error;
   }

   if (!camera->output_num)
   {
      status = MMAL_ENOSYS;
      vcos_log_error("Camera doesn't have output ports");
      goto error;
   }

   status = mmal_port_parameter_set_uint32(camera->control, MMAL_PARAMETER_CAMERA_CUSTOM_SENSOR_CONFIG, config->sensor_mode);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Could not set sensor mode : error %d", status);
      goto error;
   }

   if (config->settings)
   {
      MMAL_PARAMETER_CHANGE_EVENT_REQUEST_T change_event_request =
         {{MMAL_PARAMETER_CHANGE_EVENT_REQUEST, sizeof(MMAL_PARAMETER_CHANGE_EVENT_REQUEST_T)},
          MMAL_PARAMETER_CAMERA_SETTINGS, 1};

      status = mmal_port_parameter_set(camera->control, &change_event_request.hdr);
      if ( status != MMAL_SUCCESS )
      {
         vcos_log_error("No camera settings events");
      }
   }

   // Enable the camera, and tell it its control callback function
   status = mmal_port_enable(camera->control, camera_control_callback);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to enable control port : error %d", status);
      goto error;
   }

   state->camera_component = camera;

   return status;

error:
   if (camera)
     mmal_component_destroy(camera);

   return status;
}

MMAL_STATUS_T
raspi_capture_set_format_and_start(RASPIVID_STATE *state)
{
   MMAL_COMPONENT_T *camera = NULL;
   MMAL_STATUS_T status;
   MMAL_ES_FORMAT_T *format;
   MMAL_PORT_T *preview_port = NULL, *video_port = NULL, *still_port = NULL;
   RASPIVID_CONFIG *config = &state->config;

   //  set up the camera configuration

   MMAL_PARAMETER_CAMERA_CONFIG_T cam_config =
   {
      { MMAL_PARAMETER_CAMERA_CONFIG, sizeof(cam_config) },
      .max_stills_w = config->width,
      .max_stills_h = config->height,
      .stills_yuv422 = 0,
      .one_shot_stills = 0,
      .max_preview_video_w = config->width,
      .max_preview_video_h = config->height,
      .num_preview_video_frames = 3,
      .stills_capture_circular_buffer_height = 0,
      .fast_preview_resume = 0,
      .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RAW_STC
   };

   camera = state->camera_component;
   preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
   video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
   still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];

   mmal_port_parameter_set(camera->control, &cam_config.hdr);

   // Now set up the port formats

   // Set the encode format on the Preview port
   // HW limitations mean we need the preview to be the same size as the required recorded output

   format = preview_port->format;

   if(config->camera_parameters.shutter_speed > 6000000)
   {
        MMAL_PARAMETER_FPS_RANGE_T fps_range = {{MMAL_PARAMETER_FPS_RANGE, sizeof(fps_range)},
                                                     { 50, 1000 }, {166, 1000}};
        mmal_port_parameter_set(preview_port, &fps_range.hdr);
   }
   else if(config->camera_parameters.shutter_speed > 1000000)
   {
        MMAL_PARAMETER_FPS_RANGE_T fps_range = {{MMAL_PARAMETER_FPS_RANGE, sizeof(fps_range)},
                                                     { 166, 1000 }, {999, 1000}};
        mmal_port_parameter_set(preview_port, &fps_range.hdr);
   }

   //enable dynamic framerate if necessary
   if (config->camera_parameters.shutter_speed)
   {
      if (((float)(config->fps_n) / config->fps_d) > 1000000.0 / config->camera_parameters.shutter_speed)
      {
         config->fps_n = 0;
         config->fps_d = 1;
         GST_INFO ("Enabling dynamic frame rate to fulfil shutter speed requirement");
      }
   }

   format->encoding = MMAL_ENCODING_OPAQUE;
   format->encoding_variant = MMAL_ENCODING_I420;

   format->es->video.width = VCOS_ALIGN_UP(config->width, 32);
   format->es->video.height = VCOS_ALIGN_UP(config->height, 16);
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = config->width;
   format->es->video.crop.height = config->height;
   format->es->video.frame_rate.num = config->fps_n;
   format->es->video.frame_rate.den = config->fps_d;

   status = mmal_port_format_commit(preview_port);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("camera viewfinder format couldn't be set");
      goto error;
   }

   // Set the encode format on the video  port
   format = video_port->format;

   if(config->camera_parameters.shutter_speed > 6000000)
   {
        MMAL_PARAMETER_FPS_RANGE_T fps_range = {{MMAL_PARAMETER_FPS_RANGE, sizeof(fps_range)},
                                                     { 50, 1000 }, {166, 1000}};
        mmal_port_parameter_set(video_port, &fps_range.hdr);
   }
   else if(config->camera_parameters.shutter_speed > 1000000)
   {
        MMAL_PARAMETER_FPS_RANGE_T fps_range = {{MMAL_PARAMETER_FPS_RANGE, sizeof(fps_range)},
                                                     { 167, 1000 }, {999, 1000}};
        mmal_port_parameter_set(video_port, &fps_range.hdr);
   }

   /* If encoding, set opaque tunneling format */
   if (state->encoder_component) {
     format->encoding = MMAL_ENCODING_OPAQUE;
     format->encoding_variant = MMAL_ENCODING_I420;
   }
   else {
     format->encoding = config->encoding;
     format->encoding_variant = config->encoding;
   }

   format->es->video.width = VCOS_ALIGN_UP(config->width, 32);
   format->es->video.height = VCOS_ALIGN_UP(config->height, 16);
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = config->width;
   format->es->video.crop.height = config->height;
   format->es->video.frame_rate.num = config->fps_n;
   format->es->video.frame_rate.den = config->fps_d;

   status = mmal_port_format_commit(video_port);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("camera video format couldn't be set");
      goto error;
   }

   // Ensure there are enough buffers to avoid dropping frames
   if (video_port->buffer_num < VIDEO_OUTPUT_BUFFERS_NUM)
      video_port->buffer_num = VIDEO_OUTPUT_BUFFERS_NUM;


   // Set the encode format on the still  port

   format = still_port->format;

   format->encoding = MMAL_ENCODING_OPAQUE;
   format->encoding_variant = MMAL_ENCODING_I420;

   format->es->video.width = VCOS_ALIGN_UP(config->width, 32);
   format->es->video.height = VCOS_ALIGN_UP(config->height, 16);
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = config->width;
   format->es->video.crop.height = config->height;
   format->es->video.frame_rate.num = 0;
   format->es->video.frame_rate.den = 1;

   status = mmal_port_format_commit(still_port);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("camera still format couldn't be set");
      goto error;
   }

   /* Ensure there are enough buffers to avoid dropping frames */
   if (still_port->buffer_num < VIDEO_OUTPUT_BUFFERS_NUM)
      still_port->buffer_num = VIDEO_OUTPUT_BUFFERS_NUM;

   /* Enable component */
   status = mmal_component_enable(camera);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("camera component couldn't be enabled");
      goto error;
   }

   raspicamcontrol_set_all_parameters(camera, &config->camera_parameters);

   update_annotation_data(state);

   if (config->verbose)
      fprintf(stderr, "Camera component done\n");

   return status;

error:
   if (camera)
      mmal_component_disable(camera);

   return status;
}

/**
 * Destroy the camera component
 *
 * @param state Pointer to state control struct
 *
 */
static void destroy_camera_component(RASPIVID_STATE *state)
{
   if (state->camera_component)
   {
      mmal_component_destroy(state->camera_component);
      state->camera_component = NULL;
   }
}

gboolean raspi_capture_request_i_frame(RASPIVID_STATE *state)
{
   MMAL_PORT_T *encoder_output = NULL;
   MMAL_STATUS_T status;
   MMAL_PARAMETER_BOOLEAN_T param = {{  MMAL_PARAMETER_VIDEO_REQUEST_I_FRAME, sizeof(param)}, 1};

   if (state->encoder_component)
     return TRUE;

   encoder_output = state->encoder_component->output[0];
   status = mmal_port_parameter_set(encoder_output, &param.hdr);
   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to request I-frame");
      return FALSE;
   }
   return TRUE;
}

/**
 * Create the encoder component, set up its ports
 *
 * @param state Pointer to state control struct
 *
 * @return MMAL_SUCCESS if all OK, something else otherwise
 *
 */
static MMAL_STATUS_T create_encoder_component(RASPIVID_STATE *state)
{
   MMAL_COMPONENT_T *encoder = 0;
   MMAL_PORT_T *encoder_input = NULL, *encoder_output = NULL;
   MMAL_STATUS_T status;
   RASPIVID_CONFIG *config = &state->config;

   gboolean encoded_format =
     (config->encoding == MMAL_ENCODING_H264 ||
      config->encoding == MMAL_ENCODING_MJPEG ||
      config->encoding == MMAL_ENCODING_JPEG);

   if (!encoded_format)
     return MMAL_SUCCESS;

   if (config->encoding == MMAL_ENCODING_JPEG)
     status = mmal_component_create(MMAL_COMPONENT_DEFAULT_IMAGE_ENCODER, &encoder);
   else
     status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER, &encoder);

   if (status != MMAL_SUCCESS) {
     vcos_log_error("Unable to create video encoder component");
     goto error;
   }

   if (!encoder->input_num || !encoder->output_num)
   {
      status = MMAL_ENOSYS;
      vcos_log_error("Video encoder doesn't have input/output ports");
      goto error;
   }

   encoder_input = encoder->input[0];
   encoder_output = encoder->output[0];

   // We want same format on input and output
   mmal_format_copy(encoder_output->format, encoder_input->format);

   // Configure desired encoding
   encoder_output->format->encoding = config->encoding;

   encoder_output->format->bitrate = config->bitrate;

   if (config->encoding == MMAL_ENCODING_H264)
     encoder_output->buffer_size = encoder_output->buffer_size_recommended;
   else
     encoder_output->buffer_size = 256<<10;

   if (encoder_output->buffer_size < encoder_output->buffer_size_min)
      encoder_output->buffer_size = encoder_output->buffer_size_min;

   encoder_output->buffer_num = encoder_output->buffer_num_recommended;

   if (encoder_output->buffer_num < encoder_output->buffer_num_min)
      encoder_output->buffer_num = encoder_output->buffer_num_min;

   GST_DEBUG ("encoder wants %d buffers of size %u",
       (guint)encoder_output->buffer_num, (guint)encoder_output->buffer_size);

   // We need to set the frame rate on output to 0, to ensure it gets
   // updated correctly from the input framerate when port connected
   encoder_output->format->es->video.frame_rate.num = 0;
   encoder_output->format->es->video.frame_rate.den = 1;

   // Commit the port changes to the output port
   status = mmal_port_format_commit(encoder_output);
   if (status != MMAL_SUCCESS) {
      vcos_log_error("Unable to set format on video encoder output port");
      goto error;
   }

   // Set the rate control parameter
   if (0)
   {
      MMAL_PARAMETER_VIDEO_RATECONTROL_T param = {{ MMAL_PARAMETER_RATECONTROL, sizeof(param)}, MMAL_VIDEO_RATECONTROL_DEFAULT};
      status = mmal_port_parameter_set(encoder_output, &param.hdr);
      if (status != MMAL_SUCCESS)
      {
         vcos_log_error("Unable to set ratecontrol");
         goto error;
      }

   }

   if (config->encoding == MMAL_ENCODING_H264 && config->intraperiod != -1)
   {
      MMAL_PARAMETER_UINT32_T param = {{ MMAL_PARAMETER_INTRAPERIOD, sizeof(param)}, config->intraperiod};
      status = mmal_port_parameter_set(encoder_output, &param.hdr);
      if (status != MMAL_SUCCESS)
      {
         vcos_log_error("Unable to set intraperiod");
         goto error;
      }
   }

   if (config->encoding == MMAL_ENCODING_H264 && config->quantisationParameter)
   {
      MMAL_PARAMETER_UINT32_T param = {{ MMAL_PARAMETER_VIDEO_ENCODE_INITIAL_QUANT, sizeof(param)}, config->quantisationParameter};
      status = mmal_port_parameter_set(encoder_output, &param.hdr);
      if (status != MMAL_SUCCESS)
      {
         vcos_log_error("Unable to set initial QP");
         goto error;
      }

      MMAL_PARAMETER_UINT32_T param2 = {{ MMAL_PARAMETER_VIDEO_ENCODE_MIN_QUANT, sizeof(param)}, config->quantisationParameter};
      status = mmal_port_parameter_set(encoder_output, &param2.hdr);
      if (status != MMAL_SUCCESS)
      {
         vcos_log_error("Unable to set min QP");
         goto error;
      }

      MMAL_PARAMETER_UINT32_T param3 = {{ MMAL_PARAMETER_VIDEO_ENCODE_MAX_QUANT, sizeof(param)}, config->quantisationParameter};
      status = mmal_port_parameter_set(encoder_output, &param3.hdr);
      if (status != MMAL_SUCCESS)
      {
         vcos_log_error("Unable to set max QP");
         goto error;
      }
   }

   if (config->encoding == MMAL_ENCODING_H264)
   {
      MMAL_PARAMETER_VIDEO_PROFILE_T  param;
      param.hdr.id = MMAL_PARAMETER_PROFILE;
      param.hdr.size = sizeof(param);

      param.profile[0].profile = config->profile;
      param.profile[0].level = MMAL_VIDEO_LEVEL_H264_4; // This is the only value supported

      status = mmal_port_parameter_set(encoder_output, &param.hdr);
      if (status != MMAL_SUCCESS)
      {
         vcos_log_error("Unable to set H264 profile");
         goto error;
      }
   }

   if (config->encoding != MMAL_ENCODING_JPEG)
   {
     if (mmal_port_parameter_set_boolean(encoder_input, MMAL_PARAMETER_VIDEO_IMMUTABLE_INPUT, config->immutableInput) != MMAL_SUCCESS)
     {
        vcos_log_error("Unable to set immutable input flag");
        // Continue rather than abort..
     }

     //set INLINE HEADER flag to generate SPS and PPS for every IDR if requested
     if (mmal_port_parameter_set_boolean(encoder_output, MMAL_PARAMETER_VIDEO_ENCODE_INLINE_HEADER, config->bInlineHeaders) != MMAL_SUCCESS)
     {
        vcos_log_error("failed to set INLINE HEADER FLAG parameters");
        // Continue rather than abort..
     }
  }

  if (config->encoding == MMAL_ENCODING_H264)
  {
     //set INLINE VECTORS flag to request motion vector estimates
     if (mmal_port_parameter_set_boolean(encoder_output, MMAL_PARAMETER_VIDEO_ENCODE_INLINE_VECTORS, config->inlineMotionVectors) != MMAL_SUCCESS)
     {
        vcos_log_error("failed to set INLINE VECTORS parameters");
        // Continue rather than abort..
     }

     // Adaptive intra refresh settings
     if (config->intra_refresh_type != -1)
     {
        MMAL_PARAMETER_VIDEO_INTRA_REFRESH_T  param;

        /* Need to memset, apparently mmal_port_parameter_get()
         * doesn't retrieve all parameters, causing random failures
         * when we set it
         */
        memset (&param, 0, sizeof (MMAL_PARAMETER_VIDEO_INTRA_REFRESH_T));

        param.hdr.id = MMAL_PARAMETER_VIDEO_INTRA_REFRESH;
        param.hdr.size = sizeof(param);

        // Get first so we don't overwrite anything unexpectedly
        status = mmal_port_parameter_get(encoder_output, &param.hdr);

        param.refresh_mode = config->intra_refresh_type;

        //if (state->intra_refresh_type == MMAL_VIDEO_INTRA_REFRESH_CYCLIC_MROWS)
        //   param.cir_mbs = 10;

        status = mmal_port_parameter_set(encoder_output, &param.hdr);
        if (status != MMAL_SUCCESS)
        {
         vcos_log_error("Unable to set H264 intra-refresh values");
           goto error;
        }
     }
   }

   if (config->encoding == MMAL_ENCODING_JPEG)
   {
      status = mmal_port_parameter_set_uint32(encoder_output, MMAL_PARAMETER_JPEG_Q_FACTOR, config->jpegQuality);
      if (status != MMAL_SUCCESS) {
         vcos_log_error("Unable to set JPEG quality");
         // Continue after warning
      }

#ifdef MMAL_PARAMETER_JPEG_RESTART_INTERVAL
      status = mmal_port_parameter_set_uint32(encoder_output, MMAL_PARAMETER_JPEG_RESTART_INTERVAL, config->jpegRestartInterval);
      if (status != MMAL_SUCCESS) {
         vcos_log_error("Unable to set JPEG restart interval");
         // Continue after warning
      }
#endif
   }

   //  Enable component
   status = mmal_component_enable(encoder);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to enable video encoder component");
      goto error;
   }

   state->encoder_component = encoder;

   if (config->verbose)
      fprintf(stderr, "Encoder component done\n");

   return status;

   error:
   if (encoder)
      mmal_component_destroy(encoder);

   state->encoder_component = NULL;

   return status;
}

/**
 * Destroy the encoder component
 *
 * @param state Pointer to state control struct
 *
 */
static void destroy_encoder_component(RASPIVID_STATE *state)
{
  /* Empty the buffer header q */
   if (state->encoded_buffer_q) {
      while (mmal_queue_length(state->encoded_buffer_q)) {
        MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(state->encoded_buffer_q);
        mmal_buffer_header_release(buffer);
      }
   }

   // Get rid of any port buffers first
   if (state->encoder_pool)
   {
      mmal_port_pool_destroy(state->encoder_output_port, state->encoder_pool);
      state->encoder_pool = NULL;
   }

   if (state->encoder_component) {

      mmal_component_destroy(state->encoder_component);
      state->encoder_component = NULL;
   }
}

/**
 * Connect two specific ports together
 *
 * @param output_port Pointer the output port
 * @param input_port Pointer the input port
 * @param Pointer to a mmal connection pointer, reassigned if function successful
 * @return Returns a MMAL_STATUS_T giving result of operation
 *
 */
static MMAL_STATUS_T connect_ports(MMAL_PORT_T *output_port, MMAL_PORT_T *input_port, MMAL_CONNECTION_T **connection)
{
   MMAL_STATUS_T status;

   status =  mmal_connection_create(connection, output_port, input_port, MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT);

   if (status == MMAL_SUCCESS)
   {
      status =  mmal_connection_enable(*connection);
      if (status != MMAL_SUCCESS)
         mmal_connection_destroy(*connection);
   }

   return status;
}

/**
 * Checks if specified port is valid and enabled, then disables it
 *
 * @param port  Pointer the port
 *
 */
static void check_disable_port(MMAL_PORT_T *port)
{
   if (port && port->is_enabled)
      mmal_port_disable(port);
}

void raspicapture_init(void)
{
   bcm_host_init();

   // Register our application with the logging system
   vcos_log_register("RaspiVid", VCOS_LOG_CATEGORY);
}

RASPIVID_STATE *
raspi_capture_setup(RASPIVID_CONFIG *config)
{
  // Our main data storage vessel..
  RASPIVID_STATE *state;

  MMAL_STATUS_T status = MMAL_SUCCESS;

  /* Default everything to zero */
  state = calloc(1, sizeof(RASPIVID_STATE));

  /* Apply passed in config */
  state->config = *config;

  /* Initialize timestamping */
  state->base_time = state->last_second = -1;

  /* So far, all we can do is create the camera component. Actual
   * config and connection of encoders etc happens in _start()
   */
  // OK, we have a nice set of parameters. Now set up our components
  // We have three components. Camera, Preview and encoder.

  if ((status = create_camera_component(state)) != MMAL_SUCCESS)
  {
     vcos_log_error("%s: Failed to create camera component", __func__);
     return NULL;
  }

  if ((status = raspipreview_create(&state->preview_state, &config->preview_parameters)) != MMAL_SUCCESS)
  {
     vcos_log_error("%s: Failed to create preview component", __func__);
     destroy_camera_component(state);
     return NULL;
  }

  state->encoded_buffer_q = mmal_queue_create();

  return state;
}

gboolean
raspi_capture_start(RASPIVID_STATE *state)
{
  MMAL_STATUS_T status = MMAL_SUCCESS;
  RASPIVID_CONFIG *config = &state->config;

  MMAL_PORT_T *camera_preview_port = NULL;
  MMAL_PORT_T *preview_input_port = NULL;
  MMAL_PORT_T *encoder_input_port = NULL;

  MMAL_POOL_T *pool;

  if ((status = create_encoder_component(state)) != MMAL_SUCCESS) {
    vcos_log_error("%s: Failed to create encode component", __func__);
    return FALSE;
  }

  if (config->verbose)
  {
     dump_state(state);
  }

  state->camera_video_port   = state->camera_component->output[MMAL_CAMERA_VIDEO_PORT];
  state->camera_still_port   = state->camera_component->output[MMAL_CAMERA_CAPTURE_PORT];
  camera_preview_port = state->camera_component->output[MMAL_CAMERA_PREVIEW_PORT];
  preview_input_port  = state->preview_state.preview_component->input[0];

  if (state->encoder_component) {
    encoder_input_port  = state->encoder_component->input[0];
    state->encoder_output_port = state->encoder_component->output[0];
  } else {
    state->encoder_output_port = state->camera_video_port;
  }

  if ((status = raspi_capture_set_format_and_start(state)) != MMAL_SUCCESS) {
     return FALSE;
  }

  GST_DEBUG ("Creating pool of %d buffers of size %d",
      state->encoder_output_port->buffer_num, state->encoder_output_port->buffer_size);
  /* Create pool of buffer headers for the output port to consume */
  pool = mmal_port_pool_create(state->encoder_output_port,
             state->encoder_output_port->buffer_num, state->encoder_output_port->buffer_size);
  if (!pool)
  {
    vcos_log_error("Failed to create buffer header pool for encoder output port %s",
         state->encoder_output_port->name);
    return FALSE;
  }
  state->encoder_pool = pool;

  if (state->config.verbose)
     fprintf(stderr, "Starting component connection stage\n");

  if (config->preview_parameters.wantPreview )
  {
     if (config->verbose)
     {
        fprintf(stderr, "Connecting camera preview port to preview input port\n");
        fprintf(stderr, "Starting video preview\n");
     }

     // Connect camera to preview
     status = connect_ports(camera_preview_port, preview_input_port, &state->preview_connection);
     if (status != MMAL_SUCCESS)
     {
        vcos_log_error("%s: Failed to connect camera to preview", __func__);
        return FALSE;
     }
  }

  if (state->encoder_component) {
    if (config->verbose)
       fprintf(stderr, "Connecting camera video port to encoder input port\n");

    // Now connect the camera to the encoder
    status = connect_ports(state->camera_video_port, encoder_input_port, &state->encoder_connection);
    if (status != MMAL_SUCCESS)
    {
      if (config->preview_parameters.wantPreview )
        mmal_connection_destroy(state->preview_connection);
      vcos_log_error("%s: Failed to connect camera video port to encoder input", __func__);
      return FALSE;
    }
  }

  // Set up our userdata - this is passed though to the callback where we need the information.
  state->callback_data.state = state;
  state->callback_data.abort = 0;

  state->encoder_output_port->userdata = (struct MMAL_PORT_USERDATA_T *)&state->callback_data;

  if (config->verbose)
     fprintf(stderr, "Enabling encoder output port\n");

  // Enable the encoder output port and tell it its callback function
  status = mmal_port_enable(state->encoder_output_port, encoder_buffer_callback);
  if (status != MMAL_SUCCESS)
  {
     vcos_log_error("Failed to setup encoder output");
     goto error;
  }

  if (config->demoMode)
  {
     // Run for the user specific time..
     int num_iterations = config->timeout / config->demoInterval;
     int i;

     if (config->verbose)
        fprintf(stderr, "Running in demo mode\n");

     for (i=0;config->timeout == 0 || i<num_iterations;i++)
     {
        raspicamcontrol_cycle_test(state->camera_component);
        vcos_sleep(state->config.demoInterval);
     }
  }

  if (config->verbose)
    fprintf(stderr, "Starting video capture\n");

  if (mmal_port_parameter_set_boolean(state->camera_video_port, MMAL_PARAMETER_CAPTURE, 1) != MMAL_SUCCESS)
  {
     goto error;
  }

  // Send all the buffers to the encoder output port
  {
     int num = mmal_queue_length(state->encoder_pool->queue);
     int q;
     for (q=0;q<num;q++)
     {
        MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(state->encoder_pool->queue);

        if (!buffer)
           vcos_log_error("Unable to get a required buffer %d from pool queue", q);

        if (mmal_port_send_buffer(state->encoder_output_port, buffer)!= MMAL_SUCCESS)
           vcos_log_error("Unable to send a buffer to encoder output port (%d)", q);

     }
  }

  // Now wait until we need to stop. Whilst waiting we do need to check to see if we have aborted (for example
  // out of storage space)
  // Going to check every ABORT_INTERVAL milliseconds

#if 0
  for (wait = 0; config->timeout == 0 || wait < config->timeout; wait+= ABORT_INTERVAL)
  {
     vcos_sleep(ABORT_INTERVAL);
     if (state->callback_data.abort)
        break;
  }

  if (config->verbose)
     fprintf(stderr, "Finished capture\n");
#endif

  return (status == MMAL_SUCCESS);

error:
  raspi_capture_stop(state);

  if (status != MMAL_SUCCESS) {
    mmal_status_to_int(status);
    raspicamcontrol_check_configuration(128);
  }

  return FALSE;
}

void
raspi_capture_stop(RASPIVID_STATE *state)
{
   RASPIVID_CONFIG *config = &state->config;

  if (config->verbose)
     fprintf(stderr, "Closing down\n");

  if (config->preview_parameters.wantPreview )
     mmal_connection_destroy(state->preview_connection);

  // Disable all our ports that are not handled by connections
  check_disable_port(state->camera_still_port);
  check_disable_port(state->encoder_output_port);

  if (state->encoder_component) {
     mmal_connection_destroy(state->encoder_connection);
     mmal_component_disable(state->encoder_component);
     destroy_encoder_component(state);
  }
}

void
raspi_capture_free(RASPIVID_STATE *state)
{
   RASPIVID_CONFIG *config = &state->config;

  // Can now close our file. Note disabling ports may flush buffers which causes
  // problems if we have already closed the file!
  if (state->output_file && state->output_file != stdout)
     fclose(state->output_file);

  /* Disable components */
  if (state->encoder_component)
     mmal_component_disable(state->encoder_component);

  if (state->preview_state.preview_component)
     mmal_component_disable(state->preview_state.preview_component);

  if (state->camera_component)
     mmal_component_disable(state->camera_component);

  destroy_encoder_component(state);
  raspipreview_destroy(&state->preview_state);
  destroy_camera_component(state);

  if (state->encoded_buffer_q) {
    mmal_queue_destroy(state->encoded_buffer_q);
    state->encoded_buffer_q = NULL;
  }

  if (config->verbose)
     fprintf(stderr, "Close down completed, all components disconnected, disabled and destroyed\n\n");

  free(state);
}

void
raspi_capture_update_config (RASPIVID_STATE *state, RASPIVID_CONFIG *config, gboolean dynamic)
{
  MMAL_STATUS_T status;
  RASPICAM_CAMERA_PARAMETERS *params = &config->camera_parameters;
  MMAL_COMPONENT_T *camera = state->camera_component;

  /* Store the new config */
  state->config = *config;
  if (!dynamic)
    return;

  if (state->encoder_component && config->change_flags & PROP_CHANGE_ENCODING) {
    /* BITRATE or QUANT or KEY Interval, intra refresh */
    MMAL_COMPONENT_T *encoder = state->encoder_component;
    MMAL_PORT_T *encoder_output = encoder->output[0];

    status = mmal_port_parameter_set_uint32(encoder_output, MMAL_PARAMETER_VIDEO_BIT_RATE, config->bitrate);
    if (status != MMAL_SUCCESS)
      vcos_log_warn("Unable to change bitrate dynamically");

    {
      MMAL_PARAMETER_UINT32_T param = {{ MMAL_PARAMETER_INTRAPERIOD, sizeof(param)}, config->intraperiod};
      status = mmal_port_parameter_set(encoder_output, &param.hdr);
      if (status != MMAL_SUCCESS)
        vcos_log_warn("Unable to change intraperiod dynamically");
    }

#if 0 /* not dynamically change-able */
    {
      MMAL_PARAMETER_UINT32_T param = {{ MMAL_PARAMETER_VIDEO_ENCODE_INITIAL_QUANT, sizeof(param)}, config->quantisationParameter};
      status = mmal_port_parameter_set(encoder_output, &param.hdr);
      if (status != MMAL_SUCCESS)
        vcos_log_warn("Unable to change Initial Quantisation Parameter dynamically");

      MMAL_PARAMETER_UINT32_T param2 = {{ MMAL_PARAMETER_VIDEO_ENCODE_MIN_QUANT, sizeof(param)}, config->quantisationParameter};
      status = mmal_port_parameter_set(encoder_output, &param2.hdr);
      if (status != MMAL_SUCCESS)
        vcos_log_warn("Unable to change Minimum Quantisation Parameter dynamically");

      MMAL_PARAMETER_UINT32_T param3 = {{ MMAL_PARAMETER_VIDEO_ENCODE_MAX_QUANT, sizeof(param)}, config->quantisationParameter};
      status = mmal_port_parameter_set(encoder_output, &param3.hdr);
      if (status != MMAL_SUCCESS)
        vcos_log_warn("Unable to change Maximum Quantisation Parameter dynamically");
    }

    {
      // Adaptive intra refresh settings
      MMAL_PARAMETER_VIDEO_INTRA_REFRESH_T  param;
      param.hdr.id = MMAL_PARAMETER_VIDEO_INTRA_REFRESH;
      param.hdr.size = sizeof(param);

      // Get first so we don't overwrite anything unexpectedly
      status = mmal_port_parameter_get(encoder_output, &param.hdr);
      if (state != MMAL_SUCCESS) {
        /* Need to memset, apparently mmal_port_parameter_get()
         * doesn't retrieve all parameters, causing random failures
         * when we set it. On older firmware the get fails.
         */
        memset (&param, 0, sizeof (MMAL_PARAMETER_VIDEO_INTRA_REFRESH_T));
      }
      param.refresh_mode = config->intra_refresh_type;

      status = mmal_port_parameter_set(encoder_output, &param.hdr);
      if (status != MMAL_SUCCESS)
         vcos_log_warn("Unable to set H264 intra-refresh values dynamically");
    }
#endif
  }
  if (config->change_flags & PROP_CHANGE_PREVIEW) {
    /* Preview settings or fullscreen */
    status = raspipreview_update_config (&state->preview_state,
        &config->preview_parameters);
    if (status != MMAL_SUCCESS)
      vcos_log_warn("Unable to change preview config dynamically");
  }
  if (config->change_flags & PROP_CHANGE_COLOURBALANCE) {
    raspicamcontrol_set_saturation(camera, params->saturation);
    raspicamcontrol_set_sharpness(camera, params->sharpness);
    raspicamcontrol_set_contrast(camera, params->contrast);
    raspicamcontrol_set_brightness(camera, params->brightness);
  }
  if (config->change_flags & PROP_CHANGE_SENSOR_SETTINGS) {
    /* ISO, EXPOSURE, SHUTTER, DRC, Sensor Mode */
    raspicamcontrol_set_ISO(camera, params->ISO);
    raspicamcontrol_set_exposure_compensation(camera, params->exposureCompensation);
    raspicamcontrol_set_exposure_mode(camera, params->exposureMode);
    raspicamcontrol_set_metering_mode(camera, params->exposureMeterMode);
    raspicamcontrol_set_shutter_speed(camera, params->shutter_speed);
    raspicamcontrol_set_DRC(camera, params->drc_level);

    /* Can we change sensor mode on the fly? Disable if not */
    status = mmal_port_parameter_set_uint32(camera->control,
       MMAL_PARAMETER_CAMERA_CUSTOM_SENSOR_CONFIG, config->sensor_mode);
    if (status != MMAL_SUCCESS)
      vcos_log_warn("Unable to change sensor mode dynamically");
  }
  if (config->change_flags & PROP_CHANGE_VIDEO_STABILISATION) {
    raspicamcontrol_set_video_stabilisation(camera, params->videoStabilisation);
  }
  if (config->change_flags & PROP_CHANGE_AWB) {
    raspicamcontrol_set_awb_mode(camera, params->awbMode);
    raspicamcontrol_set_awb_gains(camera, params->awb_gains_r, params->awb_gains_b);
  }
  if (config->change_flags & PROP_CHANGE_IMAGE_COLOUR_EFFECT) {
    raspicamcontrol_set_imageFX(camera, params->imageEffect);
    raspicamcontrol_set_colourFX(camera, &params->colourEffects);
  }
  if (config->change_flags & PROP_CHANGE_ORIENTATION) {
    raspicamcontrol_set_rotation(camera, params->rotation);
    raspicamcontrol_set_flips(camera, params->hflip, params->vflip);
  }
  if (config->change_flags & PROP_CHANGE_ROI) {
    raspicamcontrol_set_ROI(camera, params->roi);
  }
  if (config->change_flags & PROP_CHANGE_ANNOTATION)
    update_annotation_data(state);
}
/* *INDENT-ON* */
