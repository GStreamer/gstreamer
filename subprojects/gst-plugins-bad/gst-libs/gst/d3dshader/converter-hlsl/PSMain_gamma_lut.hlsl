/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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
#define GAMMA10 0
#define GAMMA18 1
#define GAMMA20 2
#define GAMMA22 3
#define BT709   4
#define SMPTE240M 5
#define SRGB 6
#define GAMMA28 7
#define LOG100 8
#define LOG316 9
#define BT2020 10
#define ADOBERGB 11
#define PQ 12
#define HLG 13

cbuffer LutBuilderCB : register(b0)
{
  uint IsDecode;
  uint LutType;
};

struct PS_INPUT
{
  float4 Position : SV_POSITION;
  float2 Texture : TEXCOORD;
};

float Decode(float val)
{
  [forcecase] switch (LutType) {
    case GAMMA18:
      return pow (val, 1.8);
    case GAMMA20:
      return pow (val, 2.0);
    case GAMMA22:
      return pow (val, 2.2);
    case BT709:
      if (val < 0.081)
        return val / 4.5;
      else
        return pow ((val + 0.099) / 1.099, 1.0 / 0.45);
    case SMPTE240M:
      if (val < 0.0913)
        return val / 4.0;
      else
        return pow ((val + 0.1115) / 1.1115, 1.0 / 0.45);
    case SRGB:
      if (val <= 0.04045)
        return val / 12.92;
      else
        return pow ((val + 0.055) / 1.055, 2.4);
    case GAMMA28:
      return pow (val, 2.8);
    case LOG100:
      if (val == 0.0)
        return 0.0;
      else
        return pow (10.0, 2.0 * (val - 1.0));
    case LOG316:
      if (val == 0.0)
        return 0.0;
      else
        return pow (10.0, 2.5 * (val - 1.0));
    case BT2020:
      if (val < 0.08145)
        return val / 4.5;
      else
        return pow ((val + 0.0993) / 1.0993, 1.0 / 0.45);
    case ADOBERGB:
      return pow (val, 2.19921875);
    case PQ:
    {
      float c1 = 0.8359375;
      float c2 = 18.8515625;
      float c3 = 18.6875;
      float m1 = 0.1593017578125;
      float m2 = 78.84375;
      float tmp = pow (val, 1.0 / m2);
      float tmp2 = max (tmp - c1, 0.0);
      return pow (tmp2 / abs(c2 - c3 * tmp), 1 / m1);
    }
    case HLG:
    {
      float a = 0.17883277;
      float b = 0.28466892;
      float c = 0.55991073;

      if (val > 0.5)
        return (exp ((val - c) / a) + b) / 12.0;
      else
        return val * val / 3.0;
    }
    default:
      return val;
  }
}

float Encode(float val)
{
  [forcecase] switch (LutType) {
    case GAMMA18:
      return pow (val, 1.0 / 1.8);
    case GAMMA20:
      return pow (val, 1.0 / 2.0);
    case GAMMA22:
      return pow (val, 1.0 / 2.2);
    case BT709:
      if (val < 0.018)
        return 4.5 * val;
      else
        return 1.099 * pow (val, 0.45) - 0.099;
    case SMPTE240M:
      if (val < 0.0228)
        return val * 4.0;
      else
        return 1.1115 * pow (val, 0.45) - 0.1115;
    case SRGB:
      if (val <= 0.0031308)
        return 12.92 * val;
      else
        return 1.055 * pow (val, 1.0 / 2.4) - 0.055;
    case GAMMA28:
      return pow (val, 1 / 2.8);
    case LOG100:
      if (val < 0.01)
        return 0.0;
      else
        return 1.0 + log10 (val) / 2.0;
    case LOG316:
      if (val < 0.0031622777)
        return 0.0;
      else
        return 1.0 + log10 (val) / 2.5;
    case BT2020:
      if (val < 0.0181)
        return 4.5 * val;
      else
        return 1.0993 * pow (val, 0.45) - 0.0993;
    case ADOBERGB:
      return pow (val, 1.0 / 2.19921875);
    case PQ:
    {
      float c1 = 0.8359375;
      float c2 = 18.8515625;
      float c3 = 18.6875;
      float m1 = 0.1593017578125;
      float m2 = 78.84375;
      float Ln = pow (val, m1);
      return pow ((c1 + c2 * Ln) / (1.0 + c3 * Ln), m2);
    }
    case HLG:
    {
      float a = 0.17883277;
      float b = 0.28466892;
      float c = 0.55991073;

      if (val > (1.0 / 12.0))
        return a * log (12.0 * val - b) + c;
      else
        return sqrt (3.0 * val);
    }
    default:
      return val;
  }
}

