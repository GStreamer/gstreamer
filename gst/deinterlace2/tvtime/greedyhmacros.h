/////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2001 Tom Barry.  All rights reserved.
/////////////////////////////////////////////////////////////////////////////
//
//	This file is subject to the terms of the GNU General Public License as
//	published by the Free Software Foundation.  A copy of this license is
//	included with this software distribution in the file COPYING.  If you
//	do not have a copy, you may obtain a copy by writing to the Free
//	Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//
//	This software is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details
//
/////////////////////////////////////////////////////////////////////////////

// Define a few macros for CPU dependent instructions. 
// I suspect I don't really understand how the C macro preprocessor works but
// this seems to get the job done.          // TRB 7/01

// BEFORE USING THESE YOU MUST SET:

// #define SIMD_TYPE MMXEXT            (or MMX or TDNOW)

// some macros for pavgb instruction
//      V_PAVGB(mmr1, mmr2, mmr work register, smask) mmr2 may = mmrw if you can trash it

#define V_PAVGB_MMX(mmr1, mmr2, mmrw, smask) \
	"movq    "mmr2",  "mmrw"\n\t"            \
	"pand    "smask", "mmrw"\n\t"            \
	"psrlw   $1,      "mmrw"\n\t"            \
	"pand    "smask", "mmr1"\n\t"            \
	"psrlw   $1,      "mmr1"\n\t"            \
	"paddusb "mmrw",  "mmr1"\n\t"
#define V_PAVGB_MMXEXT(mmr1, mmr2, mmrw, smask)      "pavgb   "mmr2", "mmr1"\n\t"
#define V_PAVGB_TDNOW(mmr1, mmr2, mmrw, smask)    "pavgusb "mmr2", "mmr1"\n\t"
#define V_PAVGB(mmr1, mmr2, mmrw, smask)          V_PAVGB2(mmr1, mmr2, mmrw, smask, SIMD_TYPE) 
#define V_PAVGB2(mmr1, mmr2, mmrw, smask, simdtype) V_PAVGB3(mmr1, mmr2, mmrw, smask, simdtype) 
#define V_PAVGB3(mmr1, mmr2, mmrw, smask, simdtype) V_PAVGB_##simdtype(mmr1, mmr2, mmrw, smask) 

// some macros for pmaxub instruction
#define V_PMAXUB_MMX(mmr1, mmr2) \
    "psubusb "mmr2", "mmr1"\n\t" \
    "paddusb "mmr2", "mmr1"\n\t"
#define V_PMAXUB_MMXEXT(mmr1, mmr2)      "pmaxub "mmr2", "mmr1"\n\t"
#define V_PMAXUB_TDNOW(mmr1, mmr2)    V_PMAXUB_MMX(mmr1, mmr2)  // use MMX version
#define V_PMAXUB(mmr1, mmr2)          V_PMAXUB2(mmr1, mmr2, SIMD_TYPE) 
#define V_PMAXUB2(mmr1, mmr2, simdtype) V_PMAXUB3(mmr1, mmr2, simdtype) 
#define V_PMAXUB3(mmr1, mmr2, simdtype) V_PMAXUB_##simdtype(mmr1, mmr2) 

// some macros for pminub instruction
//      V_PMINUB(mmr1, mmr2, mmr work register)     mmr2 may NOT = mmrw
#define V_PMINUB_MMX(mmr1, mmr2, mmrw) \
    "pcmpeqb "mmrw", "mmrw"\n\t"       \
    "psubusb "mmr2", "mmrw"\n\t"       \
    "paddusb "mmrw", "mmr1"\n\t"       \
    "psubusb "mmrw", "mmr1"\n\t"
#define V_PMINUB_MMXEXT(mmr1, mmr2, mmrw)      "pminub "mmr2", "mmr1"\n\t"
#define V_PMINUB_TDNOW(mmr1, mmr2, mmrw)    V_PMINUB_MMX(mmr1, mmr2, mmrw)  // use MMX version
#define V_PMINUB(mmr1, mmr2, mmrw)          V_PMINUB2(mmr1, mmr2, mmrw, SIMD_TYPE) 
#define V_PMINUB2(mmr1, mmr2, mmrw, simdtype) V_PMINUB3(mmr1, mmr2, mmrw, simdtype) 
#define V_PMINUB3(mmr1, mmr2, mmrw, simdtype) V_PMINUB_##simdtype(mmr1, mmr2, mmrw) 

// some macros for movntq instruction
//      V_MOVNTQ(mmr1, mmr2) 
#define V_MOVNTQ_MMX(mmr1, mmr2)      "movq   "mmr2", "mmr1"\n\t"
#define V_MOVNTQ_TDNOW(mmr1, mmr2)    "movq   "mmr2", "mmr1"\n\t"
#define V_MOVNTQ_MMXEXT(mmr1, mmr2)      "movntq "mmr2", "mmr1"\n\t"
#define V_MOVNTQ(mmr1, mmr2)          V_MOVNTQ2(mmr1, mmr2, SIMD_TYPE) 
#define V_MOVNTQ2(mmr1, mmr2, simdtype) V_MOVNTQ3(mmr1, mmr2, simdtype) 
#define V_MOVNTQ3(mmr1, mmr2, simdtype) V_MOVNTQ_##simdtype(mmr1, mmr2)

// end of macros
