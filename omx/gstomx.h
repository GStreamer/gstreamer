/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2013, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_OMX_H__
#define __GST_OMX_H__

#include <gmodule.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef GST_OMX_STRUCT_PACKING
# if GST_OMX_STRUCT_PACKING == 1
#  pragma pack(1)
# elif GST_OMX_STRUCT_PACKING == 2
#  pragma pack(2)
# elif GST_OMX_STRUCT_PACKING == 4
#  pragma pack(4)
# elif GST_OMX_STRUCT_PACKING == 8
#  pragma pack(8)
# else
#  error "Unsupported struct packing value"
# endif
#endif

/* If the component may signal EOS before it has finished pushing
 * out all of its buffers. Happens with egl_render on the rpi.
 */
#define GST_OMX_HACK_SIGNALS_PREMATURE_EOS                            G_GUINT64_CONSTANT (0x0000000000000400)

#include <OMX_Core.h>
#include <OMX_Component.h>

#ifdef USE_OMX_TARGET_RPI
#include <OMX_Broadcom.h>
#endif

#ifdef HAVE_VIDEO_EXT
#include <OMX_VideoExt.h>
#endif

#ifdef HAVE_INDEX_EXT
#include <OMX_IndexExt.h>
#endif

#ifdef HAVE_COMPONENT_EXT
#include <OMX_ComponentExt.h>
#endif

#ifdef HAVE_CORE_EXT
#include <OMX_CoreExt.h>
#endif

#ifdef HAVE_AUDIO_EXT
#include <OMX_AudioExt.h>
#endif

#ifdef HAVE_IV_COMMON_EXT
#include <OMX_IVCommonExt.h>
#endif

#ifdef HAVE_IMAGE_EXT
#include <OMX_ImageExt.h>
#endif

#ifdef HAVE_OTHER_EXT
#include <OMX_OtherExt.h>
#endif

#ifdef GST_OMX_STRUCT_PACKING
#pragma pack()
#endif

G_BEGIN_DECLS

#define GST_OMX_INIT_STRUCT(st) G_STMT_START { \
  memset ((st), 0, sizeof (*(st))); \
  (st)->nSize = sizeof (*(st)); \
  (st)->nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
  (st)->nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
  (st)->nVersion.s.nRevision = OMX_VERSION_REVISION; \
  (st)->nVersion.s.nStep = OMX_VERSION_STEP; \
} G_STMT_END

#ifdef OMX_SKIP64BIT
#define GST_OMX_GET_TICKS(ticks) ((((guint64) (ticks).nHighPart) << 32) | ((ticks).nLowPart))
#define GST_OMX_SET_TICKS(ticks, i) G_STMT_START { \
  ticks.nLowPart = ((guint64) (i)) & 0xffffffff; \
  ticks.nHighPart = ((guint64) (i)) >> 32; \
} G_STMT_END
#else
#define GST_OMX_GET_TICKS(ticks) (ticks)
#define GST_OMX_SET_TICKS(ticks, i) G_STMT_START { \
  ticks = i; \
} G_STMT_END
#endif

/* If set on an element property means "use the OMX default value".
 * If set on a default_* variable means that the default values hasn't been
 * retrieved from OMX yet. */
#define GST_OMX_PROP_OMX_DEFAULT G_MAXUINT32

/* OMX_StateInvalid does not exist in 1.2.0 spec. The initial state is now
 * StateLoaded. Problem is that gst-omx still needs an initial state different
 * than StateLoaded. Otherwise gst_omx_component_set_state(StateLoaded) will
 * early return because it will think it is already in StateLoaded. Also note
 * that there is no call to gst_omx_component_set_state(StateInvalid) so this
 * also shows that StateInvalid is used as a helper in gst-omx.
 */
#if OMX_VERSION_MINOR == 2
#define OMX_StateInvalid OMX_StateReserved_0x00000000
#endif

/* Different hacks that are required to work around
 * bugs in different OpenMAX implementations
 */
/* In the EventSettingsChanged callback use nData2 instead of nData1 for
 * the port index. Happens with Bellagio.
 */
