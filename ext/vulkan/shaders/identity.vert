#version 450 core

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 outTexCoord;

void main() {
   gl_Position = inPos;
   outTexCoord = inTexCoord;
}

