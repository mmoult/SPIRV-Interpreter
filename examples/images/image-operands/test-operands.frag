#version 310 es
#extension GL_EXT_gpu_shader5 : require
precision highp float;

layout(location = 0) in highp vec2 v_texCoord;

layout(binding = 0) uniform highp sampler2D u_sampler;
layout(binding = 1) uniform offset { highp ivec2 u_offset; };

layout(location = 0) out mediump vec4 o_color;

void main()
{
    vec4 offset = textureGatherOffset(u_sampler, v_texCoord, u_offset, 2);
    vec4 const_offsets = textureGatherOffsets(u_sampler, v_texCoord, ivec2[4](
        ivec2(-1, -1),
        ivec2(5, 6),
        ivec2(6, -10),
        ivec2(17, 1)
    ), 1);

    o_color = offset + const_offsets;
}
