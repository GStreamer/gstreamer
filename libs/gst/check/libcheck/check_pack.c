/*
 * Check: a unit test framework for C
 * Copyright (C) 2001, 2002 Arien Malec
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "libcompat/libcompat.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "internal-check.h"
#include "check_error.h"
#include "check_list.h"
#include "check_impl.h"
#include "check_pack.h"

#ifndef HAVE_PTHREAD
#define pthread_mutex_lock(arg)
#define pthread_mutex_unlock(arg)
#define pthread_cleanup_push(f,a) {
#define pthread_cleanup_pop(e) }
#endif

/* Maximum size for one message in the message stream. */
#define CK_MAX_MSG_SIZE 8192
/* This is used to implement a sliding window on the receiving
 * side. When sending messages, we assure that no single message
 * is bigger than this (actually we check against CK_MAX_MSG_SIZE/2).
 * The usual size for a message is less than 80 bytes.
 * All this is done instead of the previous approach to allocate (actually
 * continuously reallocate) one big chunk for the whole message stream.
 * Problems were seen in the wild with up to 4 GB reallocations.
 */


/* typedef an unsigned int that has at least 4 bytes */
typedef uint32_t ck_uint32;


static void pack_int (char **buf, int val);
static int upack_int (char **buf);
static void pack_str (char **buf, const char *str);
static char *upack_str (char **buf);

static int pack_ctx (char **buf, CtxMsg * cmsg);
static int pack_loc (char **buf, LocMsg * lmsg);
static int pack_fail (char **buf, FailMsg * fmsg);
static int pack_duration (char **buf, DurationMsg * fmsg);
static void upack_ctx (char **buf, CtxMsg * cmsg);
static void upack_loc (char **buf, LocMsg * lmsg);
static void upack_fail (char **buf, FailMsg * fmsg);
static void upack_duration (char **buf, DurationMsg * fmsg);

static void check_type (int type, const char *file, int line);
static enum ck_msg_type upack_type (char **buf);
static void pack_type (char **buf, enum ck_msg_type type);

static int read_buf (FILE * fdes, int size, char *buf);
static int get_result (char *buf, RcvMsg * rmsg);
static void rcvmsg_update_ctx (RcvMsg * rmsg, enum ck_result_ctx ctx);
static void rcvmsg_update_loc (RcvMsg * rmsg, const char *file, int line);
static RcvMsg *rcvmsg_create (void);
void rcvmsg_free (RcvMsg * rmsg);

typedef int (*pfun) (char **, CheckMsg *);
typedef void (*upfun) (char **, CheckMsg *);

static pfun pftab[] = {
  (pfun) pack_ctx,
  (pfun) pack_fail,
  (pfun) pack_loc,
  (pfun) pack_duration
};

static upfun upftab[] = {
  (upfun) upack_ctx,
  (upfun) upack_fail,
  (upfun) upack_loc,
  (upfun) upack_duration
};

int
pack (enum ck_msg_type type, char **buf, CheckMsg * msg)
{
  if (buf == NULL)
    return -1;
  if (msg == NULL)
    return 0;

  check_type (type, __FILE__, __LINE__);

  return pftab[type] (buf, msg);
}

int
upack (char *buf, CheckMsg * msg, enum ck_msg_type *type)
{
  char *obuf;

  if (buf == NULL)
    return -1;

  obuf = buf;

  *type = upack_type (&buf);

  check_type (*type, __FILE__, __LINE__);

  upftab[*type] (&buf, msg);

  return buf - obuf;
}

static void
pack_int (char **buf, int val)
{
  unsigned char *ubuf = (unsigned char *) *buf;
  ck_uint32 uval = val;

  ubuf[0] = (unsigned char) ((uval >> 24) & 0xFF);
  ubuf[1] = (unsigned char) ((uval >> 16) & 0xFF);
  ubuf[2] = (unsigned char) ((uval >> 8) & 0xFF);
  ubuf[3] = (unsigned char) (uval & 0xFF);

  *buf += 4;
}

static int
upack_int (char **buf)
{
  unsigned char *ubuf = (unsigned char *) *buf;
  ck_uint32 uval;

  uval =
      (ck_uint32) ((ubuf[0] << 24) | (ubuf[1] << 16) | (ubuf[2] << 8) |
      ubuf[3]);

  *buf += 4;

  return (int) uval;
}

