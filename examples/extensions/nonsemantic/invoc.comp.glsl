#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_16bit_storage : require
layout(local_size_x_id = 0, local_size_y_id = 1, local_size_z_id = 2) in;

struct Fields
{
    uint a;
    uint b;
    uint c;
    uint d;
    uint e;
    uint f;
    uint g;
    uint h;
    uint i;
    uint j;
    uint k;
};

layout(set = 0, binding = 0, std430) buffer Initial
{
    uint8_t data[];
} initial;

layout(set = 0, binding = 1, std430) buffer Ending
{
    uint8_t values[];
} ending;

layout(push_constant, std140) uniform _39_8
{
    uvec3 indices;
    Fields fields;
} offset;

uvec3 _5 = gl_WorkGroupSize;

u16vec2 pack(u8vec4 a)
{
    return u16vec2(uint(a.x) << 8 | uint(a.y), uint(a.z) << 8 | uint(a.w));
}
uvec2 pack(u16vec4 a)
{
    return uvec2(uint(a.x) << 16 | uint(a.y), uint(a.z) << 16 | uint(a.w));
}
u8vec4 unpack(highp float a)
{
    uint u = floatBitsToUint(a);
    u8vec4 result = u8vec4(0);
    for (uint i = 0; i < 4; i++)
    {
        result[i] = uint8_t((u >> (24 - (i * 8))) & 0xFFu);
    }
    return result;
}

void main()
{
    uint _130 = offset.indices.x + gl_GlobalInvocationID.x + _5.x;
    uint _135 = offset.indices.y + gl_GlobalInvocationID.y + _5.y;
    if ((int(_130) < int(offset.fields.h)) ? (int(_135) < int(offset.fields.g)) : false)
    {
        float _145 = fma(float(_130) + 0.5, uintBitsToFloat(offset.fields.i), -0.5);
        float _148 = fma(float(_135) + 0.5, uintBitsToFloat(offset.fields.j), -0.5);
        uint _150 = uint(floor(_145));
        uint _152 = uint(floor(_148));
        uint _159 = uint(max(int(_150), int(0u)));
        bool _160 = int(_159) < int(offset.fields.d);
        uint _161 = offset.fields.d + 4294967295u;
        float _162 = _160 ? ((int(_150) < int(0u)) ? 0.0 : (_145 - float(_150))) : 0.0;
        uint _163 = _160 ? _159 : _161;
        uint _166 = uint(max(int(_152), int(0u)));
        bool _167 = int(_166) < int(offset.fields.c);
        uint _168 = offset.fields.c + 4294967295u;
        float _169 = _167 ? ((int(_152) < int(0u)) ? 0.0 : (_148 - float(_152))) : 0.0;
        uint _170 = _167 ? _166 : _168;
        float _175 = 1.0 - _162;
        float _176 = 1.0 - _169;
        uint _177 = _163 << 3u;
        uint _178 = _170 * offset.fields.a;
        uint _180 = (_178 + _177) + offset.fields.b;
        u8vec4 _204;
        _204.x = initial.data[_180];
        _204.y = initial.data[_180 + 1u];
        _204.z = initial.data[_180 + 2u];
        _204.w = initial.data[_180 + 3u];
        u8vec4 _208;
        _208.x = initial.data[_180 + 4u];
        _208.y = initial.data[_180 + 5u];
        _208.z = initial.data[_180 + 6u];
        _208.w = initial.data[_180 + 7u];
        u16vec2 _212 = pack(_204);
        u16vec2 _213 = pack(_208);
        uint _216 = uint(min(int(_163 + 1u), int(_161))) << 3u;
        uint _218 = (_178 + _216) + offset.fields.b;
        u8vec4 _242;
        _242.x = initial.data[_218];
        _242.y = initial.data[_218 + 1u];
        _242.z = initial.data[_218 + 2u];
        _242.w = initial.data[_218 + 3u];
        u8vec4 _246;
        _246.x = initial.data[_218 + 4u];
        _246.y = initial.data[_218 + 5u];
        _246.z = initial.data[_218 + 6u];
        _246.w = initial.data[_218 + 7u];
        u16vec2 _250 = pack(_242);
        u16vec2 _251 = pack(_246);
        uint _254 = uint(min(int(_170 + 1u), int(_168))) * offset.fields.a;
        uint _256 = (_254 + _177) + offset.fields.b;
        u8vec4 _280;
        _280.x = initial.data[_256];
        _280.y = initial.data[_256 + 1u];
        _280.z = initial.data[_256 + 2u];
        _280.w = initial.data[_256 + 3u];
        u8vec4 _284;
        _284.x = initial.data[_256 + 4u];
        _284.y = initial.data[_256 + 5u];
        _284.z = initial.data[_256 + 6u];
        _284.w = initial.data[_256 + 7u];
        u16vec2 _288 = pack(_280);
        u16vec2 _289 = pack(_284);
        uint _293 = (_254 + _216) + offset.fields.b;
        u8vec4 _317;
        _317.x = initial.data[_293];
        _317.y = initial.data[_293 + 1u];
        _317.z = initial.data[_293 + 2u];
        _317.w = initial.data[_293 + 3u];
        u8vec4 _321;
        _321.x = initial.data[_293 + 4u];
        _321.y = initial.data[_293 + 5u];
        _321.z = initial.data[_293 + 6u];
        _321.w = initial.data[_293 + 7u];
        u16vec2 _325 = pack(_317);
        u16vec2 _326 = pack(_321);
        float _330 = _175 * _176;
        float _333 = _162 * _176;
        float _338 = _175 * _169;
        float _342 = _162 * _169;
        float _345 = uintBitsToFloat(offset.fields.k);
        vec2 _348 = vec2(_345) * fma(
            vec2(_342),
            pack(u16vec4(_325.x, _325.y, _326.x, _326.y)),
            fma(
                vec2(_338),
                pack(u16vec4(_288.x, _288.y, _289.x, _289.y)),
                fma(
                    vec2(_330),
                    pack(u16vec4(_212.x, _212.y, _213.x, _213.y)),
                    vec2(_333) * pack(u16vec4(_250.x, _250.y, _251.x, _251.y))
                )
            )
        );
        uint _352 = ((_135 * offset.fields.e) + (_130 << 3u)) + offset.fields.f;
        u8vec4 _356 = unpack(_348.x);
        u8vec4 _357 = unpack(_348.y);
        ending.values[_352] = _356.x;
        ending.values[_352 + 1u] = _356.y;
        ending.values[_352 + 2u] = _356.z;
        ending.values[_352 + 3u] = _356.w;
        ending.values[_352 + 4u] = _357.x;
        ending.values[_352 + 5u] = _357.y;
        ending.values[_352 + 6u] = _357.z;
        ending.values[_352 + 7u] = _357.w;
    }
}
