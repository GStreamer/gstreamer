/**
 * MIT License
 *
 * Copyright (c) 2018 Jeremiah van Oosten
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Source: https://github.com/jpvanoosten/LearningDirectX12 */

#ifdef BUILDING_HLSL

#define BLOCK_SIZE 8

 // When reducing the size of a texture, it could be that downscaling the texture
 // will result in a less than exactly 50% (1/2) of the original texture size.
 // This happens if either the width, or the height (or both) dimensions of the texture
 // are odd. For example, downscaling a 5x3 texture will result in a 2x1 texture which
 // has a 60% reduction in the texture width and 66% reduction in the height.
 // When this happens, we need to take more samples from the source texture to
 // determine the pixel value in the destination texture.

#define WIDTH_HEIGHT_EVEN 0     // Both the width and the height of the texture are even.
#define WIDTH_ODD_HEIGHT_EVEN 1 // The texture width is odd and the height is even.
#define WIDTH_EVEN_HEIGHT_ODD 2 // The texture width is even and teh height is odd.
#define WIDTH_HEIGHT_ODD 3      // Both the width and height of the texture are odd.

struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

cbuffer GenerateMipsCB : register( b0 )
{
    uint SrcMipLevel;   // Texture level of source mip
    uint NumMipLevels;  // Number of OutMips to write: [1-4]
    uint SrcDimension;  // Width and height of the source texture are even or odd.
    uint padding;
    float2 TexelSize;   // 1.0 / OutMip1.Dimensions
}

// Source mip map.
Texture2D<float> SrcMip : register( t0 );

// Write up to 4 mip map levels.
RWTexture2D<float> OutMip1 : register( u0 );
RWTexture2D<float> OutMip2 : register( u1 );
RWTexture2D<float> OutMip3 : register( u2 );
RWTexture2D<float> OutMip4 : register( u3 );

// Linear clamp sampler.
SamplerState LinearClampSampler : register( s0 );

// The reason for separating channels is to reduce bank conflicts in the
// local data memory controller.  A large stride will cause more threads
// to collide on the same memory bank.
groupshared float gs_R[64];

void StoreColor( uint Index, float Color )
{
    gs_R[Index] = Color;
}

float LoadColor( uint Index )
{
    return gs_R[Index];
}