float4 ENTRY_POINT (PS_INPUT input) : SV_TARGET
{
  float value;
  [branch] if (IsDecode) {
    value = Decode(abs(input.Texture.x));
  } else {
    value = Encode(abs(input.Texture.x));
  }
  return float4 (value, 0.0, 0.0, 1.0);
}
#else
static const char str_PSMain_gamma_lut[] =
"#define GAMMA10 0\n"
"#define GAMMA18 1\n"
"#define GAMMA20 2\n"
"#define GAMMA22 3\n"
"#define BT709   4\n"
"#define SMPTE240M 5\n"
"#define SRGB 6\n"
"#define GAMMA28 7\n"
"#define LOG100 8\n"
"#define LOG316 9\n"
"#define BT2020 10\n"
"#define ADOBERGB 11\n"
"#define PQ 12\n"
"#define HLG 13\n"
"\n"
"cbuffer LutBuilderCB : register(b0)\n"
"{\n"
"  uint IsDecode;\n"
"  uint LutType;\n"
"};\n"
"\n"
"struct PS_INPUT\n"
"{\n"
"  float4 Position : SV_POSITION;\n"
"  float2 Texture : TEXCOORD;\n"
"};\n"
"\n"
"float Decode(float val)\n"
"{\n"
"  [forcecase] switch (LutType) {\n"
"    case GAMMA18:\n"
"      return pow (val, 1.8);\n"
"    case GAMMA20:\n"
"      return pow (val, 2.0);\n"
"    case GAMMA22:\n"
"      return pow (val, 2.2);\n"
"    case BT709:\n"
"      if (val < 0.081)\n"
"        return val / 4.5;\n"
"      else\n"
"        return pow ((val + 0.099) / 1.099, 1.0 / 0.45);\n"
"    case SMPTE240M:\n"
"      if (val < 0.0913)\n"
"        return val / 4.0;\n"
"      else\n"
"        return pow ((val + 0.1115) / 1.1115, 1.0 / 0.45);\n"
"    case SRGB:\n"
"      if (val <= 0.04045)\n"
"        return val / 12.92;\n"
"      else\n"
"        return pow ((val + 0.055) / 1.055, 2.4);\n"
"    case GAMMA28:\n"
"      return pow (val, 2.8);\n"
"    case LOG100:\n"
"      if (val == 0.0)\n"
"        return 0.0;\n"
"      else\n"
"        return pow (10.0, 2.0 * (val - 1.0));\n"
"    case LOG316:\n"
"      if (val == 0.0)\n"
"        return 0.0;\n"
"      else\n"
"        return pow (10.0, 2.5 * (val - 1.0));\n"
"    case BT2020:\n"
"      if (val < 0.08145)\n"
"        return val / 4.5;\n"
"      else\n"
"        return pow ((val + 0.0993) / 1.0993, 1.0 / 0.45);\n"
"    case ADOBERGB:\n"
"      return pow (val, 2.19921875);\n"
"    case PQ:\n"
"    {\n"
"      float c1 = 0.8359375;\n"
"      float c2 = 18.8515625;\n"
"      float c3 = 18.6875;\n"
"      float m1 = 0.1593017578125;\n"
"      float m2 = 78.84375;\n"
"      float tmp = pow (val, 1.0 / m2);\n"
"      float tmp2 = max (tmp - c1, 0.0);\n"
"      return pow (tmp2 / abs(c2 - c3 * tmp), 1 / m1);\n"
"    }\n"
"    case HLG:\n"
"    {\n"
"      float a = 0.17883277;\n"
"      float b = 0.28466892;\n"
"      float c = 0.55991073;\n"
"\n"
"      if (val > 0.5)\n"
"        return (exp ((val - c) / a) + b) / 12.0;\n"
"      else\n"
"        return val * val / 3.0;\n"
"    }\n"
"    default:\n"
"      return val;\n"
"  }\n"
"}\n"
"\n"
"float Encode(float val)\n"
"{\n"
"  [forcecase] switch (LutType) {\n"
"    case GAMMA18:\n"
"      return pow (val, 1.0 / 1.8);\n"
"    case GAMMA20:\n"
"      return pow (val, 1.0 / 2.0);\n"
"    case GAMMA22:\n"
"      return pow (val, 1.0 / 2.2);\n"
"    case BT709:\n"
"      if (val < 0.018)\n"
"        return 4.5 * val;\n"
"      else\n"
"        return 1.099 * pow (val, 0.45) - 0.099;\n"
"    case SMPTE240M:\n"
"      if (val < 0.0228)\n"
"        return val * 4.0;\n"
"      else\n"
"        return 1.1115 * pow (val, 0.45) - 0.1115;\n"
"    case SRGB:\n"
"      if (val <= 0.0031308)\n"
"        return 12.92 * val;\n"
"      else\n"
"        return 1.055 * pow (val, 1.0 / 2.4) - 0.055;\n"
"    case GAMMA28:\n"
"      return pow (val, 1 / 2.8);\n"
"    case LOG100:\n"
"      if (val < 0.01)\n"
"        return 0.0;\n"
"      else\n"
"        return 1.0 + log10 (val) / 2.0;\n"
"    case LOG316:\n"
"      if (val < 0.0031622777)\n"
"        return 0.0;\n"
"      else\n"
"        return 1.0 + log10 (val) / 2.5;\n"
"    case BT2020:\n"
"      if (val < 0.0181)\n"
"        return 4.5 * val;\n"
"      else\n"
"        return 1.0993 * pow (val, 0.45) - 0.0993;\n"
"    case ADOBERGB:\n"
"      return pow (val, 1.0 / 2.19921875);\n"
"    case PQ:\n"
"    {\n"
"      float c1 = 0.8359375;\n"
"      float c2 = 18.8515625;\n"
"      float c3 = 18.6875;\n"
"      float m1 = 0.1593017578125;\n"
"      float m2 = 78.84375;\n"
"      float Ln = pow (val, m1);\n"
"      return pow ((c1 + c2 * Ln) / (1.0 + c3 * Ln), m2);\n"
"    }\n"
"    case HLG:\n"
"    {\n"
"      float a = 0.17883277;\n"
"      float b = 0.28466892;\n"
"      float c = 0.55991073;\n"
"\n"
"      if (val > (1.0 / 12.0))\n"
"        return a * log (12.0 * val - b) + c;\n"
"      else\n"
"        return sqrt (3.0 * val);\n"
"    }\n"
"    default:\n"
"      return val;\n"
"  }\n"
"}\n"
"\n"
"float4 ENTRY_POINT (PS_INPUT input) : SV_TARGET\n"
"{\n"
"  float value;\n"
"  [branch] if (IsDecode) {\n"
"    value = Decode(abs(input.Texture.x));\n"
"  } else {\n"
"    value = Encode(abs(input.Texture.x));\n"
"  }\n"
"  return float4 (value, 0.0, 0.0, 1.0);\n"
"}\n";
#endif
