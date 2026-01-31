#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, std140) uniform type_ConstantBuffer_ConstantsStruct
{
    float FFTThreshold;
} Constants;

layout(set = 1, binding = 0, rg32f) uniform writeonly image2D TargetTextureR;
layout(set = 1, binding = 1, rg32f) uniform writeonly image2D TargetTextureG;
layout(set = 1, binding = 2, rg32f) uniform writeonly image2D TargetTextureB;
layout(set = 1, binding = 3) uniform texture2D SourceTextureRGB;

void main()
{
    uvec2 _39 = uvec2(textureSize(SourceTextureRGB, 0));
    uvec2 _41 = uvec2(0u);
    _41.x = _39.x;
    _41.y = _39.y;
    vec3 _54;
    if (all(lessThan(gl_GlobalInvocationID.xy, _41)))
    {
        _54 = max(
            texelFetch(SourceTextureRGB, ivec2(gl_GlobalInvocationID.xy), int(0u)).xyz - vec3(Constants.FFTThreshold),
            vec3(0.0)
        );
    }
    else
    {
        _54 = vec3(0.0);
    }
    imageStore(TargetTextureR, ivec2(gl_GlobalInvocationID.xy), vec2(_54.x, 0.0).xyyy);
    imageStore(TargetTextureG, ivec2(gl_GlobalInvocationID.xy), vec2(_54.y, 0.0).xyyy);
    imageStore(TargetTextureB, ivec2(gl_GlobalInvocationID.xy), vec2(_54.z, 0.0).xyyy);
}