[numthreads( BLOCK_SIZE, BLOCK_SIZE, 1 )]
void ENTRY_POINT( ComputeShaderInput IN )
{
    float Src1 = 0;

    // One bilinear sample is insufficient when scaling down by more than 2x.
    // You will slightly undersample in the case where the source dimension
    // is odd.  This is why it's a really good idea to only generate mips on
    // power-of-two sized textures.  Trying to handle the undersampling case
    // will force this shader to be slower and more complicated as it will
    // have to take more source texture samples.

    // Determine the path to use based on the dimension of the
    // source texture.
    // 0b00(0): Both width and height are even.
    // 0b01(1): Width is odd, height is even.
    // 0b10(2): Width is even, height is odd.
    // 0b11(3): Both width and height are odd.
    switch ( SrcDimension )
    {
        case WIDTH_HEIGHT_EVEN:
        {
            float2 UV = TexelSize * ( IN.DispatchThreadID.xy + 0.5 );

            Src1 = SrcMip.SampleLevel( LinearClampSampler, UV, SrcMipLevel );
        }
        break;
        case WIDTH_ODD_HEIGHT_EVEN:
        {
            // > 2:1 in X dimension
            // Use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
            // horizontally.
            float2 UV1 = TexelSize * ( IN.DispatchThreadID.xy + float2( 0.25, 0.5 ) );
            float2 Off = TexelSize * float2( 0.5, 0.0 );

            Src1 = 0.5 * ( SrcMip.SampleLevel( LinearClampSampler, UV1, SrcMipLevel ) +
                           SrcMip.SampleLevel( LinearClampSampler, UV1 + Off, SrcMipLevel ) );
        }
        break;
        case WIDTH_EVEN_HEIGHT_ODD:
        {
            // > 2:1 in Y dimension
            // Use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
            // vertically.
            float2 UV1 = TexelSize * ( IN.DispatchThreadID.xy + float2( 0.5, 0.25 ) );
            float2 Off = TexelSize * float2( 0.0, 0.5 );

            Src1 = 0.5 * ( SrcMip.SampleLevel( LinearClampSampler, UV1, SrcMipLevel ) +
                           SrcMip.SampleLevel( LinearClampSampler, UV1 + Off, SrcMipLevel ) );
        }
        break;
        case WIDTH_HEIGHT_ODD:
        {
            // > 2:1 in in both dimensions
            // Use 4 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
            // in both directions.
            float2 UV1 = TexelSize * ( IN.DispatchThreadID.xy + float2( 0.25, 0.25 ) );
            float2 Off = TexelSize * 0.5;

            Src1 =  SrcMip.SampleLevel( LinearClampSampler, UV1, SrcMipLevel );
            Src1 += SrcMip.SampleLevel( LinearClampSampler, UV1 + float2( Off.x, 0.0   ), SrcMipLevel );
            Src1 += SrcMip.SampleLevel( LinearClampSampler, UV1 + float2( 0.0,   Off.y ), SrcMipLevel );
            Src1 += SrcMip.SampleLevel( LinearClampSampler, UV1 + float2( Off.x, Off.y ), SrcMipLevel );
            Src1 *= 0.25;
        }
        break;
    }

    OutMip1[IN.DispatchThreadID.xy] = Src1;

    // A scalar (constant) branch can exit all threads coherently.
    if ( NumMipLevels == 1 )
        return;

    // Without lane swizzle operations, the only way to share data with other
    // threads is through LDS.
    StoreColor( IN.GroupIndex, Src1 );

    // This guarantees all LDS writes are complete and that all threads have
    // executed all instructions so far (and therefore have issued their LDS
    // write instructions.)
    GroupMemoryBarrierWithGroupSync();

    // With low three bits for X and high three bits for Y, this bit mask
    // (binary: 001001) checks that X and Y are even.
    if ( ( IN.GroupIndex & 0x9 ) == 0 )
    {
        float Src2 = LoadColor( IN.GroupIndex + 0x01 );
        float Src3 = LoadColor( IN.GroupIndex + 0x08 );
        float Src4 = LoadColor( IN.GroupIndex + 0x09 );
        Src1 = 0.25 * ( Src1 + Src2 + Src3 + Src4 );

        OutMip2[IN.DispatchThreadID.xy / 2] = Src1;
        StoreColor( IN.GroupIndex, Src1 );
    }

    if ( NumMipLevels == 2 )
        return;

    GroupMemoryBarrierWithGroupSync();

    // This bit mask (binary: 011011) checks that X and Y are multiples of four.
    if ( ( IN.GroupIndex & 0x1B ) == 0 )
    {
        float Src2 = LoadColor( IN.GroupIndex + 0x02 );
        float Src3 = LoadColor( IN.GroupIndex + 0x10 );
        float Src4 = LoadColor( IN.GroupIndex + 0x12 );
        Src1 = 0.25 * ( Src1 + Src2 + Src3 + Src4 );

        OutMip3[IN.DispatchThreadID.xy / 4] = Src1;
        StoreColor( IN.GroupIndex, Src1 );
    }

    if ( NumMipLevels == 3 )
        return;

    GroupMemoryBarrierWithGroupSync();

    // This bit mask would be 111111 (X & Y multiples of 8), but only one
    // thread fits that criteria.
    if ( IN.GroupIndex == 0 )
    {
        float Src2 = LoadColor( IN.GroupIndex + 0x04 );
        float Src3 = LoadColor( IN.GroupIndex + 0x20 );
        float Src4 = LoadColor( IN.GroupIndex + 0x24 );
        Src1 = 0.25 * ( Src1 + Src2 + Src3 + Src4 );

        OutMip4[IN.DispatchThreadID.xy / 8] = Src1;
    }
}
#else
static const char str_CSMain_mipgen_gray[] =
"#define BLOCK_SIZE 8\n"
"\n"
" // When reducing the size of a texture, it could be that downscaling the texture\n"
" // will result in a less than exactly 50% (1/2) of the original texture size.\n"
" // This happens if either the width, or the height (or both) dimensions of the texture\n"
" // are odd. For example, downscaling a 5x3 texture will result in a 2x1 texture which\n"
" // has a 60% reduction in the texture width and 66% reduction in the height.\n"
" // When this happens, we need to take more samples from the source texture to\n"
" // determine the pixel value in the destination texture.\n"
"\n"
"#define WIDTH_HEIGHT_EVEN 0     // Both the width and the height of the texture are even.\n"
"#define WIDTH_ODD_HEIGHT_EVEN 1 // The texture width is odd and the height is even.\n"
"#define WIDTH_EVEN_HEIGHT_ODD 2 // The texture width is even and teh height is odd.\n"
"#define WIDTH_HEIGHT_ODD 3      // Both the width and height of the texture are odd.\n"
"\n"
"struct ComputeShaderInput\n"
"{\n"
"    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.\n"
"    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.\n"
"    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.\n"
"    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.\n"
"};\n"
"\n"
"cbuffer GenerateMipsCB : register( b0 )\n"
"{\n"
"    uint SrcMipLevel;   // Texture level of source mip\n"
"    uint NumMipLevels;  // Number of OutMips to write: [1-4]\n"
"    uint SrcDimension;  // Width and height of the source texture are even or odd.\n"
"    uint padding;\n"
"    float2 TexelSize;   // 1.0 / OutMip1.Dimensions\n"
"}\n"
"\n"
"// Source mip map.\n"
"Texture2D<float> SrcMip : register( t0 );\n"
"\n"
"// Write up to 4 mip map levels.\n"
"RWTexture2D<float> OutMip1 : register( u0 );\n"
"RWTexture2D<float> OutMip2 : register( u1 );\n"
"RWTexture2D<float> OutMip3 : register( u2 );\n"
"RWTexture2D<float> OutMip4 : register( u3 );\n"
"\n"
"// Linear clamp sampler.\n"
"SamplerState LinearClampSampler : register( s0 );\n"
"\n"
"// The reason for separating channels is to reduce bank conflicts in the\n"
"// local data memory controller.  A large stride will cause more threads\n"
"// to collide on the same memory bank.\n"
"groupshared float gs_R[64];\n"
"\n"
"void StoreColor( uint Index, float Color )\n"
"{\n"
"    gs_R[Index] = Color;\n"
"}\n"
"\n"
"float LoadColor( uint Index )\n"
"{\n"
"    return gs_R[Index];\n"
"}\n"
"\n"
"[numthreads( BLOCK_SIZE, BLOCK_SIZE, 1 )]\n"
"void ENTRY_POINT( ComputeShaderInput IN )\n"
"{\n"
"    float Src1 = 0;\n"
"\n"
"    // One bilinear sample is insufficient when scaling down by more than 2x.\n"
"    // You will slightly undersample in the case where the source dimension\n"
"    // is odd.  This is why it's a really good idea to only generate mips on\n"
"    // power-of-two sized textures.  Trying to handle the undersampling case\n"
"    // will force this shader to be slower and more complicated as it will\n"
"    // have to take more source texture samples.\n"
"\n"
"    // Determine the path to use based on the dimension of the\n"
"    // source texture.\n"
"    // 0b00(0): Both width and height are even.\n"
"    // 0b01(1): Width is odd, height is even.\n"
"    // 0b10(2): Width is even, height is odd.\n"
"    // 0b11(3): Both width and height are odd.\n"
"    switch ( SrcDimension )\n"
"    {\n"
"        case WIDTH_HEIGHT_EVEN:\n"
"        {\n"
"            float2 UV = TexelSize * ( IN.DispatchThreadID.xy + 0.5 );\n"
"\n"
"            Src1 = SrcMip.SampleLevel( LinearClampSampler, UV, SrcMipLevel );\n"
"        }\n"
"        break;\n"
"        case WIDTH_ODD_HEIGHT_EVEN:\n"
"        {\n"
"            // > 2:1 in X dimension\n"
"            // Use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x\n"
"            // horizontally.\n"
"            float2 UV1 = TexelSize * ( IN.DispatchThreadID.xy + float2( 0.25, 0.5 ) );\n"
"            float2 Off = TexelSize * float2( 0.5, 0.0 );\n"
"\n"
"            Src1 = 0.5 * ( SrcMip.SampleLevel( LinearClampSampler, UV1, SrcMipLevel ) +\n"
"                           SrcMip.SampleLevel( LinearClampSampler, UV1 + Off, SrcMipLevel ) );\n"
"        }\n"
"        break;\n"
"        case WIDTH_EVEN_HEIGHT_ODD:\n"
"        {\n"
"            // > 2:1 in Y dimension\n"
"            // Use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x\n"
"            // vertically.\n"
"            float2 UV1 = TexelSize * ( IN.DispatchThreadID.xy + float2( 0.5, 0.25 ) );\n"
"            float2 Off = TexelSize * float2( 0.0, 0.5 );\n"
"\n"
"            Src1 = 0.5 * ( SrcMip.SampleLevel( LinearClampSampler, UV1, SrcMipLevel ) +\n"
"                           SrcMip.SampleLevel( LinearClampSampler, UV1 + Off, SrcMipLevel ) );\n"
"        }\n"
"        break;\n"
"        case WIDTH_HEIGHT_ODD:\n"
"        {\n"
"            // > 2:1 in in both dimensions\n"
"            // Use 4 bilinear samples to guarantee we don't undersample when downsizing by more than 2x\n"
"            // in both directions.\n"
"            float2 UV1 = TexelSize * ( IN.DispatchThreadID.xy + float2( 0.25, 0.25 ) );\n"
"            float2 Off = TexelSize * 0.5;\n"
"\n"
"            Src1 =  SrcMip.SampleLevel( LinearClampSampler, UV1, SrcMipLevel );\n"
"            Src1 += SrcMip.SampleLevel( LinearClampSampler, UV1 + float2( Off.x, 0.0   ), SrcMipLevel );\n"
"            Src1 += SrcMip.SampleLevel( LinearClampSampler, UV1 + float2( 0.0,   Off.y ), SrcMipLevel );\n"
"            Src1 += SrcMip.SampleLevel( LinearClampSampler, UV1 + float2( Off.x, Off.y ), SrcMipLevel );\n"
"            Src1 *= 0.25;\n"
"        }\n"
"        break;\n"
"    }\n"
"\n"
"    OutMip1[IN.DispatchThreadID.xy] = Src1;\n"
"\n"
"    // A scalar (constant) branch can exit all threads coherently.\n"
"    if ( NumMipLevels == 1 )\n"
"        return;\n"
"\n"
"    // Without lane swizzle operations, the only way to share data with other\n"
"    // threads is through LDS.\n"
"    StoreColor( IN.GroupIndex, Src1 );\n"
"\n"
"    // This guarantees all LDS writes are complete and that all threads have\n"
"    // executed all instructions so far (and therefore have issued their LDS\n"
"    // write instructions.)\n"
"    GroupMemoryBarrierWithGroupSync();\n"
"\n"
"    // With low three bits for X and high three bits for Y, this bit mask\n"
"    // (binary: 001001) checks that X and Y are even.\n"
"    if ( ( IN.GroupIndex & 0x9 ) == 0 )\n"
"    {\n"
"        float Src2 = LoadColor( IN.GroupIndex + 0x01 );\n"
"        float Src3 = LoadColor( IN.GroupIndex + 0x08 );\n"
"        float Src4 = LoadColor( IN.GroupIndex + 0x09 );\n"
"        Src1 = 0.25 * ( Src1 + Src2 + Src3 + Src4 );\n"
"\n"
"        OutMip2[IN.DispatchThreadID.xy / 2] = Src1;\n"
"        StoreColor( IN.GroupIndex, Src1 );\n"
"    }\n"
"\n"
"    if ( NumMipLevels == 2 )\n"
"        return;\n"
"\n"
"    GroupMemoryBarrierWithGroupSync();\n"
"\n"
"    // This bit mask (binary: 011011) checks that X and Y are multiples of four.\n"
"    if ( ( IN.GroupIndex & 0x1B ) == 0 )\n"
"    {\n"
"        float Src2 = LoadColor( IN.GroupIndex + 0x02 );\n"
"        float Src3 = LoadColor( IN.GroupIndex + 0x10 );\n"
"        float Src4 = LoadColor( IN.GroupIndex + 0x12 );\n"
"        Src1 = 0.25 * ( Src1 + Src2 + Src3 + Src4 );\n"
"\n"
"        OutMip3[IN.DispatchThreadID.xy / 4] = Src1;\n"
"        StoreColor( IN.GroupIndex, Src1 );\n"
"    }\n"
"\n"
"    if ( NumMipLevels == 3 )\n"
"        return;\n"
"\n"
"    GroupMemoryBarrierWithGroupSync();\n"
"\n"
"    // This bit mask would be 111111 (X & Y multiples of 8), but only one\n"
"    // thread fits that criteria.\n"
"    if ( IN.GroupIndex == 0 )\n"
"    {\n"
"        float Src2 = LoadColor( IN.GroupIndex + 0x04 );\n"
"        float Src3 = LoadColor( IN.GroupIndex + 0x20 );\n"
"        float Src4 = LoadColor( IN.GroupIndex + 0x24 );\n"
"        Src1 = 0.25 * ( Src1 + Src2 + Src3 + Src4 );\n"
"\n"
"        OutMip4[IN.DispatchThreadID.xy / 8] = Src1;\n"
"    }\n"
"}\n";
#endif
