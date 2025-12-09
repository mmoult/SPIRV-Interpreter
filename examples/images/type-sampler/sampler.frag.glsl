#version 440
layout(set = 0, binding = 0) uniform sampler texSampler;
layout(set = 0, binding = 1) uniform texture1D texImage;
layout(location = 0) in vec4 vtxTexCoords;
layout(location = 0) out vec4 fragColor;
void main (void)
{
    fragColor = texture(sampler1D(texImage, texSampler), vtxTexCoords.x);
}
