/*
 *  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

/*
 * vncauth.c - Functions for VNC password management and authentication.
 */

#include "config.h"
#include <glib.h>
#include "_stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "time.h"
#include "vncauth.h"
#include "d3des.h"

#include "sys/stat.h"


/*
 * We use a fixed key to store passwords, since we assume that our local
 * file system is secure but nonetheless don't want to store passwords
 * as plaintext.
 */

unsigned char fixedkey[8] = { 23, 82, 107, 6, 35, 78, 88, 7 };


/*
 * Encrypt a password and store it in a file.  Returns 0 if successful,
 * 1 if the file could not be written.
 */

int32_t
vncEncryptAndStorePasswd (char *passwd, char *fname)
{
  FILE *fp;
  uint32_t i;
  unsigned char encryptedPasswd[8];

  if ((fp = fopen (fname, "w")) == NULL)
    return 1;

  chmod (fname, S_IRUSR | S_IWUSR);

  /* pad password with nulls */

  for (i = 0; i < 8; i++) {
    if (i < strlen (passwd)) {
      encryptedPasswd[i] = passwd[i];
    } else {
      encryptedPasswd[i] = 0;
    }
  }

  /* Do encryption in-place - this way we overwrite our copy of the plaintext
     password */

  deskey (fixedkey, EN0);
  des (encryptedPasswd, encryptedPasswd);

  for (i = 0; i < 8; i++) {
    putc (encryptedPasswd[i], fp);
  }

  fclose (fp);
  return 0;
}


/*
 * Decrypt a password from a file.  Returns a pointer to a newly allocated
 * string containing the password or a null pointer if the password could
 * not be retrieved for some reason.
 */

char *
vncDecryptPasswdFromFile (char *fname)
{
  FILE *fp;
  int32_t i, ch;
  unsigned char *passwd;

  if ((fp = fopen (fname, "r")) == NULL)
    return NULL;

  passwd = (unsigned char *) malloc (9);

  for (i = 0; i < 8; i++) {
    ch = getc (fp);
    if (ch == EOF) {
      fclose (fp);
      free (passwd);
      return NULL;
    }
    passwd[i] = ch;
  }

  fclose (fp);

  deskey (fixedkey, DE1);
  des (passwd, passwd);

  passwd[8] = 0;

  return (char *) passwd;
}


/*
 * Generate CHALLENGESIZE random bytes for use in challenge-response
 * authentication.
 */

void
vncRandomBytes (unsigned char *bytes)
{
  int32_t i;
  uint32_t seed = (uint32_t) time (0);
#ifndef G_OS_WIN32
  srandom (seed);
#else
  srand (seed);
#endif
  for (i = 0; i < CHALLENGESIZE; i++) {
#ifndef G_OS_WIN32
    bytes[i] = (unsigned char) (random () & 255);
#else
    bytes[i] = (unsigned char) (rand () & 255);
#endif
  }
}


/*
 * Encrypt CHALLENGESIZE bytes in memory using a password.
 */

void
vncEncryptBytes (unsigned char *bytes, char *passwd)
{
  unsigned char key[8];
  uint32_t i;

  /* key is simply password padded with nulls */

  for (i = 0; i < 8; i++) {
    if (i < strlen (passwd)) {
      key[i] = passwd[i];
    } else {
      key[i] = 0;
    }
  }

  deskey (key, EN0);

  for (i = 0; i < CHALLENGESIZE; i += 8) {
    des (bytes + i, bytes + i);
  }
}