static void
pack_str (char **buf, const char *val)
{
  int strsz;

  if (val == NULL)
    strsz = 0;
  else
    strsz = strlen (val);

  pack_int (buf, strsz);

  if (strsz > 0) {
    memcpy (*buf, val, strsz);
    *buf += strsz;
  }
}

static char *
upack_str (char **buf)
{
  char *val;
  int strsz;

  strsz = upack_int (buf);

  if (strsz > 0) {
    val = (char *) emalloc (strsz + 1);
    memcpy (val, *buf, strsz);
    val[strsz] = 0;
    *buf += strsz;
  } else {
    val = (char *) emalloc (1);
    *val = 0;
  }

  return val;
}

static void
pack_type (char **buf, enum ck_msg_type type)
{
  pack_int (buf, (int) type);
}

static enum ck_msg_type
upack_type (char **buf)
{
  return (enum ck_msg_type) upack_int (buf);
}


static int
pack_ctx (char **buf, CtxMsg * cmsg)
{
  char *ptr;
  int len;

  len = 4 + 4;
  *buf = ptr = (char *) emalloc (len);

  pack_type (&ptr, CK_MSG_CTX);
  pack_int (&ptr, (int) cmsg->ctx);

  return len;
}

static void
upack_ctx (char **buf, CtxMsg * cmsg)
{
  cmsg->ctx = (enum ck_result_ctx) upack_int (buf);
}

static int
pack_duration (char **buf, DurationMsg * cmsg)
{
  char *ptr;
  int len;

  len = 4 + 4;
  *buf = ptr = (char *) emalloc (len);

  pack_type (&ptr, CK_MSG_DURATION);
  pack_int (&ptr, cmsg->duration);

  return len;
}

static void
upack_duration (char **buf, DurationMsg * cmsg)
{
  cmsg->duration = upack_int (buf);
}

static int
pack_loc (char **buf, LocMsg * lmsg)
{
  char *ptr;
  int len;

  len = 4 + 4 + (lmsg->file ? strlen (lmsg->file) : 0) + 4;
  *buf = ptr = (char *) emalloc (len);

  pack_type (&ptr, CK_MSG_LOC);
  pack_str (&ptr, lmsg->file);
  pack_int (&ptr, lmsg->line);

  return len;
}

static void
upack_loc (char **buf, LocMsg * lmsg)
{
  lmsg->file = upack_str (buf);
  lmsg->line = upack_int (buf);
}

static int
pack_fail (char **buf, FailMsg * fmsg)
{
  char *ptr;
  int len;

  len = 4 + 4 + (fmsg->msg ? strlen (fmsg->msg) : 0);
  *buf = ptr = (char *) emalloc (len);

  pack_type (&ptr, CK_MSG_FAIL);
  pack_str (&ptr, fmsg->msg);

  return len;
}

static void
upack_fail (char **buf, FailMsg * fmsg)
{
  fmsg->msg = upack_str (buf);
}

static void
check_type (int type, const char *file, int line)
{
  if (type < 0 || type >= CK_MSG_LAST)
    eprintf ("Bad message type arg %d", file, line, type);
}

#ifdef HAVE_PTHREAD
static pthread_mutex_t ck_mutex_lock = PTHREAD_MUTEX_INITIALIZER;
static void
ppack_cleanup (void *mutex)
{
  pthread_mutex_unlock ((pthread_mutex_t *) mutex);
}
#endif

void
ppack (FILE * fdes, enum ck_msg_type type, CheckMsg * msg)
{
  char *buf = NULL;
  int n;
  ssize_t r;

  n = pack (type, &buf, msg);
  /* Keep it on the safe side to not send too much data. */
  if (n > (CK_MAX_MSG_SIZE / 2))
    eprintf ("Message string too long", __FILE__, __LINE__ - 2);

  pthread_cleanup_push (ppack_cleanup, &ck_mutex_lock);
  pthread_mutex_lock (&ck_mutex_lock);
  r = fwrite (buf, 1, n, fdes);
  fflush (fdes);
  pthread_mutex_unlock (&ck_mutex_lock);
  pthread_cleanup_pop (0);
  if (r != n)
    eprintf ("Error in call to fwrite:", __FILE__, __LINE__ - 2);

  free (buf);
}

static int
read_buf (FILE * fdes, int size, char *buf)
{
  int n;

  n = fread (buf, 1, size, fdes);

  if (ferror (fdes)) {
    eprintf ("Error in call to fread:", __FILE__, __LINE__ - 4);
  }

  return n;
}

