/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
 *
 * Portions of this file extracted from FFmpeg
 * Copyright (C) 2018 Philip Langdale <philipl@overt.org>
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

#ifdef BUILDING_HLSL
cbuffer YADIFData : register(b0)
{
  int g_width;
  int g_height;
  uint g_primary_line;
  uint g_is_second;
};

Texture2D<float> prevTex : register(t0);
Texture2D<float> curTex : register(t1);
Texture2D<float> nextTex : register(t2);
RWTexture2D<unorm float> outTex : register(u0);

float max3 (float a, float b, float c)
{
  return max (max (a, b), c);
}

float min3 (float a, float b, float c)
{
  return min (min (a, b), c);
}

float
SpatialPred (float a, float b, float c, float d, float e, float f, float g,
    float h, float i, float j, float k, float l, float m, float n)
{
  float spatial_pred = (d + k) / 2;
  float spatial_score = abs (c - j) + abs (d - k) + abs (e - l);
  float score = abs (b - k) + abs (c - l) + abs (d - m);
  if (score < spatial_score) {
    spatial_pred = (c + l) / 2;
    spatial_score = score;
    score = abs (a - l) + abs (b - m) + abs (c - n);
    if (score < spatial_score) {
      spatial_pred = (b + m) / 2;
      spatial_score = score;
    }
  }
  score = abs (d - i) + abs (e - j) + abs (f - k);
  if (score < spatial_score) {
    spatial_pred = (e + j) / 2;
    spatial_score = score;
    score = abs (e - h) + abs (f - i) + abs (g - j);
    if (score < spatial_score) {
      spatial_pred = (f + i) / 2;
      spatial_score = score;
    }
  }
  return spatial_pred;
}

float
TemporalPred (float A, float B, float C, float D, float E, float F,
    float G, float H, float I, float J, float K, float L, float spatial_pred)
{
  float p0 = (C + H) / 2;
  float p1 = F;
  float p2 = (D + I) / 2;
  float p3 = G;
  float p4 = (E + J) / 2;
  float tdiff0 = abs (D - I);
  float tdiff1 = (abs (A - F) + abs (B - G)) / 2;
  float tdiff2 = (abs (K - F) + abs (G - L)) / 2;
  float diff = max3 (tdiff0, tdiff1, tdiff2);
  float maxi = max3 (p2 - p3, p2 - p1, min (p0 - p1, p4 - p3));
  float mini = min3 (p2 - p3, p2 - p1, max (p0 - p1, p4 - p3));
  diff = max3 (diff, mini, -maxi);
  if (spatial_pred > p2 + diff)
    spatial_pred = p2 + diff;
  if (spatial_pred < p2 - diff)
    spatial_pred = p2 - diff;
  return spatial_pred;
}

int GetPosX (int x)
{
  return clamp (x, 0, g_width - 1);
}

int GetPosY (int y)
{
  return clamp (y, 0, g_height - 1);
}

void Execute (int2 pos)
{
  if (pos.x < g_width && pos.y < g_height) {
    [branch] if ((uint(pos.y) % 2) == g_primary_line) {
      outTex[uint2(pos.x, pos.y)] = curTex.Load(int3(pos, 0));
    } else {
      float a = curTex.Load(int3(GetPosX(pos.x - 3), GetPosY(pos.y - 1), 0));
      float b = curTex.Load(int3(GetPosX(pos.x - 2), GetPosY(pos.y - 1), 0));
      float c = curTex.Load(int3(GetPosX(pos.x - 1), GetPosY(pos.y - 1), 0));
      float d = curTex.Load(int3(        pos.x,              pos.y - 1,  0));
      float e = curTex.Load(int3(GetPosX(pos.x + 1), GetPosY(pos.y - 1), 0));
      float f = curTex.Load(int3(GetPosX(pos.x + 2), GetPosY(pos.y - 1), 0));
      float g = curTex.Load(int3(GetPosX(pos.x + 3), GetPosY(pos.y - 1), 0));
      float h = curTex.Load(int3(GetPosX(pos.x - 3), GetPosY(pos.y + 1), 0));
      float i = curTex.Load(int3(GetPosX(pos.x - 2), GetPosY(pos.y + 1), 0));
      float j = curTex.Load(int3(GetPosX(pos.x - 1), GetPosY(pos.y + 1), 0));
      float k = curTex.Load(int3(        pos.x,      GetPosY(pos.y + 1), 0));
      float l = curTex.Load(int3(GetPosX(pos.x + 1), GetPosY(pos.y + 1), 0));
      float m = curTex.Load(int3(GetPosX(pos.x + 2), GetPosY(pos.y + 1), 0));
      float n = curTex.Load(int3(GetPosX(pos.x + 3), GetPosY(pos.y + 1), 0));
      float spatial_pred =
          SpatialPred (a, b, c, d, e, f, g, h, i, j, k, l, m, n);
      float A = prevTex.Load(int3(pos.x, GetPosY(pos.y - 1), 0));
      float B = prevTex.Load(int3(pos.x, GetPosY(pos.y + 1), 0));
      float C, D, E;
      if (g_is_second) {
        C = curTex.Load(int3(pos.x, GetPosY(pos.y - 2), 0));
        D = curTex.Load(int3(pos.x,         pos.y,      0));
        E = curTex.Load(int3(pos.x, GetPosY(pos.y + 2), 0));
      } else {
        C = prevTex.Load(int3(pos.x, GetPosY(pos.y - 2), 0));
        D = prevTex.Load(int3(pos.x,         pos.y,      0));
        E = prevTex.Load(int3(pos.x, GetPosY(pos.y + 2), 0));
      }
      float F =   curTex.Load(int3(pos.x, GetPosY(pos.y - 1), 0));
      float G =   curTex.Load(int3(pos.x, GetPosY(pos.y + 1), 0));
      float H, I, J;
      if (g_is_second) {
        H = nextTex.Load(int3(pos.x, GetPosY(pos.y - 2), 0));
        I = nextTex.Load(int3(pos.x,         pos.y,      0));
        J = nextTex.Load(int3(pos.x, GetPosY(pos.y + 2), 0));
      } else {
        H = curTex.Load(int3(pos.x, GetPosY(pos.y - 2), 0));
        I = curTex.Load(int3(pos.x,         pos.y,      0));
        J = curTex.Load(int3(pos.x, GetPosY(pos.y + 2), 0));
      }
      float K = nextTex.Load(int3(pos.x, GetPosY(pos.y - 1), 0));
      float L = nextTex.Load(int3(pos.x, GetPosY(pos.y + 1), 0));
      spatial_pred =
          TemporalPred(A, B, C, D, E, F, G, H, I, J, K, L, spatial_pred);
      outTex[uint2(pos.x, pos.y)] = clamp (spatial_pred, 0, 4095.0 / 65535.0);
    }
  }
}