#define GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_NDATA_PARAMETER_SWAP G_GUINT64_CONSTANT (0x0000000000000001)
/* In the EventSettingsChanged callback assume that port index 0 really
 * means port index 1. Happens with the Bellagio ffmpegdist video decoder.
 */
#define GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_PORT_0_TO_1          G_GUINT64_CONSTANT (0x0000000000000002)
/* If the video framerate is not specified as fraction (Q.16) but as
 * integer number. Happens with the Bellagio ffmpegdist video encoder.
 */
#define GST_OMX_HACK_VIDEO_FRAMERATE_INTEGER                          G_GUINT64_CONSTANT (0x0000000000000004)
/* If the SYNCFRAME flag on encoder output buffers is not used and we
 * have to assume that all frames are sync frames.
 * Happens with the Bellagio ffmpegdist video encoder.
 */
#define GST_OMX_HACK_SYNCFRAME_FLAG_NOT_USED                          G_GUINT64_CONSTANT (0x0000000000000008)
/* If the component needs to be re-created if the caps change.
 * Happens with Qualcomm's OpenMAX implementation.
 */
#define GST_OMX_HACK_NO_COMPONENT_RECONFIGURE                         G_GUINT64_CONSTANT (0x0000000000000010)

/* If the component does not accept empty EOS buffers.
 * Happens with Qualcomm's OpenMAX implementation.
 */
#define GST_OMX_HACK_NO_EMPTY_EOS_BUFFER                              G_GUINT64_CONSTANT (0x0000000000000020)

/* If the component might not acknowledge a drain.
 * Happens with TI's Ducati OpenMAX implementation.
 */
#define GST_OMX_HACK_DRAIN_MAY_NOT_RETURN                             G_GUINT64_CONSTANT (0x0000000000000040)

/* If the component doesn't allow any component role to be set.
 * Happens with Broadcom's OpenMAX implementation.
 */
#define GST_OMX_HACK_NO_COMPONENT_ROLE                                G_GUINT64_CONSTANT (0x0000000000000080)

/* If the component doesn't allow disabling the outport while
 * when setting the format until the output format is known.
 */
#define GST_OMX_HACK_NO_DISABLE_OUTPORT                               G_GUINT64_CONSTANT (0x0000000000000100)

/* If the encoder requires input buffers that have a height
 * which is a multiple of 16 pixels
 */
#define GST_OMX_HACK_HEIGHT_MULTIPLE_16             G_GUINT64_CONSTANT (0x0000000000000200)

/* If we should pass the profile/level information from upstream to the
 * OMX decoder. This is a violation of the OMX spec as
 * OMX_IndexParamVideoProfileLevelCurrent is supposed to be r-o so
 * do it as a platform specific hack.
 */
#define GST_OMX_HACK_PASS_PROFILE_TO_DECODER        G_GUINT64_CONSTANT (0x0000000000000800)

/* If we should pass the color format information from upstream to the
 * OMX decoder input. This is a violation of the OMX spec as
 * the eColorFormat field is supposed to only be used if eCompressionFormat is
 * set to OMX_IMAGE_CodingUnused.
 * Do this as a platform specific hack for OMX implementation which may use
 * this information to pre-allocate internal buffers for example.
 */
#define GST_OMX_HACK_PASS_COLOR_FORMAT_TO_DECODER        G_GUINT64_CONSTANT (0x0000000000001000)

/* If set, automatically update nBufferCountActual to nBufferCountMin before
 * allocating buffers. This can be used on OMX implementation decreasing
 * nBufferCountMin depending of the format and so can reduce the number
 * of allocated buffers.
 */
#define GST_OMX_HACK_ENSURE_BUFFER_COUNT_ACTUAL          G_GUINT64_CONSTANT (0x0000000000002000)

typedef struct _GstOMXCore GstOMXCore;
typedef struct _GstOMXPort GstOMXPort;
typedef enum _GstOMXPortDirection GstOMXPortDirection;
typedef struct _GstOMXComponent GstOMXComponent;
typedef struct _GstOMXBuffer GstOMXBuffer;
typedef struct _GstOMXClassData GstOMXClassData;
typedef struct _GstOMXMessage GstOMXMessage;

