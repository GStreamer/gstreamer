/* GStreamer
 * Copyright (C) 2010 Wim Taymans <wim.taymans at gmail.com>
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

#include <string.h>

#include "rtsp-auth.h"

enum
{
  PROP_0,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_auth_debug);
#define GST_CAT_DEFAULT rtsp_auth_debug

static void gst_rtsp_auth_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_auth_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_auth_finalize (GObject * obj);

static gboolean default_setup_auth (GstRTSPAuth * auth, GstRTSPClient * client,
    GQuark hint, GstRTSPClientState * state);
static gboolean default_check_method (GstRTSPAuth * auth,
    GstRTSPClient * client, GQuark hint, GstRTSPClientState * state);

G_DEFINE_TYPE (GstRTSPAuth, gst_rtsp_auth, G_TYPE_OBJECT);

static void
gst_rtsp_auth_class_init (GstRTSPAuthClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_auth_get_property;
  gobject_class->set_property = gst_rtsp_auth_set_property;
  gobject_class->finalize = gst_rtsp_auth_finalize;

  klass->setup_auth = default_setup_auth;
  klass->check_method = default_check_method;

  GST_DEBUG_CATEGORY_INIT (rtsp_auth_debug, "rtspauth", 0, "GstRTSPAuth");
}

static void
gst_rtsp_auth_init (GstRTSPAuth * auth)
{
  /* bitwise or of all methods that need authentication */
  auth->methods = GST_RTSP_DESCRIBE |
      GST_RTSP_ANNOUNCE |
      GST_RTSP_GET_PARAMETER |
      GST_RTSP_SET_PARAMETER |
      GST_RTSP_PAUSE |
      GST_RTSP_PLAY | GST_RTSP_RECORD | GST_RTSP_SETUP | GST_RTSP_TEARDOWN;
}

static void
gst_rtsp_auth_finalize (GObject * obj)
{
  GstRTSPAuth *auth = GST_RTSP_AUTH (obj);

  GST_INFO ("finalize auth %p", auth);
  g_free (auth->basic);

  G_OBJECT_CLASS (gst_rtsp_auth_parent_class)->finalize (obj);
}

static void
gst_rtsp_auth_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPAuth *auth = GST_RTSP_AUTH (object);

  switch (propid) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_auth_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPAuth *auth = GST_RTSP_AUTH (object);

  switch (propid) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_auth_new:
 *
 * Create a new #GstRTSPAuth instance.
 *
 * Returns: a new #GstRTSPAuth
 */
GstRTSPAuth *
gst_rtsp_auth_new (void)
{
  GstRTSPAuth *result;

  result = g_object_new (GST_TYPE_RTSP_AUTH, NULL);

  return result;
}

/**
 * gst_rtsp_auth_set_basic:
 * @auth: a #GstRTSPAuth
 * @basic: the basic token
 *
 * Set the basic token for the default authentication algorithm.
 */
void
gst_rtsp_auth_set_basic (GstRTSPAuth * auth, const gchar * basic)
{
  g_free (auth->basic);
  auth->basic = g_strdup (basic);
}

static gboolean
default_setup_auth (GstRTSPAuth * auth, GstRTSPClient * client,
    GQuark hint, GstRTSPClientState * state)
{
  if (state->response == NULL)
    return FALSE;

  /* we only have Basic for now */
  gst_rtsp_message_add_header (state->response, GST_RTSP_HDR_WWW_AUTHENTICATE,
      "Basic realm=\"GStreamer RTSP Server\"");

  return TRUE;
}

/**
 * gst_rtsp_auth_setup_auth:
 * @auth: a #GstRTSPAuth
 * @client: the client
 * @uri: the requested uri
 * @session: the session
 * @request: the request
 * @response: the response
 *
 * Add authentication tokens to @response.
 *
 * Returns: FALSE if something is wrong.
 */
gboolean
gst_rtsp_auth_setup_auth (GstRTSPAuth * auth, GstRTSPClient * client,
    GQuark hint, GstRTSPClientState * state)
{
  gboolean result = FALSE;
  GstRTSPAuthClass *klass;

  klass = GST_RTSP_AUTH_GET_CLASS (auth);

  GST_DEBUG_OBJECT (auth, "setup auth");

  if (klass->setup_auth)
    result = klass->setup_auth (auth, client, hint, state);

  return result;
}

static gboolean
default_check_method (GstRTSPAuth * auth, GstRTSPClient * client,
    GQuark hint, GstRTSPClientState * state)
{
  gboolean result = TRUE;
  GstRTSPResult res;

  if (state->method & auth->methods != 0) {
    gchar *authorization;

    result = FALSE;

    res =
        gst_rtsp_message_get_header (state->request, GST_RTSP_HDR_AUTHORIZATION,
        &authorization, 0);
    if (res < 0)
      goto no_auth;

    /* parse type */
    if (g_ascii_strncasecmp (authorization, "basic ", 6) == 0) {
      GST_DEBUG_OBJECT (auth, "check Basic auth");
      if (auth->basic && strcmp (&authorization[6], auth->basic) == 0)
        result = TRUE;
    } else if (g_ascii_strncasecmp (authorization, "digest ", 7) == 0) {
      GST_DEBUG_OBJECT (auth, "check Digest auth");
      /* not implemented yet */
      result = FALSE;
    }
  }
  return result;

no_auth:
  {
    GST_DEBUG_OBJECT (auth, "no authorization header found");
    return FALSE;
  }
}

/**
 * gst_rtsp_auth_check_method:
 * @auth: a #GstRTSPAuth
 * @client: the client
 * @hint: a hint
 * @state: client state
 *
 * Check if @client is allowed to perform the actions of @state.
 *
 * Returns: FALSE if the action is not allowed.
 */
gboolean
gst_rtsp_auth_check (GstRTSPAuth * auth, GstRTSPClient * client,
    GQuark hint, GstRTSPClientState * state)
{
  gboolean result = FALSE;
  GstRTSPAuthClass *klass;

  klass = GST_RTSP_AUTH_GET_CLASS (auth);

  GST_DEBUG_OBJECT (auth, "check state");

  if (klass->check_method)
    result = klass->check_method (auth, client, hint, state);

  return result;
}

/**
 * gst_rtsp_auth_make_basic:
 * @user: a userid
 * @pass: a password
 *
 * Construct a Basic authorisation token from @user and @pass.
 *
 * Returns: the base64 encoding of the string @user:@pass. g_free()
 *    after usage.
 */
gchar *
gst_rtsp_auth_make_basic (const gchar * user, const gchar * pass)
{
  gchar *user_pass;
  gchar *result;

  user_pass = g_strjoin (":", user, pass, NULL);
  result = g_base64_encode ((guchar *) user_pass, strlen (user_pass));
  g_free (user_pass);

  return result;
}