static int
get_result (char *buf, RcvMsg * rmsg)
{
  enum ck_msg_type type;
  CheckMsg msg;
  int n;

  n = upack (buf, &msg, &type);
  if (n == -1)
    eprintf ("Error in call to upack", __FILE__, __LINE__ - 2);

  if (type == CK_MSG_CTX) {
    CtxMsg *cmsg = (CtxMsg *) & msg;

    rcvmsg_update_ctx (rmsg, cmsg->ctx);
  } else if (type == CK_MSG_LOC) {
    LocMsg *lmsg = (LocMsg *) & msg;

    if (rmsg->failctx == CK_CTX_INVALID) {
      rcvmsg_update_loc (rmsg, lmsg->file, lmsg->line);
    }
    free (lmsg->file);
  } else if (type == CK_MSG_FAIL) {
    FailMsg *fmsg = (FailMsg *) & msg;

    if (rmsg->msg == NULL) {
      rmsg->msg = strdup (fmsg->msg);
      rmsg->failctx = rmsg->lastctx;
    } else {
      /* Skip subsequent failure messages, only happens for CK_NOFORK */
    }
    free (fmsg->msg);
  } else if (type == CK_MSG_DURATION) {
    DurationMsg *cmsg = (DurationMsg *) & msg;

    rmsg->duration = cmsg->duration;
  } else
    check_type (type, __FILE__, __LINE__);

  return n;
}

static void
reset_rcv_test (RcvMsg * rmsg)
{
  rmsg->test_line = -1;
  rmsg->test_file = NULL;
}

static void
reset_rcv_fixture (RcvMsg * rmsg)
{
  rmsg->fixture_line = -1;
  rmsg->fixture_file = NULL;
}

static RcvMsg *
rcvmsg_create (void)
{
  RcvMsg *rmsg;

  rmsg = (RcvMsg *) emalloc (sizeof (RcvMsg));
  rmsg->lastctx = CK_CTX_INVALID;
  rmsg->failctx = CK_CTX_INVALID;
  rmsg->msg = NULL;
  rmsg->duration = -1;
  reset_rcv_test (rmsg);
  reset_rcv_fixture (rmsg);
  return rmsg;
}

void
rcvmsg_free (RcvMsg * rmsg)
{
  free (rmsg->fixture_file);
  free (rmsg->test_file);
  free (rmsg->msg);
  free (rmsg);
}

static void
rcvmsg_update_ctx (RcvMsg * rmsg, enum ck_result_ctx ctx)
{
  if (rmsg->lastctx != CK_CTX_INVALID) {
    free (rmsg->fixture_file);
    reset_rcv_fixture (rmsg);
  }
  rmsg->lastctx = ctx;
}

static void
rcvmsg_update_loc (RcvMsg * rmsg, const char *file, int line)
{
  if (rmsg->lastctx == CK_CTX_TEST) {
    free (rmsg->test_file);
    rmsg->test_line = line;
    rmsg->test_file = strdup (file);
  } else {
    free (rmsg->fixture_file);
    rmsg->fixture_line = line;
    rmsg->fixture_file = strdup (file);
  }
}

RcvMsg *
punpack (FILE * fdes)
{
  int nread, nparse, n;
  char *buf;
  RcvMsg *rmsg;

  rmsg = rcvmsg_create ();

  /* Allcate a buffer */
  buf = (char *) emalloc (CK_MAX_MSG_SIZE);
  /* Fill the buffer from the file */
  nread = read_buf (fdes, CK_MAX_MSG_SIZE, buf);
  nparse = nread;
  /* While not all parsed */
  while (nparse > 0) {
    /* Parse one message */
    n = get_result (buf, rmsg);
    nparse -= n;
    if (nparse < 0)
      eprintf ("Error in call to get_result", __FILE__, __LINE__ - 3);
    /* Move remaining data in buffer to the beginning */
    memmove (buf, buf + n, nparse);
    /* If EOF has not been seen */
    if (nread > 0) {
      /* Read more data into empty space at end of the buffer */
      nread = read_buf (fdes, n, buf + nparse);
      nparse += nread;
    }
  }
  free (buf);

  if (rmsg->lastctx == CK_CTX_INVALID) {
    free (rmsg);
    rmsg = NULL;
  }

  return rmsg;
}
