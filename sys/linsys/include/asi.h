/* asi.h
 *
 * Shared header file for the Linux user-space API for
 * Linear Systems Ltd. DVB Master ASI interface boards.
 *
 * Copyright (C) 1999 Tony Bolger <d7v@indigo.ie>
 * Copyright (C) 2000-2009 Linear Systems Ltd.
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

#ifndef _ASI_H
#define _ASI_H

/* Driver info */
#define ASI_DRIVER_NAME "asi"

#define ASI_MAJOR 61	/* Set to 0 for dynamic allocation.
			 * Otherwise, 61 is available.
			 * See /usr/src/linux/Documentation/devices.txt */

#define ASI_TX_BUFFERS_MIN 2 /* This must be at least 2 */
/* The minimum transmit buffer size must be positive, divisible by 8,
 * and large enough that the buffers aren't transferred to the onboard FIFOs
 * too quickly for the machine to handle the interrupts.
 * This is especially a problem at startup, when the FIFOs are empty.
 * Relevant factors include onboard FIFO size, PCI bus throughput,
 * processor speed, and interrupt latency. */
#define ASI_TX_BUFSIZE_MIN 1024
#define ASI_RX_BUFFERS_MIN 2 /* This must be at least 2 */
#define ASI_RX_BUFSIZE_MIN 8 /* This must be positive and divisible by 8 */

#define ASI_TX_BUFFERS 54 /* This must be at least 2 */
#define ASI_TX_BUFSIZE 38352 /* This must be positive and divisible by 8 */
#define ASI_RX_BUFFERS 54 /* This must be at least 2 */
#define ASI_RX_BUFSIZE 38352 /* This must be positive and divisible by 8 */

/* Ioctl () definitions */
#define ASI_IOC_MAGIC '?' /* This ioctl magic number is currently free. See
			   * /usr/src/linux/Documentation/ioctl-number.txt */

#define ASI_IOC_TXGETCAP	_IOR(ASI_IOC_MAGIC, 1, unsigned int)
#define ASI_IOC_TXGETEVENTS	_IOR(ASI_IOC_MAGIC, 2, unsigned int)
#define ASI_IOC_TXGETBUFLEVEL	_IOR(ASI_IOC_MAGIC, 3, unsigned int)
#define ASI_IOC_TXSETSTUFFING	_IOW(ASI_IOC_MAGIC, 4, struct asi_txstuffing)
#define ASI_IOC_TXGETBYTECOUNT	_IOR(ASI_IOC_MAGIC, 5, unsigned int)
/* #define ASI_IOC_TXGETFIFO	_IOR(ASI_IOC_MAGIC, 6, int) */
#define ASI_IOC_TXGETTXD	_IOR(ASI_IOC_MAGIC, 7, int)
#define ASI_IOC_TXGET27COUNT	_IOR(ASI_IOC_MAGIC, 8, unsigned int)
/* Provide compatibility with applications compiled for older API */
#define ASI_IOC_TXSETPID_DEPRECATED	_IOR(ASI_IOC_MAGIC, 9, unsigned int)
#define ASI_IOC_TXSETPID	_IOW(ASI_IOC_MAGIC, 9, unsigned int)
#define ASI_IOC_TXGETPCRSTAMP	_IOR(ASI_IOC_MAGIC, 10, struct asi_pcrstamp)
/* Provide compatibility with applications compiled for older API */
#define ASI_IOC_TXCHANGENEXTIP_DEPRECATED	_IOR(ASI_IOC_MAGIC, 11, int)
#define ASI_IOC_TXCHANGENEXTIP	_IOW(ASI_IOC_MAGIC, 11, int)