typedef enum {
  /* Everything good and the buffer is valid */
  GST_OMX_ACQUIRE_BUFFER_OK = 0,
  /* The port is flushing, exit ASAP */
  GST_OMX_ACQUIRE_BUFFER_FLUSHING,
  /* The port must be reconfigured */
  GST_OMX_ACQUIRE_BUFFER_RECONFIGURE,
  /* The port is EOS */
  GST_OMX_ACQUIRE_BUFFER_EOS,
  /* A fatal error happened */
  GST_OMX_ACQUIRE_BUFFER_ERROR,
  /* No buffer is currently available (used when calling gst_omx_port_acquire_buffer() in not waiting mode) */
  GST_OMX_ACQUIRE_BUFFER_NO_AVAILABLE,
} GstOMXAcquireBufferReturn;

struct _GstOMXCore {
  /* Handle to the OpenMAX IL core shared library */
  GModule *module;

  /* Current number of users, transitions from/to 0
   * call init/deinit */
  GMutex lock;
  gint user_count; /* LOCK */

  /* OpenMAX core library functions, protected with LOCK */
  OMX_ERRORTYPE (*init) (void);
  OMX_ERRORTYPE (*deinit) (void);
  OMX_ERRORTYPE (*get_handle) (OMX_HANDLETYPE * handle,
      OMX_STRING name, OMX_PTR data, OMX_CALLBACKTYPE * callbacks);
  OMX_ERRORTYPE (*free_handle) (OMX_HANDLETYPE handle);
  OMX_ERRORTYPE (*setup_tunnel) (OMX_HANDLETYPE output, OMX_U32 outport, OMX_HANDLETYPE input, OMX_U32 inport);
};

typedef enum {
  GST_OMX_MESSAGE_STATE_SET,
  GST_OMX_MESSAGE_FLUSH,
  GST_OMX_MESSAGE_ERROR,
  GST_OMX_MESSAGE_PORT_ENABLE,
  GST_OMX_MESSAGE_PORT_SETTINGS_CHANGED,
  GST_OMX_MESSAGE_BUFFER_FLAG,
  GST_OMX_MESSAGE_BUFFER_DONE,
} GstOMXMessageType;

typedef enum {
  GST_OMX_COMPONENT_TYPE_SINK,
  GST_OMX_COMPONENT_TYPE_SOURCE,
  GST_OMX_COMPONENT_TYPE_FILTER
} GstOmxComponentType;

/* How the port's buffers are allocated */
typedef enum {
  GST_OMX_BUFFER_ALLOCATION_ALLOCATE_BUFFER,
  GST_OMX_BUFFER_ALLOCATION_USE_BUFFER,
  GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC, /* Only supported by OMX 1.2.0 */
} GstOMXBufferAllocation;

typedef enum {
  GST_OMX_WAIT,
  GST_OMX_DONT_WAIT,
} GstOMXWait;

struct _GstOMXMessage {
  GstOMXMessageType type;

  union {
    struct {
      OMX_STATETYPE state;
    } state_set;
    struct {
      OMX_U32 port;
    } flush;
    struct {
      OMX_ERRORTYPE error;
    } error;
    struct {
      OMX_U32 port;
      OMX_BOOL enable;
    } port_enable;
    struct {
      OMX_U32 port;
    } port_settings_changed;
    struct {
      OMX_U32 port;
      OMX_U32 flags;
    } buffer_flag;
    struct {
      OMX_HANDLETYPE component;
      OMX_PTR app_data;
      OMX_BUFFERHEADERTYPE *buffer;
      OMX_BOOL empty;
    } buffer_done;
  } content;
};

struct _GstOMXPort {
  GstOMXComponent *comp;
  guint32 index;

  gboolean tunneled;

  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  GPtrArray *buffers; /* Contains GstOMXBuffer* */
  GQueue pending_buffers; /* Contains GstOMXBuffer* */
  gboolean flushing;
  gboolean flushed; /* TRUE after OMX_CommandFlush was done */
  gboolean enabled_pending;  /* TRUE after OMX_Command{En,Dis}able */
  gboolean disabled_pending; /* was done until it took effect */
  gboolean eos; /* TRUE after a buffer with EOS flag was received */
  GstOMXBufferAllocation allocation;
  gboolean using_pool; /* TRUE if the buffers of this port are managed by a pool */

