/* GStreamer
 *
 * Copyright (C) 2014 YouView TV Ltd
 *  Authors: Mariusz Buras <mariusz.buras@youview.com>
 *           Mathieu Duponchelle <mathieu.duponchelle@collabora.com>
 *
 * socket_interposer.c : overrides for standard socket functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "../../gst/validate/gst-validate-scenario.h"

#if defined(__gnu_linux__) && !defined(__ANDROID__) && !defined (ANDROID)

#include <sys/socket.h>
#include <netinet/ip.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#define MAX_CALLBACKS (16)

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
/* Return 0 to remove the callback immediately */
typedef int (*socket_interposer_callback) (void *, const void *, size_t);

struct
{
  socket_interposer_callback callback;
  void *userdata;
  struct sockaddr_in sockaddr;
  int fd;
} callbacks[MAX_CALLBACKS];

static int
socket_interposer_remove_callback_unlocked (struct sockaddr_in *addrin,
    socket_interposer_callback callback, void *userdata)
{
  size_t i;
  for (i = 0; i < MAX_CALLBACKS; i++) {
    if (callbacks[i].callback == callback
        && callbacks[i].userdata == userdata
        && callbacks[i].sockaddr.sin_addr.s_addr == addrin->sin_addr.s_addr
        && callbacks[i].sockaddr.sin_port == addrin->sin_port) {
      memset (&callbacks[i], 0, sizeof (callbacks[0]));
      return 1;
    }
  }
  return 0;
}

static void
socket_interposer_set_callback (struct sockaddr_in *addrin,
    socket_interposer_callback callback, void *userdata)
{
  size_t i;
  pthread_mutex_lock (&mutex);


  socket_interposer_remove_callback_unlocked (addrin, callback, userdata);
  for (i = 0; i < MAX_CALLBACKS; i++) {
    if (callbacks[i].callback == NULL) {
      callbacks[i].callback = callback;
      callbacks[i].userdata = userdata;
      memcpy (&callbacks[i].sockaddr, addrin, sizeof (struct sockaddr_in));
      callbacks[i].fd = -1;
      break;
    }
  }
  pthread_mutex_unlock (&mutex);
}

int
connect (int socket, const struct sockaddr_in *addrin, socklen_t address_len)
{
  size_t i;
  int override_errno = 0;
  typedef ssize_t (*real_connect_fn) (int, const struct sockaddr_in *,
      socklen_t);
  static real_connect_fn real_connect = 0;
  ssize_t ret = 0;

  pthread_mutex_lock (&mutex);

  for (i = 0; i < MAX_CALLBACKS; i++) {
    if (callbacks[i].sockaddr.sin_addr.s_addr == addrin->sin_addr.s_addr
        && callbacks[i].sockaddr.sin_port == addrin->sin_port) {

      callbacks[i].fd = socket;

      if (callbacks[i].callback) {
        int ret = callbacks[i].callback (callbacks[i].userdata, NULL,
            0);
        if (ret != 0)
          override_errno = ret;
        else                    /* Remove the callback */
          memset (&callbacks[i], 0, sizeof (callbacks[0]));
      }

      break;
    }
  }

  pthread_mutex_unlock (&mutex);

  if (!real_connect) {
    real_connect = (real_connect_fn) dlsym (RTLD_NEXT, "connect");
  }

  if (!override_errno) {
    ret = real_connect (socket, addrin, address_len);
  } else {
    // override errno
    errno = override_errno;
    ret = -1;
  }
  return ret;
}

ssize_t
send (int socket, const void *buffer, size_t len, int flags)
{
  size_t i;
  int override_errno = 0;
  typedef ssize_t (*real_send_fn) (int, const void *, size_t, int);
  ssize_t ret;
  static real_send_fn real_send = 0;

  pthread_mutex_lock (&mutex);
  for (i = 0; i < MAX_CALLBACKS; i++) {
    if (callbacks[i].fd != 0 && callbacks[i].fd == socket) {
      int ret = callbacks[i].callback (callbacks[i].userdata, buffer,
          len);

      if (ret != 0)
        override_errno = ret;
      else                      /* Remove the callback */
        memset (&callbacks[i], 0, sizeof (callbacks[0]));

      break;
    }
  }
  pthread_mutex_unlock (&mutex);

  if (!real_send) {
    real_send = (real_send_fn) dlsym (RTLD_NEXT, "send");
  }

  ret = real_send (socket, buffer, len, flags);

  // override errno
  if (override_errno != 0) {
    errno = override_errno;
    ret = -1;
  }

  return ret;

}