#define ASI_IOC_RXGETCAP	_IOR(ASI_IOC_MAGIC, 65, unsigned int)
#define ASI_IOC_RXGETEVENTS	_IOR(ASI_IOC_MAGIC, 66, unsigned int)
#define ASI_IOC_RXGETBUFLEVEL	_IOR(ASI_IOC_MAGIC, 67, unsigned int)
/* #define ASI_IOC_RXSETREFRAME	_IOW(ASI_IOC_MAGIC, 68, int) */
#define ASI_IOC_RXGETSTATUS	_IOR(ASI_IOC_MAGIC, 69, int)
#define ASI_IOC_RXGETBYTECOUNT	_IOR(ASI_IOC_MAGIC, 70, unsigned int)
/* #define ASI_IOC_RXGETFIFO	_IOR(ASI_IOC_MAGIC, 71, int) */
#define ASI_IOC_RXSETINVSYNC	_IOW(ASI_IOC_MAGIC, 72, int)
#define ASI_IOC_RXGETCARRIER	_IOR(ASI_IOC_MAGIC, 73, int)
#define ASI_IOC_RXSETDSYNC	_IOW(ASI_IOC_MAGIC, 74, int)
#define ASI_IOC_RXGETRXD	_IOR(ASI_IOC_MAGIC, 75, int)
#define ASI_IOC_RXSETPF		_IOW(ASI_IOC_MAGIC, 76, unsigned int [256])
/* #define ASI_IOC_RXSETPFE	_IOW(ASI_IOC_MAGIC, 77, int) */
#define ASI_IOC_RXSETPID0	_IOW(ASI_IOC_MAGIC, 78, int)
#define ASI_IOC_RXGETPID0COUNT	_IOR(ASI_IOC_MAGIC, 79, unsigned int)
#define ASI_IOC_RXSETPID1	_IOW(ASI_IOC_MAGIC, 80, int)
#define ASI_IOC_RXGETPID1COUNT	_IOR(ASI_IOC_MAGIC, 81, unsigned int)
#define ASI_IOC_RXSETPID2	_IOW(ASI_IOC_MAGIC, 82, int)
#define ASI_IOC_RXGETPID2COUNT	_IOR(ASI_IOC_MAGIC, 83, unsigned int)
#define ASI_IOC_RXSETPID3	_IOW(ASI_IOC_MAGIC, 84, int)
#define ASI_IOC_RXGETPID3COUNT	_IOR(ASI_IOC_MAGIC, 85, unsigned int)
/* #define ASI_IOC_RXGETSTAMP	_IOR(ASI_IOC_MAGIC, 86, unsigned int) */
#define ASI_IOC_RXGET27COUNT	_IOR(ASI_IOC_MAGIC, 87, unsigned int)
#define ASI_IOC_RXGETSTATUS2	_IOR(ASI_IOC_MAGIC, 88, int)
/* Provide compatibility with applications compiled for older API */
#define ASI_IOC_RXSETINPUT_DEPRECATED	_IOR(ASI_IOC_MAGIC, 89, int)
#define ASI_IOC_RXSETINPUT	_IOW(ASI_IOC_MAGIC, 89, int)
#define ASI_IOC_RXGETRXD2	_IOR(ASI_IOC_MAGIC, 90, int)

#define ASI_IOC_GETID		_IOR(ASI_IOC_MAGIC, 129, unsigned int)
#define ASI_IOC_GETVERSION	_IOR(ASI_IOC_MAGIC, 130, unsigned int)

/* Transmitter event flag bit locations */
#define ASI_EVENT_TX_BUFFER_ORDER	0
#define ASI_EVENT_TX_BUFFER		(1 << ASI_EVENT_TX_BUFFER_ORDER)
#define ASI_EVENT_TX_FIFO_ORDER		1
#define ASI_EVENT_TX_FIFO		(1 << ASI_EVENT_TX_FIFO_ORDER)
#define ASI_EVENT_TX_DATA_ORDER		2
#define ASI_EVENT_TX_DATA		(1 << ASI_EVENT_TX_DATA_ORDER)

/* Receiver event flag bit locations */
#define ASI_EVENT_RX_BUFFER_ORDER	0
#define ASI_EVENT_RX_BUFFER		(1 << ASI_EVENT_RX_BUFFER_ORDER)
#define ASI_EVENT_RX_FIFO_ORDER		1
#define ASI_EVENT_RX_FIFO		(1 << ASI_EVENT_RX_FIFO_ORDER)
#define ASI_EVENT_RX_CARRIER_ORDER	2
#define ASI_EVENT_RX_CARRIER		(1 << ASI_EVENT_RX_CARRIER_ORDER)
#define ASI_EVENT_RX_AOS_ORDER		3
#define ASI_EVENT_RX_AOS		(1 << ASI_EVENT_RX_AOS_ORDER)
#define ASI_EVENT_RX_LOS_ORDER		4
#define ASI_EVENT_RX_LOS		(1 << ASI_EVENT_RX_LOS_ORDER)
#define ASI_EVENT_RX_DATA_ORDER		5
#define ASI_EVENT_RX_DATA		(1 << ASI_EVENT_RX_DATA_ORDER)

/**
 * asi_txstuffing - Transmitter stuffing parameters
 * @ib: interbyte stuffing
 * @ip: interpacket stuffing
 * @normal_ip: FT0
 * @big_ip: FT1
 * @il_normal: IL0
 * @il_big: IL1
 **/
struct asi_txstuffing {
	/* Number of K28.5 characters to insert between packet bytes */
	unsigned int ib;

	/* Base number of K28.5 characters to insert between packets,
	 * not including the two required by ASI */
	unsigned int ip;

	/* Number of packets with (ip) bytes of interpacket stuffing
	 * per finetuning cycle */
	unsigned int normal_ip;

