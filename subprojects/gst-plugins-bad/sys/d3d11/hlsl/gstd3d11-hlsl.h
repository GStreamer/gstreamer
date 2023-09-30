/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#ifdef HLSL_PRECOMPILED
#include "ps-checker-luma.h"
#include "ps-checker-rgb.h"
#include "ps-checker-vuya.h"
#include "ps-checker.h"
#include "ps-color.h"
#include "ps-sample-premul.h"
#include "ps-sample.h"
#include "ps-snow.h"
#include "vs-color.h"
#include "vs-coord.h"
#include "vs-pos.h"
#else
const BYTE g_PSMain_checker_luma[] = { 0 };
const BYTE g_PSMain_checker_rgb[] = { 0 };
const BYTE g_PSMain_checker_vuya[] = { 0 };
const BYTE g_PSMain_checker[] = { 0 };
const BYTE g_PSMain_color[] = { 0 };
const BYTE g_PSMain_sample_premul[] = { 0 };
const BYTE g_PSMain_sample[] = { 0 };
const BYTE g_PSMain_snow[] = { 0 };
const BYTE g_VSMain_color[] = { 0 };
const BYTE g_VSMain_coord[] = { 0 };
const BYTE g_VSMain_pos[] = { 0 };
#endif

#include "ps-checker-luma.hlsl"
#include "ps-checker-rgb.hlsl"
#include "ps-checker-vuya.hlsl"
#include "ps-checker.hlsl"
#include "ps-color.hlsl"
#include "ps-sample-premul.hlsl"
#include "ps-sample.hlsl"
#include "ps-snow.hlsl"
#include "vs-color.hlsl"
#include "vs-coord.hlsl"
#include "vs-pos.hlsl"
