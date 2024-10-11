#version 100
#extension GL_OES_EGL_image_external : require
precision highp float;
precision highp int;

struct buf
{
    mat4 qt_Matrix;
    ivec4 swizzle;
    mat4 color_matrix;
    float qt_Opacity;
};

uniform buf ubuf;

uniform samplerExternalOES tex;

varying vec2 vTexCoord;

vec4 swizzle(vec4 texel, ivec4 swizzle_1)
{
    return vec4(texel[swizzle_1.x], texel[swizzle_1.y], texel[swizzle_1.z], texel[swizzle_1.w]);
}

void main()
{
    vec4 param = texture2D(tex, vTexCoord);
    ivec4 param_1 = ubuf.swizzle;
    vec4 texel = swizzle(param, param_1);
    gl_FragData[0] = texel * ubuf.qt_Opacity;
}