	/* Number of packets with (ip + 1) bytes of interpacket stuffing
	 * per finetuning cycle */
	unsigned int big_ip;

	/* Number of packets with (ip) bytes of interpacket stuffing
	 * per interleaved finetuning cycle */
	unsigned int il_normal;

	/* Number of packets with (ip + 1) bytes of interpacket stuffing
	 * per interleaved finetuning cycle */
	unsigned int il_big;
};

/**
 * asi_pcrstamp - PCR - departure time pair
 * @adaptation_field_length: adaptation field length
 * @adaptation_field_flags: adaptation field flags
 * @PCR: a program clock reference
 * @count: departure time of this PCR, in 1 / 27 MHz
 **/
struct asi_pcrstamp {
	unsigned char adaptation_field_length;
	unsigned char adaptation_field_flags;
	unsigned char PCR[6];
	long long int count;
};

/* Interface capabilities */
#define ASI_CAP_TX_MAKE204	0x00000004
#define ASI_CAP_TX_FINETUNING	0x00000008
#define ASI_CAP_TX_BYTECOUNTER	0x00000010
#define ASI_CAP_TX_SETCLKSRC	0x00000020
#define ASI_CAP_TX_FIFOUNDERRUN	0x00000040
#define ASI_CAP_TX_LARGEIB	0x00000080
#define ASI_CAP_TX_INTERLEAVING	0x00000100
#define ASI_CAP_TX_DATA		0x00000200
#define ASI_CAP_TX_RXCLKSRC	0x00000400
/* #define ASI_CAP_TX_COMPOSITEREF	0x00000800 */
#define ASI_CAP_TX_PCRSTAMP	0x00001000
#define ASI_CAP_TX_CHANGENEXTIP	0x00002000
#define ASI_CAP_TX_27COUNTER	0x00004000
#define ASI_CAP_TX_BYTESOR27	0x00008000
#define ASI_CAP_TX_TIMESTAMPS	0x00010000
#define ASI_CAP_TX_PTIMESTAMPS	0x00020000
#define ASI_CAP_TX_NULLPACKETS	0x00040000

#define ASI_CAP_RX_SYNC		0x00000004
#define ASI_CAP_RX_MAKE188	0x00000008
#define ASI_CAP_RX_BYTECOUNTER	0x00000010
/* #define ASI_CAP_RX_FIFOSTATUS	0x00000020 */
#define ASI_CAP_RX_INVSYNC	0x00000040
#define ASI_CAP_RX_CD		0x00000080
#define ASI_CAP_RX_DSYNC	0x00000100
#define ASI_CAP_RX_DATA		0x00000200
#define ASI_CAP_RX_PIDFILTER	0x00000400
#define ASI_CAP_RX_PIDCOUNTER	0x00000800
#define ASI_CAP_RX_4PIDCOUNTER	0x00001000
#define ASI_CAP_RX_FORCEDMA	0x00002000
#define ASI_CAP_RX_27COUNTER	0x00004000
#define ASI_CAP_RX_BYTESOR27	0x00008000
#define ASI_CAP_RX_TIMESTAMPS	0x00010000
#define ASI_CAP_RX_PTIMESTAMPS	0x00020000
#define ASI_CAP_RX_NULLPACKETS	0x00040000
#define ASI_CAP_RX_REDUNDANT	0x00080000
#define ASI_CAP_RX_DATA2	0x00100000

/* Transmitter clock source settings */
#define ASI_CTL_TX_CLKSRC_ONBOARD	0
#define ASI_CTL_TX_CLKSRC_EXT		1
#define ASI_CTL_TX_CLKSRC_RX		2
/* #define ASI_CTL_TX_CLKSRC_EXT_PAL	3 */

/* Transmitter mode settings */
#define ASI_CTL_TX_MODE_188	0
#define ASI_CTL_TX_MODE_204	1
#define ASI_CTL_TX_MODE_MAKE204	2

/* Receiver mode settings */
#define ASI_CTL_RX_MODE_RAW		0
#define ASI_CTL_RX_MODE_188		1
#define ASI_CTL_RX_MODE_204		2
#define ASI_CTL_RX_MODE_AUTO		3
#define ASI_CTL_RX_MODE_AUTOMAKE188	4
#define ASI_CTL_RX_MODE_204MAKE188	5

/* Timestamping settings */
#define ASI_CTL_TSTAMP_NONE	0
#define ASI_CTL_TSTAMP_APPEND	1
#define ASI_CTL_TSTAMP_PREPEND	2

/* Transport settings */
#define ASI_CTL_TRANSPORT_DVB_ASI	0
#define ASI_CTL_TRANSPORT_SMPTE_310M	1

#endif

