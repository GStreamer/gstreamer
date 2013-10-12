/*
 * Copyright (c) 2013 Jan Schmidt <jan@centricular.com>
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
 * \date 28th Feb 2013, 11 Oct 2013
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <sysexits.h>


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

#include <semaphore.h>

/// Camera number to use - we only have one camera, indexed from 0.
#define CAMERA_NUMBER 0

// Standard port setting for the camera component
#define MMAL_CAMERA_PREVIEW_PORT 0
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_CAPTURE_PORT 2

// Video format information
#define VIDEO_FRAME_RATE_NUM 30
#define VIDEO_FRAME_RATE_DEN 1

/// Video render needs at least 2 buffers.
#define VIDEO_OUTPUT_BUFFERS_NUM 3

// Max bitrate we allow for recording
const int MAX_BITRATE = 30000000; // 30Mbits/s

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

   MMAL_PORT_T *camera_still_port;
   MMAL_PORT_T *encoder_output_port;

   MMAL_POOL_T *encoder_pool; /// Pointer to the pool of buffers used by encoder output port

   PORT_USERDATA callback_data;
};

#if 0
/// Structure to cross reference H264 profile strings against the MMAL parameter equivalent
static XREF_T  profile_map[] =
{
   {"baseline",     MMAL_VIDEO_PROFILE_H264_BASELINE},
   {"main",         MMAL_VIDEO_PROFILE_H264_MAIN},
   {"high",         MMAL_VIDEO_PROFILE_H264_HIGH},
//   {"constrained",  MMAL_VIDEO_PROFILE_H264_CONSTRAINED_BASELINE} // Does anyone need this?
};

static int profile_map_size = sizeof(profile_map) / sizeof(profile_map[0]);


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

static COMMAND_LIST cmdline_commands[] =
{
   { CommandHelp,    "-help",       "?",  "This help information", 0 },
   { CommandWidth,   "-width",      "w",  "Set image width <size>. Default 1920", 1 },
   { CommandHeight,  "-height",     "h",  "Set image height <size>. Default 1080", 1 },
   { CommandBitrate, "-bitrate",    "b",  "Set bitrate. Use bits per second (e.g. 10MBits/s would be -b 10000000)", 1 },
   { CommandOutput,  "-output",     "o",  "Output filename <filename> (to write to stdout, use '-o -')", 1 },
   { CommandVerbose, "-verbose",    "v",  "Output verbose information during run", 0 },
   { CommandTimeout, "-timeout",    "t",  "Time (in ms) to capture for. If not specified, set to 5s. Zero to disable", 1 },
   { CommandDemoMode,"-demo",       "d",  "Run a demo mode (cycle through range of camera options, no capture)", 1},
   { CommandFramerate,"-framerate", "fps","Specify the frames per second to record", 1},
   { CommandPreviewEnc,"-penc",     "e",  "Display preview image *after* encoding (shows compression artifacts)", 0},
   { CommandIntraPeriod,"-intra",   "g",  "Specify the intra refresh period (key frame rate/GoP size)", 1},
   { CommandProfile,  "-profile",   "pf", "Specify H264 profile to use for encoding", 1},
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

   // Now set anything non-zero
   config->timeout = 5000;     // 5s delay before take image
   config->width = 1920;       // Default to 1080p
   config->height = 1080;
   config->bitrate = 17000000; // This is a decent default bitrate for 1080p
   config->framerate = VIDEO_FRAME_RATE_NUM;
   config->intraperiod = 0;    // Not set
   config->demoMode = 0;
   config->demoInterval = 250; // ms
   config->immutableInput = 1;
   config->profile = MMAL_VIDEO_PROFILE_H264_HIGH;

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
   if (!state)
   {
      vcos_assert(0);
      return;
   }

   fprintf(stderr, "Width %d, Height %d, filename %s\n", state->config.width, state->config.height, state->config.filename);
   fprintf(stderr, "bitrate %d, framerate %d, time delay %d\n", state->config.bitrate, state->config.framerate, state->config.timeout);
   //fprintf(stderr, "H264 Profile %s\n", raspicli_unmap_xref(state->config.profile, profile_map, profile_map_size));

   raspipreview_dump_parameters(&state->config.preview_parameters);
   //raspicamcontrol_dump_parameters(&state->config.camera_parameters);
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
               strncpy(state->filename, argv[i + 1], len);
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
            // TODO : What limits do we need for timeout?
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

      case CommandProfile: // H264 profile
      {
         state->profile = raspicli_map_xref(argv[i + 1], profile_map, profile_map_size);

         if( state->profile == -1)
            state->profile = MMAL_VIDEO_PROFILE_H264_HIGH;

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
      fprintf(stderr, "Invalid command line option (%s)\n", argv[i]);
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
   }
   else
   {
      vcos_log_error("Received unexpected camera control callback event, 0x%08x", buffer->cmd);
   }

   mmal_buffer_header_release(buffer);
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
   MMAL_BUFFER_HEADER_T *new_buffer;

   // We pass our file handle and other stuff in via the userdata field.

   PORT_USERDATA *pData = (PORT_USERDATA *)port->userdata;
   RASPIVID_STATE *state  = pData->state;

   if (pData)
   {
      int bytes_written = buffer->length;

      vcos_assert(state->output_file);

      if (buffer->length)
      {
         mmal_buffer_header_mem_lock(buffer);

         bytes_written = fwrite(buffer->data, 1, buffer->length, state->output_file);

         mmal_buffer_header_mem_unlock(buffer);
      }

      if (bytes_written != buffer->length)
      {
         vcos_log_error("Failed to write buffer data (%d from %d)- aborting", bytes_written, buffer->length);
         pData->abort = 1;
      }
   }
   else
   {
      vcos_log_error("Received a encoder buffer callback with no state");
   }

   // release buffer back to the pool
   mmal_buffer_header_release(buffer);

   // and send one back to the port (if still open)
   if (port->is_enabled)
   {
      MMAL_STATUS_T status = MMAL_SUCCESS;

      new_buffer = mmal_queue_get(state->encoder_pool->queue);

      if (new_buffer)
         status = mmal_port_send_buffer(port, new_buffer);

      if (!new_buffer || status != MMAL_SUCCESS)
         vcos_log_error("Unable to return a buffer to the encoder port");
   }
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
   MMAL_COMPONENT_T *camera = 0;
   MMAL_ES_FORMAT_T *format;
   MMAL_PORT_T *preview_port = NULL, *video_port = NULL, *still_port = NULL;
   MMAL_STATUS_T status;

   /* Create the component */
   status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Failed to create camera component");
      goto error;
   }

   if (!camera->output_num)
   {
      status = MMAL_ENOSYS;
      vcos_log_error("Camera doesn't have output ports");
      goto error;
   }

   preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
   video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
   still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];

   // Enable the camera, and tell it its control callback function
   status = mmal_port_enable(camera->control, camera_control_callback);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to enable control port : error %d", status);
      goto error;
   }

   //  set up the camera configuration
   {
      MMAL_PARAMETER_CAMERA_CONFIG_T cam_config =
      {
         { MMAL_PARAMETER_CAMERA_CONFIG, sizeof(cam_config) },
         .max_stills_w = state->config.width,
         .max_stills_h = state->config.height,
         .stills_yuv422 = 0,
         .one_shot_stills = 0,
         .max_preview_video_w = state->config.width,
         .max_preview_video_h = state->config.height,
         .num_preview_video_frames = 3,
         .stills_capture_circular_buffer_height = 0,
         .fast_preview_resume = 0,
         .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
      };
      mmal_port_parameter_set(camera->control, &cam_config.hdr);
   }

   // Now set up the port formats

   // Set the encode format on the Preview port
   // HW limitations mean we need the preview to be the same size as the required recorded output

   format = preview_port->format;

   format->encoding = MMAL_ENCODING_OPAQUE;
   format->encoding_variant = MMAL_ENCODING_I420;

   format->encoding = MMAL_ENCODING_OPAQUE;
   format->es->video.width = state->config.width;
   format->es->video.height = state->config.height;
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = state->config.width;
   format->es->video.crop.height = state->config.height;
   format->es->video.frame_rate.num = state->config.framerate;
   format->es->video.frame_rate.den = VIDEO_FRAME_RATE_DEN;

   status = mmal_port_format_commit(preview_port);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("camera viewfinder format couldn't be set");
      goto error;
   }

   // Set the encode format on the video  port

   format = video_port->format;
   format->encoding_variant = MMAL_ENCODING_I420;

   format->encoding = MMAL_ENCODING_OPAQUE;
   format->es->video.width = state->config.width;
   format->es->video.height = state->config.height;
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = state->config.width;
   format->es->video.crop.height = state->config.height;
   format->es->video.frame_rate.num = state->config.framerate;
   format->es->video.frame_rate.den = VIDEO_FRAME_RATE_DEN;

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

   format->es->video.width = state->config.width;
   format->es->video.height = state->config.height;
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = state->config.width;
   format->es->video.crop.height = state->config.height;
   format->es->video.frame_rate.num = 1;
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

   raspicamcontrol_set_all_parameters(camera, &state->config.camera_parameters);

   state->camera_component = camera;

   if (state->config.verbose)
      fprintf(stderr, "Camera component done\n");

   return status;

