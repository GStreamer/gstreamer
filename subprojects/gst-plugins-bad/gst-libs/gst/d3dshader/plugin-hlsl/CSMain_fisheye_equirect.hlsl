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
  float horizontalFOV;
  float verticalFOV;
  float rollAngle; // unused

  float2 roiOffset;
  float2 roiScale;

  float4 padding;

  float3x3 RotationMatrix;
};

[numthreads(8, 8, 1)]
void ENTRY_POINT (uint3 DTid : SV_DispatchThreadID)
{
  uint width, height;
  uvLUT.GetDimensions(width, height);
  if (DTid.x >= width || DTid.y >= height)
      return;

  // Compute normalized screen coordinate
  float2 uv = float2(DTid.x, DTid.y) / float2(width, height);

  // Apply ROI cropping and scaling
  float2 uv_roi = roiOffset + uv * roiScale;

  // Convert to spherical coordinates (delta = latitude, psi = longitude)
  float delta = verticalFOV * (uv_roi.y - 0.5); // up-down angle
  float psi = horizontalFOV * (uv_roi.x - 0.5); // left-right angle

  // Convert spherical to 3D ray (Z-forward)
  float cosD = cos(delta);
  float sinD = sin(delta);
  float cosP = cos(psi);
  float sinP = sin(psi);

  float3 ray = float3(
    cosD * sinP, // X
    sinD,        // Y
    cosD * cosP  // Z
  );

  // Apply rotation matrix
  float3 rotatedRay = mul(RotationMatrix, ray);
  rotatedRay = normalize(rotatedRay);

  // Convert back to spherical angles
  float theta = acos(rotatedRay.z); // zenith angle

  float4 fishUV = float4(0.0, 0.0, 0.0, 1.0);
  if (theta <= maxAngle) {
    // azimuth angle
    float phi = atan2(rotatedRay.y, rotatedRay.x);

    // Map to fisheye UV via equidistant projection
    float2 r = (fisheyeRadius / maxAngle) * theta;
    fishUV.xy = fisheyeCenter + r * float2(cos(phi), sin(phi));
  } else {
    // Out of view
    fishUV.w = 0.0;
  }

  uvLUT[DTid.xy] = fishUV;
}
#else
static const char str_CSMain_fisheye_equirect[] =
"RWTexture2D<float4> uvLUT : register(u0);\n"
"\n"
"cbuffer Parameters : register(b0)\n"
"{\n"
"  float2 fisheyeCenter;\n"
"  float2 fisheyeRadius;\n"
"\n"
"  float maxAngle;\n"
"  float horizontalFOV;\n"
"  float verticalFOV;\n"
"  float rollAngle; // unused\n"
"\n"
"  float2 roiOffset;\n"
"  float2 roiScale;\n"
"\n"
"  float4 padding;\n"
"\n"
"  float3x3 RotationMatrix;\n"
"};\n"
"\n"
"[numthreads(8, 8, 1)]\n"
"void ENTRY_POINT (uint3 DTid : SV_DispatchThreadID)\n"
"{\n"
"  uint width, height;\n"
"  uvLUT.GetDimensions(width, height);\n"
"  if (DTid.x >= width || DTid.y >= height)\n"
"      return;\n"
"\n"
"  // Compute normalized screen coordinate\n"
"  float2 uv = float2(DTid.x, DTid.y) / float2(width, height);\n"
"\n"
"  // Apply ROI cropping and scaling\n"
"  float2 uv_roi = roiOffset + uv * roiScale;\n"
"\n"
"  // Convert to spherical coordinates (delta = latitude, psi = longitude)\n"
"  float delta = verticalFOV * (uv_roi.y - 0.5); // up-down angle\n"
"  float psi = horizontalFOV * (uv_roi.x - 0.5); // left-right angle\n"
"\n"
"  // Convert spherical to 3D ray (Z-forward)\n"
"  float cosD = cos(delta);\n"
"  float sinD = sin(delta);\n"
"  float cosP = cos(psi);\n"
"  float sinP = sin(psi);\n"
"\n"
"  float3 ray = float3(\n"
"    cosD * sinP, // X\n"
"    sinD,        // Y\n"
"    cosD * cosP  // Z\n"
"  );\n"
"\n"
"  // Apply rotation matrix\n"
"  float3 rotatedRay = mul(RotationMatrix, ray);\n"
"  rotatedRay = normalize(rotatedRay);\n"
"\n"
"  // Convert back to spherical angles\n"
"  float theta = acos(rotatedRay.z); // zenith angle\n"
"\n"
"  float4 fishUV = float4(0.0, 0.0, 0.0, 1.0);\n"
"  if (theta <= maxAngle) {\n"
"    // azimuth angle\n"
"    float phi = atan2(rotatedRay.y, rotatedRay.x);\n"
"\n"
"    // Map to fisheye UV via equidistant projection\n"
"    float2 r = (fisheyeRadius / maxAngle) * theta;\n"
"    fishUV.xy = fisheyeCenter + r * float2(cos(phi), sin(phi));\n"
"  } else {\n"
"    // Out of view\n"
"    fishUV.w = 0.0;\n"
"  }\n"
"\n"
"  uvLUT[DTid.xy] = fishUV;\n"
"}\n";
#endif
