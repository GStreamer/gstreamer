/*
 * qcamip.h - Connectix QuickCam Image Processing routines
 *
 * Time-stamp: <02 Sep 96 11:19:27 HST edo@eosys.com>
 *
 * Version 0.2
 */

/******************************************************************

Copyright (C) 1996 by Ed Orcutt Systems

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, and/or distribute copies of the
Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

1. The above copyright notice and this permission notice shall
   be included in all copies or substantial portions of the
   Software.

2. Redistribution for profit requires the express, written
   permission of the author.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT.  IN NO EVENT SHALL ED ORCUTT SYSTEMS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

******************************************************************/

#ifndef _QCAMIP_H
#define _QCAMIP_H
#include "qcam.h"

/* Auto exposure modes  */

#define AE_ALL_AVG 0
#define AE_CTR_AVG 1
#define AE_STD_AVG 2

/* Return value of image processing routines */

#define QCIP_XPSR_OK        0
#define QCIP_XPSR_RSCN      1
#define QCIP_XPSR_ERR       2
#define QCIP_XPSR_LUM_INVLD 3

/* Prototypes for image processing routines  */

int qcip_autoexposure (struct qcam *q, scanbuf * scan);
int qcip_set_luminance_target (struct qcam *q, int val);
int qcip_set_luminance_tolerance (struct qcam *q, int val);
int qcip_set_luminance_std_target (struct qcam *q, int val);
int qcip_set_luminance_std_tolerance (struct qcam *q, int val);
int qcip_set_autoexposure_mode (int val);
void qcip_histogram (struct qcam *q, scanbuf * scan, int *histogram);
void qcip_display_histogram (struct qcam *q, scanbuf * scan);

#endif /*! _QCAMIP_H */