  /* Increased whenever the settings of these port change.
   * If settings_cookie != configured_settings_cookie
   * the port has to be reconfigured.
   */
  gint settings_cookie;
  gint configured_settings_cookie;
};

struct _GstOMXComponent {
  GstMiniObject mini_object;

  GstObject *parent;

  gchar *name; /* for debugging mostly */

  OMX_HANDLETYPE handle;
  GstOMXCore *core;

  guint64 hacks; /* Flags, GST_OMX_HACK_* */

  /* Added once, never changed. No locks necessary */
  GPtrArray *ports; /* Contains GstOMXPort* */
  gint n_in_ports, n_out_ports;

  /* Locking order: lock -> messages_lock
   *
   * Never hold lock while waiting for messages_cond
   * Always check that messages is empty before waiting */
  GMutex lock;

  GQueue messages; /* Queue of GstOMXMessages */
  GMutex messages_lock;
  GCond messages_cond;

  OMX_STATETYPE state;
  /* OMX_StateInvalid if no pending state */
  OMX_STATETYPE pending_state;
  /* OMX_ErrorNone usually, if different nothing will work */
  OMX_ERRORTYPE last_error;

  GList *pending_reconfigure_outports;
};

struct _GstOMXBuffer {
  GstOMXPort *port;
  OMX_BUFFERHEADERTYPE *omx_buf;

  /* TRUE if the buffer is used by the port, i.e.
   * between {Empty,Fill}ThisBuffer and the callback
   */
  gboolean used;

  /* Cookie of the settings when this buffer was allocated */
  gint settings_cookie;

  /* TRUE if this is an EGLImage */
  gboolean eglimage;

  /* Used in dynamic buffer mode to keep track of the mapped content while it's
   * being processed by the OMX component. */
  GstVideoFrame input_frame;
  gboolean input_frame_mapped; /* TRUE if input_frame is valid */
  GstMemory *input_mem;
  GstBuffer *input_buffer;
  gboolean input_buffer_mapped;
  GstMapInfo map;
};

struct _GstOMXClassData {
  const gchar *core_name;
  const gchar *component_name;
  const gchar *component_role;

  const gchar *default_src_template_caps;
  const gchar *default_sink_template_caps;

  guint32 in_port_index, out_port_index;

  guint64 hacks;

  GstOmxComponentType type;
};

GKeyFile *        gst_omx_get_configuration (void);

const gchar *     gst_omx_error_to_string (OMX_ERRORTYPE err);
const gchar *     gst_omx_state_to_string (OMX_STATETYPE state);
const gchar *     gst_omx_command_to_string (OMX_COMMANDTYPE cmd);
const gchar *     gst_omx_buffer_flags_to_string (guint32 flags);

guint64           gst_omx_parse_hacks (gchar ** hacks);

GstOMXCore *      gst_omx_core_acquire (const gchar * filename);
void              gst_omx_core_release (GstOMXCore * core);

GType             gst_omx_component_get_type (void);

GstOMXComponent * gst_omx_component_new (GstObject * parent, const gchar *core_name, const gchar *component_name, const gchar * component_role, guint64 hacks);
GstOMXComponent * gst_omx_component_ref   (GstOMXComponent * comp);
void              gst_omx_component_unref (GstOMXComponent * comp);

OMX_ERRORTYPE     gst_omx_component_set_state (GstOMXComponent * comp, OMX_STATETYPE state);
OMX_STATETYPE     gst_omx_component_get_state (GstOMXComponent * comp, GstClockTime timeout);

OMX_ERRORTYPE     gst_omx_component_get_last_error (GstOMXComponent * comp);
const gchar *     gst_omx_component_get_last_error_string (GstOMXComponent * comp);

GstOMXPort *      gst_omx_component_add_port (GstOMXComponent * comp, guint32 index);
GstOMXPort *      gst_omx_component_get_port (GstOMXComponent * comp, guint32 index);

OMX_ERRORTYPE     gst_omx_component_get_parameter (GstOMXComponent * comp, OMX_INDEXTYPE index, gpointer param);
OMX_ERRORTYPE     gst_omx_component_set_parameter (GstOMXComponent * comp, OMX_INDEXTYPE index, gpointer param);

