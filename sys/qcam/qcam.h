/* qcam.h -- routines for accessing the Connectix QuickCam */

/* Version 0.1, January 2, 1996 */
/* Version 0.5, August 24, 1996 */
/* Version 0.7, August 26, 1996 */


/******************************************************************

Copyright (C) 1996 by Scott Laird

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL SCOTT LAIRD BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

******************************************************************/

#ifndef _QCAM_H
#define _QCAM_H

#define QC_VERSION "0.91"

/* One from column A... */
#define QC_NOTSET 0
#define QC_UNIDIR 1
#define QC_BIDIR  2
#define QC_SERIAL 3

/* ... and one from column B */
#define QC_ANY          0x00
#define QC_FORCE_UNIDIR 0x10
#define QC_FORCE_BIDIR  0x20
#define QC_FORCE_SERIAL 0x30
/* in the port_mode member */

#define QC_MODE_MASK    0x07
#define QC_FORCE_MASK   0x70

#define MAX_HEIGHT 243
#define MAX_WIDTH 336

struct qcam {
  int width, height;
  int bpp;
  int mode;
  int contrast, brightness, whitebal;
  int port;
  int port_mode;
  int transfer_scale;
  int top, left;
  int fd; /* lock file descriptor
           * It was, unfortunately, necessary to add this member to the
           * struct qcam to conveniently implement POSIX fcntl-style locking.
           * We need a seperate lock file for each struct qcam, for instance,
           * if the same process (using qcam-lib) is accessing multiple
           * QuickCams on (of course) multiple ports.
           * - Dave Plonka (plonka@carroll1.cc.edu)
           */
};

typedef unsigned char scanbuf;

/* General QuickCam handling routines */

int qc_getbrightness(const struct qcam *q);
int qc_setbrightness(struct qcam *q, int val);
int qc_getcontrast(const struct qcam *q);
int qc_setcontrast(struct qcam *q, int val);
int qc_getwhitebal(const struct qcam *q);
int qc_setwhitebal(struct qcam *q, int val);
void qc_getresolution(const struct qcam *q, int *x, int *y);
int qc_setresolution(struct qcam *q, int x, int y);
int qc_getbitdepth(const struct qcam *q);
int qc_setbitdepth(struct qcam *q, int val);
int qc_getheight(const struct qcam *q);
int qc_setheight(struct qcam *q, int y);
int qc_getwidth(const struct qcam *q);
int qc_setwidth(struct qcam *q, int x);
int qc_gettop(const struct qcam *q);
int qc_settop(struct qcam *q, int val);
int qc_getleft(const struct qcam *q);
int qc_setleft(struct qcam *q, int val);
int qc_gettransfer_scale(const struct qcam *q);
int qc_settransfer_scale(struct qcam *q, int val);
int qc_calibrate(struct qcam *q);
int qc_forceunidir(struct qcam *q);
void qc_dump(const struct qcam *q, char *file);

struct qcam *qc_init(void);
int qc_initfile(struct qcam *q, char *fname);
int qc_open(struct qcam *q);
int qc_close(struct qcam *q);
int qc_detect(const struct qcam *q);
void qc_reset(struct qcam *q);
void qc_set(struct qcam *q);
scanbuf *qc_scan(const struct qcam *q);
scanbuf *qc_convertscan(struct qcam *q, scanbuf *scan);
void qc_writepgm(const struct qcam *q, FILE *f, scanbuf *scan);
void qc_wait(int val);

/* OS/hardware specific routines */

int read_lpstatus(const struct qcam *q);
int read_lpcontrol(const struct qcam *q);
int read_lpdata(const struct qcam *q);
void write_lpdata(const struct qcam *q, int d);
void write_lpcontrol(const struct qcam *q, int d);
int enable_ports(const struct qcam *q);
int disable_ports(const struct qcam *q);
int qc_unlock(struct qcam *q);
int qc_lock(struct qcam *q);
void qc_wait(int val);
int qc_probe(struct qcam *q);

/* Image processing routines */
int fixdark(const struct qcam *q, scanbuf *scan);
int qc_edge_detect(const struct qcam *q, scanbuf *scan, int tolerance);

#endif /*! _QCAM_H*/