error:

   if (camera)
      mmal_component_destroy(camera);

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
   MMAL_POOL_T *pool;

   status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER, &encoder);

   if (status != MMAL_SUCCESS)
   {
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

   // Only supporting H264 at the moment
   encoder_output->format->encoding = MMAL_ENCODING_H264;

   encoder_output->format->bitrate = state->config.bitrate;

   encoder_output->buffer_size = encoder_output->buffer_size_recommended;

   if (encoder_output->buffer_size < encoder_output->buffer_size_min)
      encoder_output->buffer_size = encoder_output->buffer_size_min;

   encoder_output->buffer_num = encoder_output->buffer_num_recommended;

   if (encoder_output->buffer_num < encoder_output->buffer_num_min)
      encoder_output->buffer_num = encoder_output->buffer_num_min;

   // Commit the port changes to the output port
   status = mmal_port_format_commit(encoder_output);

   if (status != MMAL_SUCCESS)
   {
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

   if (state->config.intraperiod)
   {
      MMAL_PARAMETER_UINT32_T param = {{ MMAL_PARAMETER_INTRAPERIOD, sizeof(param)}, state->config.intraperiod};
      status = mmal_port_parameter_set(encoder_output, &param.hdr);
      if (status != MMAL_SUCCESS)
      {
         vcos_log_error("Unable to set intraperiod");
         goto error;
      }

   }

   {
      MMAL_PARAMETER_VIDEO_PROFILE_T  param;
      param.hdr.id = MMAL_PARAMETER_PROFILE;
      param.hdr.size = sizeof(param);

      param.profile[0].profile = state->config.profile;
      param.profile[0].level = MMAL_VIDEO_LEVEL_H264_4; // This is the only value supported

      status = mmal_port_parameter_set(encoder_output, &param.hdr);
      if (status != MMAL_SUCCESS)
      {
         vcos_log_error("Unable to set H264 profile");
         goto error;
      }
   }


   if (mmal_port_parameter_set_boolean(encoder_input, MMAL_PARAMETER_VIDEO_IMMUTABLE_INPUT, state->config.immutableInput) != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to set immutable input flag");
      // Continue rather than abort..
   }

   //  Enable component
   status = mmal_component_enable(encoder);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to enable video encoder component");
      goto error;
   }

   /* Create pool of buffer headers for the output port to consume */
   pool = mmal_port_pool_create(encoder_output, encoder_output->buffer_num, encoder_output->buffer_size);

   if (!pool)
   {
      vcos_log_error("Failed to create buffer header pool for encoder output port %s", encoder_output->name);
   }

   state->encoder_pool = pool;
   state->encoder_component = encoder;

   if (state->config.verbose)
      fprintf(stderr, "Encoder component done\n");

   return status;

   error:
   if (encoder)
      mmal_component_destroy(encoder);

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
   // Get rid of any port buffers first
   if (state->encoder_pool)
   {
      mmal_port_pool_destroy(state->encoder_component->output[0], state->encoder_pool);
   }

   if (state->encoder_component)
   {
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

void raspicapture_init()
{
   bcm_host_init();

   // Register our application with the logging system
   vcos_log_register("RaspiVid", VCOS_LOG_CATEGORY);
}

RASPIVID_STATE *
raspi_capture_start(RASPIVID_CONFIG *config)
{
  // Our main data storage vessel..
  RASPIVID_STATE *state;
  //int exit_code = EX_OK;

  MMAL_STATUS_T status = MMAL_SUCCESS;
  MMAL_PORT_T *camera_preview_port = NULL;
  MMAL_PORT_T *camera_video_port = NULL;
  MMAL_PORT_T *preview_input_port = NULL;
  MMAL_PORT_T *encoder_input_port = NULL;

  /* Default everything to zero */
  state = calloc(1, sizeof(RASPIVID_STATE));

  /* Apply passed in config */
  state->config = *config;

  if (state->config.verbose)
  {
     dump_state(state);
  }

  // OK, we have a nice set of parameters. Now set up our components
  // We have three components. Camera, Preview and encoder.

  if ((status = create_camera_component(state)) != MMAL_SUCCESS)
  {
     vcos_log_error("%s: Failed to create camera component", __func__);
     return NULL;
  }

  if ((status = raspipreview_create(&state->config.preview_parameters)) != MMAL_SUCCESS)
  {
     vcos_log_error("%s: Failed to create preview component", __func__);
     destroy_camera_component(state);
     return NULL;
  }

  if ((status = create_encoder_component(state)) != MMAL_SUCCESS)
  {
     vcos_log_error("%s: Failed to create encode component", __func__);
     raspipreview_destroy(&state->config.preview_parameters);
     destroy_camera_component(state);
     return NULL;
  }

  if (state->config.verbose)
     fprintf(stderr, "Starting component connection stage\n");

  camera_preview_port = state->camera_component->output[MMAL_CAMERA_PREVIEW_PORT];
  camera_video_port   = state->camera_component->output[MMAL_CAMERA_VIDEO_PORT];
  preview_input_port  = state->config.preview_parameters.preview_component->input[0];
  encoder_input_port  = state->encoder_component->input[0];
  state->camera_still_port   = state->camera_component->output[MMAL_CAMERA_CAPTURE_PORT];
  state->encoder_output_port = state->encoder_component->output[0];

  if (state->config.preview_parameters.wantPreview )
  {
     if (state->config.verbose)
     {
        fprintf(stderr, "Connecting camera preview port to preview input port\n");
        fprintf(stderr, "Starting video preview\n");
     }

     // Connect camera to preview
     status = connect_ports(camera_preview_port, preview_input_port, &state->preview_connection);
     if (status != MMAL_SUCCESS)
     {
        mmal_status_to_int(status);
        vcos_log_error("%s: Failed to connect camera to preview", __func__);
        goto error;
     }
  }

  if (state->config.verbose)
     fprintf(stderr, "Connecting camera stills port to encoder input port\n");

  // Now connect the camera to the encoder
  status = connect_ports(camera_video_port, encoder_input_port, &state->encoder_connection);

  if (status != MMAL_SUCCESS)
  {
     vcos_log_error("%s: Failed to connect camera video port to encoder input", __func__);
     goto error;
  }

  if (state->config.filename)
  {
     if (state->config.filename[0] == '-')
     {
        state->output_file = stdout;

        // Ensure we don't upset the output stream with diagnostics/info
        state->config.verbose = 0;
     }
     else
     {
        if (state->config.verbose)
           fprintf(stderr, "Opening output file \"%s\"\n", state->config.filename);

        state->output_file = fopen(state->config.filename, "wb");
     }

     if (!state->output_file)
     {
        // Notify user, carry on but discarding encoded output buffers
        vcos_log_error("%s: Error opening output file: %s\nNo output file will be generated\n", __func__, state->config.filename);
     }
  }

  // Set up our userdata - this is passed though to the callback where we need the information.
  state->callback_data.state = state;
  state->callback_data.abort = 0;

  state->encoder_output_port->userdata = (struct MMAL_PORT_USERDATA_T *)&state->callback_data;

  if (state->config.verbose)
     fprintf(stderr, "Enabling encoder output port\n");

  // Enable the encoder output port and tell it its callback function
  status = mmal_port_enable(state->encoder_output_port, encoder_buffer_callback);

  if (status != MMAL_SUCCESS)
  {
     vcos_log_error("Failed to setup encoder output");
     goto error;
  }

  if (state->config.demoMode)
  {
     // Run for the user specific time..
     int num_iterations = state->config.timeout / state->config.demoInterval;
     int i;

     if (state->config.verbose)
        fprintf(stderr, "Running in demo mode\n");

     for (i=0;state->config.timeout == 0 || i<num_iterations;i++)
     {
        raspicamcontrol_cycle_test(state->camera_component);
        vcos_sleep(state->config.demoInterval);
     }
  }
  else
  {
     // Only encode stuff if we have a filename and it opened
     if (state->output_file)
     {
        int wait;

        if (state->config.verbose)
           fprintf(stderr, "Starting video capture\n");

        if (mmal_port_parameter_set_boolean(camera_video_port, MMAL_PARAMETER_CAPTURE, 1) != MMAL_SUCCESS)
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

        for (wait = 0; state->config.timeout == 0 || wait < state->config.timeout; wait+= ABORT_INTERVAL)
        {
           vcos_sleep(ABORT_INTERVAL);
           if (state->callback_data.abort)
              break;
        }

        if (state->config.verbose)
           fprintf(stderr, "Finished capture\n");
     }
     else
     {
        if (state->config.timeout)
           vcos_sleep(state->config.timeout);
        else
           for (;;) vcos_sleep(ABORT_INTERVAL);
     }
  }

  return state;

error:
  raspi_capture_stop(state);

  if (status != MMAL_SUCCESS) {
    mmal_status_to_int(status);
    raspicamcontrol_check_configuration(128);
  }

  return NULL;
}

void
raspi_capture_stop(RASPIVID_STATE *state)
{
  if (state->config.verbose)
     fprintf(stderr, "Closing down\n");

  // Disable all our ports that are not handled by connections
  check_disable_port(state->camera_still_port);
  check_disable_port(state->encoder_output_port);

  if (state->config.preview_parameters.wantPreview )
     mmal_connection_destroy(state->preview_connection);
  mmal_connection_destroy(state->encoder_connection);

  // Can now close our file. Note disabling ports may flush buffers which causes
  // problems if we have already closed the file!
  if (state->output_file && state->output_file != stdout)
     fclose(state->output_file);

  /* Disable components */
  if (state->encoder_component)
     mmal_component_disable(state->encoder_component);

  if (state->config.preview_parameters.preview_component)
     mmal_component_disable(state->config.preview_parameters.preview_component);

  if (state->camera_component)
     mmal_component_disable(state->camera_component);

  destroy_encoder_component(state);
  raspipreview_destroy(&state->config.preview_parameters);
  destroy_camera_component(state);

  if (state->config.verbose)
     fprintf(stderr, "Close down completed, all components disconnected, disabled and destroyed\n\n");

  free(state);
}