[numthreads(8, 8, 1)]
void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)
{
  Execute (int2(tid.x, tid.y));
}
#else
static const char str_CSMain_yadif_1_12[] =
"cbuffer YADIFData : register(b0)\n"
"{\n"
"  int g_width;\n"
"  int g_height;\n"
"  uint g_primary_line;\n"
"  uint g_is_second;\n"
"};\n"
"\n"
"Texture2D<float> prevTex : register(t0);\n"
"Texture2D<float> curTex : register(t1);\n"
"Texture2D<float> nextTex : register(t2);\n"
"RWTexture2D<unorm float> outTex : register(u0);\n"
"\n"
"float max3 (float a, float b, float c)\n"
"{\n"
"  return max (max (a, b), c);\n"
"}\n"
"\n"
"float min3 (float a, float b, float c)\n"
"{\n"
"  return min (min (a, b), c);\n"
"}\n"
"\n"
"float\n"
"SpatialPred (float a, float b, float c, float d, float e, float f, float g,\n"
"    float h, float i, float j, float k, float l, float m, float n)\n"
"{\n"
"  float spatial_pred = (d + k) / 2;\n"
"  float spatial_score = abs (c - j) + abs (d - k) + abs (e - l);\n"
"  float score = abs (b - k) + abs (c - l) + abs (d - m);\n"
"  if (score < spatial_score) {\n"
"    spatial_pred = (c + l) / 2;\n"
"    spatial_score = score;\n"
"    score = abs (a - l) + abs (b - m) + abs (c - n);\n"
"    if (score < spatial_score) {\n"
"      spatial_pred = (b + m) / 2;\n"
"      spatial_score = score;\n"
"    }\n"
"  }\n"
"  score = abs (d - i) + abs (e - j) + abs (f - k);\n"
"  if (score < spatial_score) {\n"
"    spatial_pred = (e + j) / 2;\n"
"    spatial_score = score;\n"
"    score = abs (e - h) + abs (f - i) + abs (g - j);\n"
"    if (score < spatial_score) {\n"
"      spatial_pred = (f + i) / 2;\n"
"      spatial_score = score;\n"
"    }\n"
"  }\n"
"  return spatial_pred;\n"
"}\n"
"\n"
"float\n"
"TemporalPred (float A, float B, float C, float D, float E, float F,\n"
"    float G, float H, float I, float J, float K, float L, float spatial_pred)\n"
"{\n"
"  float p0 = (C + H) / 2;\n"
"  float p1 = F;\n"
"  float p2 = (D + I) / 2;\n"
"  float p3 = G;\n"
"  float p4 = (E + J) / 2;\n"
"  float tdiff0 = abs (D - I);\n"
"  float tdiff1 = (abs (A - F) + abs (B - G)) / 2;\n"
"  float tdiff2 = (abs (K - F) + abs (G - L)) / 2;\n"
"  float diff = max3 (tdiff0, tdiff1, tdiff2);\n"
"  float maxi = max3 (p2 - p3, p2 - p1, min (p0 - p1, p4 - p3));\n"
"  float mini = min3 (p2 - p3, p2 - p1, max (p0 - p1, p4 - p3));\n"
"  diff = max3 (diff, mini, -maxi);\n"
"  if (spatial_pred > p2 + diff)\n"
"    spatial_pred = p2 + diff;\n"
"  if (spatial_pred < p2 - diff)\n"
"    spatial_pred = p2 - diff;\n"
"  return spatial_pred;\n"
"}\n"
"\n"
"int GetPosX (int x)\n"
"{\n"
"  return clamp (x, 0, g_width - 1);\n"
"}\n"
"\n"
"int GetPosY (int y)\n"
"{\n"
"  return clamp (y, 0, g_height - 1);\n"
"}\n"
"\n"
"void Execute (int2 pos)\n"
"{\n"
"  if (pos.x < g_width && pos.y < g_height) {\n"
"    [branch] if ((uint(pos.y) % 2) == g_primary_line) {\n"
"      outTex[uint2(pos.x, pos.y)] = curTex.Load(int3(pos, 0));\n"
"    } else {\n"
"      float a = curTex.Load(int3(GetPosX(pos.x - 3), GetPosY(pos.y - 1), 0));\n"
"      float b = curTex.Load(int3(GetPosX(pos.x - 2), GetPosY(pos.y - 1), 0));\n"
"      float c = curTex.Load(int3(GetPosX(pos.x - 1), GetPosY(pos.y - 1), 0));\n"
"      float d = curTex.Load(int3(        pos.x,              pos.y - 1,  0));\n"
"      float e = curTex.Load(int3(GetPosX(pos.x + 1), GetPosY(pos.y - 1), 0));\n"
"      float f = curTex.Load(int3(GetPosX(pos.x + 2), GetPosY(pos.y - 1), 0));\n"
"      float g = curTex.Load(int3(GetPosX(pos.x + 3), GetPosY(pos.y - 1), 0));\n"
"      float h = curTex.Load(int3(GetPosX(pos.x - 3), GetPosY(pos.y + 1), 0));\n"
"      float i = curTex.Load(int3(GetPosX(pos.x - 2), GetPosY(pos.y + 1), 0));\n"
"      float j = curTex.Load(int3(GetPosX(pos.x - 1), GetPosY(pos.y + 1), 0));\n"
"      float k = curTex.Load(int3(        pos.x,      GetPosY(pos.y + 1), 0));\n"
"      float l = curTex.Load(int3(GetPosX(pos.x + 1), GetPosY(pos.y + 1), 0));\n"
"      float m = curTex.Load(int3(GetPosX(pos.x + 2), GetPosY(pos.y + 1), 0));\n"
"      float n = curTex.Load(int3(GetPosX(pos.x + 3), GetPosY(pos.y + 1), 0));\n"
"      float spatial_pred =\n"
"          SpatialPred (a, b, c, d, e, f, g, h, i, j, k, l, m, n);\n"
"      float A = prevTex.Load(int3(pos.x, GetPosY(pos.y - 1), 0));\n"
"      float B = prevTex.Load(int3(pos.x, GetPosY(pos.y + 1), 0));\n"
"      float C, D, E;\n"
"      if (g_is_second) {\n"
"        C = curTex.Load(int3(pos.x, GetPosY(pos.y - 2), 0));\n"
"        D = curTex.Load(int3(pos.x,         pos.y,      0));\n"
"        E = curTex.Load(int3(pos.x, GetPosY(pos.y + 2), 0));\n"
"      } else {\n"
"        C = prevTex.Load(int3(pos.x, GetPosY(pos.y - 2), 0));\n"
"        D = prevTex.Load(int3(pos.x,         pos.y,      0));\n"
"        E = prevTex.Load(int3(pos.x, GetPosY(pos.y + 2), 0));\n"
"      }\n"
"      float F =   curTex.Load(int3(pos.x, GetPosY(pos.y - 1), 0));\n"
"      float G =   curTex.Load(int3(pos.x, GetPosY(pos.y + 1), 0));\n"
"      float H, I, J;\n"
"      if (g_is_second) {\n"
"        H = nextTex.Load(int3(pos.x, GetPosY(pos.y - 2), 0));\n"
"        I = nextTex.Load(int3(pos.x,         pos.y,      0));\n"
"        J = nextTex.Load(int3(pos.x, GetPosY(pos.y + 2), 0));\n"
"      } else {\n"
"        H = curTex.Load(int3(pos.x, GetPosY(pos.y - 2), 0));\n"
"        I = curTex.Load(int3(pos.x,         pos.y,      0));\n"
"        J = curTex.Load(int3(pos.x, GetPosY(pos.y + 2), 0));\n"
"      }\n"
"      float K = nextTex.Load(int3(pos.x, GetPosY(pos.y - 1), 0));\n"
"      float L = nextTex.Load(int3(pos.x, GetPosY(pos.y + 1), 0));\n"
"      spatial_pred =\n"
"          TemporalPred(A, B, C, D, E, F, G, H, I, J, K, L, spatial_pred);\n"
"      outTex[uint2(pos.x, pos.y)] = clamp (spatial_pred, 0, 4095.0 / 65535.0);\n"
"    }\n"
"  }\n"
"}\n"
"\n"
"[numthreads(8, 8, 1)]\n"
"void ENTRY_POINT (uint3 tid : SV_DispatchThreadID)\n"
"{\n"
"  Execute (int2(tid.x, tid.y));\n"
"}\n";
#endif
