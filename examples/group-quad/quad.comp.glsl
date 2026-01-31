#version 450
#extension GL_EXT_shader_16bit_storage : require
#extension GL_KHR_shader_subgroup_quad : require
#extension GL_EXT_shader_subgroup_extended_types_float16 : require
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(push_constant, std430) uniform _5_25
{
    vec2 _m0;
    uint _m1;
} _25;

layout(set = 0, binding = 1) uniform sampler2D _6;
layout(set = 0, binding = 0, rgba16) uniform coherent writeonly image2D _7[6];

shared f16vec4 _41[16][16];

void main()
{
    do
    {
        uint _94;
        uint _96;
        uint _74 = gl_LocalInvocationIndex % 64u;
        int _75 = int(2u);
        int _76 = int(3u);
        int _78 = int(1u);
        uint _86 = bitfieldInsert(bitfieldExtract(_74, _75, _76), _74, 0, _78) + (8u * ((gl_LocalInvocationIndex >> uint(6)) % 2u));
        uint _89 = bitfieldInsert(bitfieldExtract(_74, _76, _76), bitfieldExtract(_74, _78, _75), 0, _75) + (8u * (gl_LocalInvocationIndex >> uint(7)));
        do
        {
            ivec2 _93 = ivec2(gl_WorkGroupID.xy * uvec2(64u));
            _94 = _86 * 2u;
            int _95 = int(_94);
            _96 = _89 * 2u;
            int _97 = int(_96);
            ivec2 _101 = ivec2(gl_WorkGroupID.xy * uvec2(32u));
            int _102 = int(_86);
            int _103 = int(_89);
            f16vec4 _115 = f16vec4(textureLod(_6, (vec2(ivec2(uvec2(_93 + ivec2(_95, _97)))) * _25._m0) + _25._m0, 0.0));
            imageStore(_7[0u], _101 + ivec2(_102, _103), vec4(_115));
            int _120 = int(_94 + 32u);
            int _124 = int(_86 + 16u);
            f16vec4 _133 = f16vec4(textureLod(_6, (vec2(ivec2(uvec2(_93 + ivec2(_120, _97)))) * _25._m0) + _25._m0, 0.0));
            imageStore(_7[0u], _101 + ivec2(_124, _103), vec4(_133));
            int _137 = int(_96 + 32u);
            int _141 = int(_89 + 16u);
            f16vec4 _150 = f16vec4(textureLod(_6, (vec2(ivec2(uvec2(_93 + ivec2(_95, _137)))) * _25._m0) + _25._m0, 0.0));
            imageStore(_7[0u], _101 + ivec2(_102, _141), vec4(_150));
            f16vec4 _163 = f16vec4(textureLod(_6, (vec2(ivec2(uvec2(_93 + ivec2(_120, _137)))) * _25._m0) + _25._m0, 0.0));
            imageStore(_7[0u], _101 + ivec2(_124, _141), vec4(_163));
            if (_25._m1 <= 1u)
            {
                break;
            }
            f16vec4 _169 = subgroupQuadSwapHorizontal(_115);
            f16vec4 _170 = subgroupQuadSwapVertical(_115);
            f16vec4 _171 = subgroupQuadSwapDiagonal(_115);
            f16vec4 _175 = (((_115 + _169) + _170) + _171) * float16_t(0.25);
            f16vec4 _176 = subgroupQuadSwapHorizontal(_133);
            f16vec4 _177 = subgroupQuadSwapVertical(_133);
            f16vec4 _178 = subgroupQuadSwapDiagonal(_133);
            f16vec4 _182 = (((_133 + _176) + _177) + _178) * float16_t(0.25);
            f16vec4 _183 = subgroupQuadSwapHorizontal(_150);
            f16vec4 _184 = subgroupQuadSwapVertical(_150);
            f16vec4 _185 = subgroupQuadSwapDiagonal(_150);
            f16vec4 _189 = (((_150 + _183) + _184) + _185) * float16_t(0.25);
            f16vec4 _190 = subgroupQuadSwapHorizontal(_163);
            f16vec4 _191 = subgroupQuadSwapVertical(_163);
            f16vec4 _192 = subgroupQuadSwapDiagonal(_163);
            f16vec4 _196 = (((_163 + _190) + _191) + _192) * float16_t(0.25);
            if ((gl_LocalInvocationIndex % 4u) == 0u)
            {
                ivec2 _202 = ivec2(gl_WorkGroupID.xy * uvec2(16u));
                uint _203 = _86 / 2u;
                int _204 = int(_203);
                uint _205 = _89 / 2u;
                int _206 = int(_205);
                imageStore(_7[1u], _202 + ivec2(_204, _206), vec4(_175));
                _41[_203][_205] = _175;
                uint _213 = _203 + 8u;
                int _214 = int(_213);
                imageStore(_7[1u], _202 + ivec2(_214, _206), vec4(_182));
                _41[_213][_205] = _182;
                uint _220 = _205 + 8u;
                int _221 = int(_220);
                imageStore(_7[1u], _202 + ivec2(_204, _221), vec4(_189));
                _41[_203][_220] = _189;
                imageStore(_7[1u], _202 + ivec2(_214, _221), vec4(_196));
                _41[_213][_220] = _196;
            }
            break;
        } while(false);
        do
        {
            if (_25._m1 <= 2u)
            {
                break;
            }
            barrier();
            f16vec4 _239 = subgroupQuadSwapHorizontal(_41[_86][_89]);
            f16vec4 _240 = subgroupQuadSwapVertical(_41[_86][_89]);
            f16vec4 _241 = subgroupQuadSwapDiagonal(_41[_86][_89]);
            f16vec4 _245 = (((_41[_86][_89] + _239) + _240) + _241) * float16_t(0.25);
            bool _247 = (gl_LocalInvocationIndex % 4u) == 0u;
            if (_247)
            {
                uint _254 = _89 / 2u;
                imageStore(_7[2u], ivec2(gl_WorkGroupID.xy * uvec2(8u)) + ivec2(int(_86 / 2u), int(_254)), vec4(_245));
                _41[_86 + (_254 % 2u)][_89] = _245;
            }
            if (_25._m1 <= 3u)
            {
                break;
            }
            barrier();
            if (gl_LocalInvocationIndex < 64u)
            {
                uint _271 = _94 + (_89 % 2u);
                f16vec4 _274 = subgroupQuadSwapHorizontal(_41[_271][_96]);
                f16vec4 _275 = subgroupQuadSwapVertical(_41[_271][_96]);
                f16vec4 _276 = subgroupQuadSwapDiagonal(_41[_271][_96]);
                f16vec4 _280 = (((_41[_271][_96] + _274) + _275) + _276) * float16_t(0.25);
                if (_247)
                {
                    uint _287 = _89 / 2u;
                    imageStore(_7[3u], ivec2(gl_WorkGroupID.xy * uvec2(4u)) + ivec2(int(_86 / 2u), int(_287)), vec4(_280));
                    _41[_94 + _287][_96] = _280;
                }
            }
            if (_25._m1 <= 4u)
            {
                break;
            }
            barrier();
            if (gl_LocalInvocationIndex < 16u)
            {
                uint _303 = (_86 * 4u) + _89;
                uint _304 = _89 * 4u;
                f16vec4 _307 = subgroupQuadSwapHorizontal(_41[_303][_304]);
                f16vec4 _308 = subgroupQuadSwapVertical(_41[_303][_304]);
                f16vec4 _309 = subgroupQuadSwapDiagonal(_41[_303][_304]);
                f16vec4 _313 = (((_41[_303][_304] + _307) + _308) + _309) * float16_t(0.25);
                if (_247)
                {
                    uint _318 = _86 / 2u;
                    imageStore(_7[4u], ivec2(gl_WorkGroupID.xy * uvec2(2u)) + ivec2(int(_318), int(_89 / 2u)), vec4(_313));
                    _41[_318 + _89][0u] = _313;
                }
            }
            if (_25._m1 <= 5u)
            {
                break;
            }
            barrier();
            if (gl_LocalInvocationIndex < 4u)
            {
                f16vec4 _337 = subgroupQuadSwapHorizontal(_41[gl_LocalInvocationIndex][0u]);
                f16vec4 _338 = subgroupQuadSwapVertical(_41[gl_LocalInvocationIndex][0u]);
                f16vec4 _339 = subgroupQuadSwapDiagonal(_41[gl_LocalInvocationIndex][0u]);
                if (_247)
                {
                    imageStore(_7[5u], ivec2(gl_WorkGroupID.xy), vec4((((_41[gl_LocalInvocationIndex][0u] + _337) + _338) + _339) * float16_t(0.25)));
                }
            }
            break;
        } while(false);
        if (_25._m1 < 7u)
        {
            break;
        }
        barrier();
        break;
    } while(false);
}