OMX_ERRORTYPE     gst_omx_component_get_config (GstOMXComponent * comp, OMX_INDEXTYPE index, gpointer config);
OMX_ERRORTYPE     gst_omx_component_set_config (GstOMXComponent * comp, OMX_INDEXTYPE index, gpointer config);

OMX_ERRORTYPE     gst_omx_setup_tunnel (GstOMXPort * port1, GstOMXPort * port2);
OMX_ERRORTYPE     gst_omx_close_tunnel (GstOMXPort * port1, GstOMXPort * port2);


OMX_ERRORTYPE     gst_omx_port_get_port_definition (GstOMXPort * port, OMX_PARAM_PORTDEFINITIONTYPE * port_def);
OMX_ERRORTYPE     gst_omx_port_update_port_definition (GstOMXPort *port, OMX_PARAM_PORTDEFINITIONTYPE *port_definition);

GstOMXAcquireBufferReturn gst_omx_port_acquire_buffer (GstOMXPort *port, GstOMXBuffer **buf, GstOMXWait wait);
OMX_ERRORTYPE     gst_omx_port_release_buffer (GstOMXPort *port, GstOMXBuffer *buf);

OMX_ERRORTYPE     gst_omx_port_set_flushing (GstOMXPort *port, GstClockTime timeout, gboolean flush);
gboolean          gst_omx_port_is_flushing (GstOMXPort *port);

OMX_ERRORTYPE     gst_omx_port_allocate_buffers (GstOMXPort *port);
OMX_ERRORTYPE     gst_omx_port_use_buffers (GstOMXPort *port, const GList *buffers);
OMX_ERRORTYPE     gst_omx_port_use_eglimages (GstOMXPort *port, const GList *images);
OMX_ERRORTYPE     gst_omx_port_deallocate_buffers (GstOMXPort *port);
OMX_ERRORTYPE     gst_omx_port_populate (GstOMXPort *port);
OMX_ERRORTYPE     gst_omx_port_wait_buffers_released (GstOMXPort * port, GstClockTime timeout);
void              gst_omx_port_requeue_buffer (GstOMXPort * port, GstOMXBuffer * buf);

OMX_ERRORTYPE     gst_omx_port_mark_reconfigured (GstOMXPort * port);

OMX_ERRORTYPE     gst_omx_port_set_enabled (GstOMXPort * port, gboolean enabled);
OMX_ERRORTYPE     gst_omx_port_wait_enabled (GstOMXPort * port, GstClockTime timeout);
gboolean          gst_omx_port_is_enabled (GstOMXPort * port);
gboolean          gst_omx_port_ensure_buffer_count_actual (GstOMXPort * port, guint extra);
gboolean          gst_omx_port_update_buffer_count_actual (GstOMXPort * port, guint nb);

gboolean          gst_omx_port_set_dmabuf (GstOMXPort * port, gboolean dmabuf);
gboolean          gst_omx_port_set_subframe (GstOMXPort * port, gboolean enabled);
gboolean          gst_omx_port_get_subframe (GstOMXPort * port);

/* OMX 1.2.0 dynamic allocation mode */
gboolean          gst_omx_is_dynamic_allocation_supported (void);
OMX_ERRORTYPE     gst_omx_port_use_dynamic_buffers (GstOMXPort * port);
gboolean          gst_omx_buffer_map_frame (GstOMXBuffer * buffer, GstBuffer * input, GstVideoInfo * info);
gboolean          gst_omx_buffer_map_memory (GstOMXBuffer * buffer, GstMemory * mem);
gboolean          gst_omx_buffer_map_buffer (GstOMXBuffer * buffer, GstBuffer * input);
gboolean          gst_omx_buffer_import_fd (GstOMXBuffer * buffer, GstBuffer * input);

void              gst_omx_set_default_role (GstOMXClassData *class_data, const gchar *default_role);

/* refered by plugin_init */
GST_DEBUG_CATEGORY_EXTERN (gst_omx_video_debug_category);

G_END_DECLS

#endif /* __GST_OMX_H__ */
