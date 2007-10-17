/*
 * camapplication.c - GStreamer CAM (EN50221) Application Layer
 * Copyright (C) 2007 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#include "camapplication.h"

#define GST_CAT_DEFAULT cam_debug_cat

static CamReturn open_session_request_cb (CamSL * sl,
    CamSLSession * session, CamSLResourceStatus * status);
static CamReturn session_opened_cb (CamSL * sl, CamSLSession * session);
static CamReturn session_closed_cb (CamSL * sl, CamSLSession * session);
static CamReturn session_data_cb (CamSL * sl,
    CamSLSession * session, guint8 * data, guint length);

static guint
resource_id_hash (gconstpointer key)
{
  guint resource_id = GPOINTER_TO_UINT (key);

  if (!CAM_AL_RESOURCE_ID_IS_PUBLIC (resource_id)) {
    /* private identifier, leave it as is */
    return resource_id;
  }

  /* public identifier, mask out the version number */
  return resource_id >> 6;
}

CamAL *
cam_al_new (CamSL * sl)
{
  CamAL *al = g_new0 (CamAL, 1);

  al->sl = sl;
  al->applications = g_hash_table_new (resource_id_hash, g_direct_equal);

  sl->user_data = al;
  sl->open_session_request = open_session_request_cb;
  sl->session_opened = session_opened_cb;
  sl->session_closed = session_closed_cb;
  sl->session_data = session_data_cb;

  return al;
}

void
cam_al_destroy (CamAL * al)
{
  g_hash_table_destroy (al->applications);
  g_free (al);
}

gboolean
cam_al_install (CamAL * al, CamALApplication * application)
{
  if (g_hash_table_lookup (al->applications,
          GINT_TO_POINTER (application->resource_id)) != NULL)
    return FALSE;

  application->al = al;

  g_hash_table_insert (al->applications,
      GINT_TO_POINTER (application->resource_id), application);

  return TRUE;
}

gboolean
cam_al_uninstall (CamAL * al, CamALApplication * application)
{
  gboolean ret;

  ret = g_hash_table_remove (al->applications,
      GINT_TO_POINTER (application->resource_id));

  return ret;
}

CamALApplication *
cam_al_get (CamAL * al, guint resource_id)
{
  return CAM_AL_APPLICATION (g_hash_table_lookup (al->applications,
          GINT_TO_POINTER (resource_id)));
}

void
_cam_al_application_init (CamALApplication * application)
{
  application->sessions = NULL;
}

void
_cam_al_application_destroy (CamALApplication * application)
{
  g_list_free (application->sessions);
}

static void
foreach_get_key (gpointer key, gpointer value, gpointer user_data)
{
  GList **lst = (GList **) user_data;

  *lst = g_list_append (*lst, key);
}

GList *
cam_al_get_resource_ids (CamAL * al)
{
  GList *resource_ids = NULL;

  g_hash_table_foreach (al->applications, foreach_get_key, &resource_ids);

  return resource_ids;
}

void
cam_al_calc_buffer_size (CamAL * al, guint body_length,
    guint * buffer_size, guint * offset)
{
  guint apdu_header_length;
  guint8 length_field_len;

  /* get the length of the lenght_field() */
  length_field_len = cam_calc_length_field_size (body_length);

  /* sum the APDU header */
  apdu_header_length = 3 + length_field_len;

  /* chain up to the session layer to get the size of the buffer that can
   * contain the whole APDU */
  cam_sl_calc_buffer_size (al->sl, apdu_header_length + body_length,
      buffer_size, offset);

  /* add the APDU header to the SPDU offset */
  *offset += apdu_header_length;
}

CamReturn
cam_al_application_write (CamALApplication * application,
    CamSLSession * session, guint tag, guint8 * buffer, guint buffer_size,
    guint body_length)
{
  guint length_field_len;
  guint apdu_header_length;
  guint8 *apdu;

  length_field_len = cam_calc_length_field_size (body_length);
  apdu_header_length = 3 + length_field_len;
  apdu = (buffer + buffer_size) - body_length - apdu_header_length;
  apdu[0] = tag >> 16;
  apdu[1] = (tag >> 8) & 0xFF;
  apdu[2] = tag & 0xFF;

  cam_write_length_field (&apdu[3], body_length);

  return cam_sl_session_write (session, buffer, buffer_size,
      apdu_header_length + body_length);
}

static CamReturn
open_session_request_cb (CamSL * sl, CamSLSession * session,
    CamSLResourceStatus * status)
{
  CamAL *al = CAM_AL (sl->user_data);
  CamALApplication *application;
  guint resource_id = session->resource_id;
  CamReturn ret;

  application = g_hash_table_lookup (al->applications,
      GINT_TO_POINTER (resource_id));
  if (application == NULL) {
    *status = CAM_SL_RESOURCE_STATUS_NOT_FOUND;

    return CAM_RETURN_OK;
  }

  if (CAM_AL_RESOURCE_ID_VERSION (application->resource_id)
      < CAM_AL_RESOURCE_ID_VERSION (resource_id)) {
    *status = CAM_SL_RESOURCE_STATUS_INVALID_VERSION;

    return CAM_RETURN_OK;
  }

  ret = application->session_request (application, session, status);
  if (CAM_FAILED (ret)) {
    *status = CAM_SL_RESOURCE_STATUS_NOT_FOUND;

    return ret;
  }

  if (*status == CAM_SL_RESOURCE_STATUS_OPEN) {
    session->user_data = application;
    application->sessions = g_list_append (application->sessions, session);
  }

  return CAM_RETURN_OK;
}

static CamReturn
session_opened_cb (CamSL * sl, CamSLSession * session)
{
  CamALApplication *application;

  application = CAM_AL_APPLICATION (session->user_data);
  if (application == NULL) {
    GST_ERROR ("session is established but has no application");
    return CAM_RETURN_APPLICATION_ERROR;
  }

  return application->open (application, session);
}

static CamReturn
session_closed_cb (CamSL * sl, CamSLSession * session)
{
  CamALApplication *application;
  CamReturn ret;
  GList *walk;

  application = CAM_AL_APPLICATION (session->user_data);
  if (application == NULL) {
    GST_ERROR ("session is established but has no application");
    return CAM_RETURN_APPLICATION_ERROR;
  }

  ret = application->close (application, session);
  for (walk = application->sessions; walk; walk = g_list_next (walk)) {
    CamSLSession *s = CAM_SL_SESSION (walk->data);

    if (s->session_nb == session->session_nb) {
      application->sessions = g_list_delete_link (application->sessions, walk);
      break;
    }
  }

  return ret;
}

static CamReturn
session_data_cb (CamSL * sl, CamSLSession * session, guint8 * data, guint size)
{
  CamALApplication *application;
  guint tag = 0;
  guint8 length_field_len;
  guint length;
  guint i;

  application = CAM_AL_APPLICATION (session->user_data);
  if (application == NULL) {
    GST_ERROR ("session is established but has no application");
    return CAM_RETURN_APPLICATION_ERROR;
  }

  if (size < 4) {
    GST_ERROR ("invalid APDU length %d", size);
    return CAM_RETURN_APPLICATION_ERROR;
  }

  for (i = 0; i < 3; ++i)
    tag = (tag << 8) | data[i];

  length_field_len = cam_read_length_field (&data[3], &length);

  if (length != size - 4) {
    GST_ERROR ("unexpected APDU length %d expected %d", length, size);

    return CAM_RETURN_APPLICATION_ERROR;
  }

  return application->data (application, session,
      tag, data + 3 + length_field_len, length);
}