ssize_t
recv (int socket, void *buffer, size_t length, int flags)
{
  size_t i;
  int old_errno;
  typedef ssize_t (*real_recv_fn) (int, void *, size_t, int);
  ssize_t ret;
  static real_recv_fn real_recv = 0;

  if (!real_recv) {
    real_recv = (real_recv_fn) dlsym (RTLD_NEXT, "recv");
  }

  ret = real_recv (socket, buffer, length, flags);
  old_errno = errno;

  pthread_mutex_lock (&mutex);
  for (i = 0; i < MAX_CALLBACKS; i++) {
    if (callbacks[i].fd != 0 && callbacks[i].fd == socket) {
      int newerrno = callbacks[i].callback (callbacks[i].userdata, buffer,
          ret);

      // override errno
      if (newerrno != 0) {
        old_errno = newerrno;
        ret = -1;
      } else {                  /* Remove the callback */
        memset (&callbacks[i], 0, sizeof (callbacks[0]));
      }

      break;
    }
  }
  pthread_mutex_unlock (&mutex);

  errno = old_errno;

  return ret;
}

struct errno_entry
{
  const gchar *str;
  int _errno;
};

static struct errno_entry errno_map[] = {
  {"ECONNABORTED", ECONNABORTED},
  {"ECONNRESET", ECONNRESET},
  {"ENETRESET", ENETRESET},
  {"ECONNREFUSED", ECONNREFUSED},
  {"EHOSTUNREACH", EHOSTUNREACH},
  {"EHOSTDOWN", EHOSTDOWN},
  {NULL, 0},
};

static int
socket_callback_ (GstValidateAction * action, const void *buff, size_t len)
{
  gint times;
  gint real_errno;

  gst_structure_get_int (action->structure, "times", &times);
  gst_structure_get_int (action->structure, "real_errno", &real_errno);

  times -= 1;
  gst_structure_set (action->structure, "times", G_TYPE_INT, times, NULL);
  if (times <= 0) {
    gst_validate_action_set_done (action);
    return 0;
  }

  return real_errno;
}

static gint
errno_string_to_int (const gchar * errno_str)
{
  gint i;

  for (i = 0; errno_map[i]._errno; i += 1) {
    if (!g_ascii_strcasecmp (errno_map[i].str, errno_str))
      return errno_map[i]._errno;
  }

  return 0;
}

static gboolean
_fault_injector_loaded (void)
{
  const gchar *ld_preload = g_getenv ("LD_PRELOAD");


  return (ld_preload && strstr (ld_preload, "libfaultinjection-1.0.so"));
}

static gboolean
_execute_corrupt_socket_recv (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  struct sockaddr_in addr =
      { AF_INET, htons (42), {htonl (INADDR_LOOPBACK)}, {0} };
  gint server_port, times;
  const gchar *errno_str;
  gint real_errno;

  if (!_fault_injector_loaded ()) {
    GST_ERROR
        ("The fault injector wasn't preloaded, can't execute socket recv corruption\n"
        "You should set LD_PRELOAD to the path of libfaultinjection.so");
    return FALSE;
  }

  if (!gst_structure_get_int (action->structure, "port", &server_port)) {
    GST_ERROR ("could not get port to corrupt recv on.");
    return FALSE;
  }

  if (!gst_structure_get_int (action->structure, "times", &times)) {
    gst_structure_set (action->structure, "times", G_TYPE_INT, 1, NULL);
  }

  errno_str = gst_structure_get_string (action->structure, "errno");
  if (!errno_str) {
    GST_ERROR ("Could not get errno string");
    return FALSE;
  }

  real_errno = errno_string_to_int (errno_str);

  if (real_errno == 0) {
    GST_ERROR ("unrecognized errno");
    return FALSE;
  }

  gst_structure_set (action->structure, "real_errno", G_TYPE_INT, real_errno,
      NULL);

  addr.sin_port = htons (server_port);

  socket_interposer_set_callback (&addr,
      (socket_interposer_callback) socket_callback_, action);

  return GST_VALIDATE_EXECUTE_ACTION_ASYNC;
}

static gboolean
socket_interposer_init (GstPlugin * plugin)
{
/*  *INDENT-OFF* */
  gst_validate_register_action_type_dynamic (plugin, "corrupt-socket-recv",
      GST_RANK_PRIMARY,
      _execute_corrupt_socket_recv, ((GstValidateActionParameter[]) {
            {
              .name = "port",
              .description = "The port the socket to be corrupted listens on",
              .mandatory = TRUE,
              .types = "int",
              .possible_variables = NULL,
            },
            {
              .name = "errno",
              .description = "errno to set when failing",
              .mandatory = TRUE,
              .types = "string",
            },
            {
              .name = "times",
              .description = "Number of times to corrupt recv, default is one",
              .mandatory = FALSE,
              .types = "int",
              .possible_variables = NULL,
              .def = "1",
            },
            {NULL}
          }),
      "corrupt the next socket receive", GST_VALIDATE_ACTION_TYPE_ASYNC);
/*  *INDENT-ON* */

  return TRUE;
}

#else /* No LD_PRELOAD tricks on Windows */

static gboolean
socket_interposer_init (GstPlugin * plugin)
{
  return TRUE;
}

#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    validatefaultinjection,
    "Fault injector plugin for GstValidate",
    socket_interposer_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
