/**
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef TOMSMOCOMP_H_INCLUDED
#define TOMSMOCOMP_H_INCLUDED

#include "gstdeinterlace2.h"

#ifdef __cplusplus
extern "C" {
#endif

int  Search_Effort_0();
int  Search_Effort_1();
int  Search_Effort_3();
int  Search_Effort_5();
int  Search_Effort_9();
int  Search_Effort_11();
int  Search_Effort_13();
int  Search_Effort_15();
int  Search_Effort_19();
int  Search_Effort_21();
int  Search_Effort_Max();

int  Search_Effort_0_SB();
int  Search_Effort_1_SB();
int  Search_Effort_3_SB();
int  Search_Effort_5_SB();
int  Search_Effort_9_SB();
int  Search_Effort_11_SB();
int  Search_Effort_13_SB();
int  Search_Effort_15_SB();
int  Search_Effort_19_SB();
int  Search_Effort_21_SB();
int  Search_Effort_Max_SB();

#ifdef __cplusplus
};
#endif

#endif /* TOMSMOCOMP_H_INCLUDED */
