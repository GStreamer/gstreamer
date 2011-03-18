/* master.h
 *
 * Global definitions for Linear Systems Ltd.
 * digital television-related boards.
 *
 * Copyright (C) 2004-2009 Linear Systems Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of Linear Systems Ltd. nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY LINEAR SYSTEMS LTD. "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL LINEAR SYSTEMS LTD. OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Linear Systems can be contacted at <http://www.linsys.ca/>.
 *
 */

#ifndef _MASTER_H
#define _MASTER_H

#define MASTER_DRIVER_VERSION "2.7.0"
#define MASTER_DRIVER_VERSION_CODE 0x020700
#define MASTER_DRIVER_DATE "2010-01-11"

#define MASTER_PCI_VENDOR_ID_LINSYS 0x1254

/* Device capabilities */
#define MASTER_CAP_BYPASS	0x00000001
#define MASTER_CAP_WATCHDOG	0x00000002
#define MASTER_CAP_GPI		0x00000004
#define MASTER_CAP_GPO		0x00000008
#define MASTER_CAP_UID		0x00000010
#define MASTER_CAP_BLACKBURST	0x00000020

/* Bypass mode settings */
#define MASTER_CTL_BYPASS_ENABLE	0
#define MASTER_CTL_BYPASS_DISABLE	1
#define MASTER_CTL_BYPASS_WATCHDOG	2

/* Black burst type settings */
#define MASTER_CTL_BLACKBURST_NTSC	0
#define MASTER_CTL_BLACKBURST_PAL	1

/* Maximum watchdog timeout in milliseconds.
 * Limited to 32 bits at 40 MHz or 27 MHz */
#define MASTER_WATCHDOG_MAX	100000

#endif

