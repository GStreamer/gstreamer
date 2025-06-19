/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
RWTexture2D<float4> uvLUT : register(u0);

cbuffer Parameters : register(b0)
{
  float2 fisheyeCenter;
  float2 fisheyeRadius;

  float maxAngle;
  float horizontalFOV; // Unused
  float verticalFOV; // Unused
  float rollAngle;

  float2 roiOffset;
  float2 roiScale;

  float innerRadius;
  float3 padding;

  float3x3 RotationMatrix; // Unused
};

[numthreads(8, 8, 1)]
void ENTRY_POINT (uint3 DTid : SV_DispatchThreadID)
{
  uint width, height;
  uvLUT.GetDimensions(width, height);
  if (DTid.x >= width || DTid.y >= height)
    return;

  // Compute normalized screen coordinate
  float2 uv = float2(DTid.xy) / float2(width, height);

  // Apply ROI cropping and scaling
  float2 uv_roi = roiOffset + uv * roiScale;

  // Zenith angle (theta): 0 = center, maxAngle = outer edge
  float minTheta = maxAngle * saturate(innerRadius);
  float theta = lerp(minTheta, maxAngle, 1.0 - uv_roi.y);

  // Map to azimuthal angle (phi) across full 360 degrees
  float phi = -6.28318530718 * (uv_roi.x - 0.5) + rollAngle;

  float4 fishUV = float4(0.0, 0.0, 0.0, 1.0);
  if (theta >= minTheta && theta <= maxAngle) {
    // Convert spherical coordinates to 2D fisheye UV using equidistant projection
    float2 r = (fisheyeRadius / maxAngle) * theta;
    fishUV.xy = fisheyeCenter + r * float2(cos(phi), -sin(phi));
  } else {
    // Out of view
    fishUV.w = 0.0;
  }

  uvLUT[DTid.xy] = fishUV;
}
#else
static const char str_CSMain_fisheye_panorama[] =
"RWTexture2D<float4> uvLUT : register(u0);\n"
"\n"
"cbuffer Parameters : register(b0)\n"
"{\n"
"  float2 fisheyeCenter;\n"
"  float2 fisheyeRadius;\n"
"\n"
"  float maxAngle;\n"
"  float horizontalFOV; // Unused\n"
"  float verticalFOV; // Unused\n"
"  float rollAngle;\n"
"\n"
"  float2 roiOffset;\n"
"  float2 roiScale;\n"
"\n"
"  float innerRadius;\n"
"  float3 padding;\n"
"\n"
"  float3x3 RotationMatrix; // Unused\n"
"};\n"
"\n"
"[numthreads(8, 8, 1)]\n"
"void ENTRY_POINT (uint3 DTid : SV_DispatchThreadID)\n"
"{\n"
"  uint width, height;\n"
"  uvLUT.GetDimensions(width, height);\n"
"  if (DTid.x >= width || DTid.y >= height)\n"
"    return;\n"
"\n"
"  // Compute normalized screen coordinate\n"
"  float2 uv = float2(DTid.xy) / float2(width, height);\n"
"\n"
"  // Apply ROI cropping and scaling\n"
"  float2 uv_roi = roiOffset + uv * roiScale;\n"
"\n"
"  // Zenith angle (theta): 0 = center, maxAngle = outer edge\n"
"  float minTheta = maxAngle * saturate(innerRadius);\n"
"  float theta = lerp(minTheta, maxAngle, 1.0 - uv_roi.y);\n"
"\n"
"  // Map to azimuthal angle (phi) across full 360 degrees\n"
"  float phi = -6.28318530718 * (uv_roi.x - 0.5) + rollAngle;\n"
"\n"
"  float4 fishUV = float4(0.0, 0.0, 0.0, 1.0);\n"
"  if (theta >= minTheta && theta <= maxAngle) {\n"
"    // Convert spherical coordinates to 2D fisheye UV using equidistant projection\n"
"    float2 r = (fisheyeRadius / maxAngle) * theta;\n"
"    fishUV.xy = fisheyeCenter + r * float2(cos(phi), -sin(phi));\n"
"  } else {\n"
"    // Out of view\n"
"    fishUV.w = 0.0;\n"
"  }\n"
"\n"
"  uvLUT[DTid.xy] = fishUV;\n"
"}\n";
#endif
