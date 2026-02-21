#version 450

layout(location = 0) in vec4 bounds;
layout(location = 1) in vec4 transform;
layout(location = 2) in vec2 offset;
layout(location = 3) in uint packed;
layout(location = 0) out vec2 refracted;
layout(location = 1) out vec2 oriented;

void main()
{
    vec2 bit_vector = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
    vec2 mixed = mix(bounds.xy, bounds.zw, bit_vector);
    mat2 tmat = mat2(transform.xy, transform.zw);
    vec2 inverse_transform = inverse(tmat) * (mixed - offset);
    vec2 unpacked = unpackSnorm2x16(packed);
    refracted = refract(inverse_transform, unpacked, 0.5);
    oriented = faceforward(offset, inverse_transform, unpacked);
}
