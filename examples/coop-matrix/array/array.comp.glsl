#version 450
#if defined(GL_AMD_gpu_shader_half_float)
#extension GL_AMD_gpu_shader_half_float : require
#elif defined(GL_EXT_shader_explicit_arithmetic_types_float16)
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#else
#error No extension available for FP16.
#endif
#extension GL_EXT_shader_16bit_storage : require
#extension GL_KHR_cooperative_matrix : require
#extension GL_KHR_shader_subgroup_basic : require
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) buffer BufI { float16_t val[]; } bufi;
layout(set = 0, binding = 3, std430) buffer BufO { float16_t val[]; } bufo;

void main()
{
    const uint STRIDE = 16;
    coopmat<float16_t, 3u, 16, 16, gl_MatrixUseA> loadTmp;
    coopMatLoad(loadTmp, bufi.val, 0, STRIDE, 0);

    coopmat<float16_t, 3u, 16, 16, gl_MatrixUseA> matIArr[2];
    matIArr[1] = loadTmp;

    coopmat<float16_t, 3u, 16, 16, gl_MatrixUseA> matOArr[2];
    for (int i = 0; i < loadTmp.length(); i++)
    {
        matOArr[1][i] = matIArr[1][i];
    }
    coopmat<float16_t, 3u, 16, 16, gl_MatrixUseA> storeTmp;
    storeTmp = matOArr[1];
    coopMatStore(storeTmp, bufo.val, 0, STRIDE, 0);
}
