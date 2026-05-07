#version 450 core
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_AMD_gpu_shader_half_float : require
#extension GL_AMD_gpu_shader_int16 : require

layout(location = 0) out int[2] bigint;
layout(location = 2) out float16_t smallfp;
layout(location = 3) out int16_t smallint;
layout(location = 4) out uint[2] bigfp;

void main() {
    int64_t a = 0x1234567890ABCDEFL; //  Clearly over Int32 max. = 1311768467294899695
    int16_t small = int16_t(42);
    a += small; // test how integers of different precisions interact
    // a = 1311768467294899737 = 0x1234'5678'90AB'CE19
    // decompose the value for storage
    bigint[0] = int(a);         // lower 32 bits: 0x90AB'CE19 = 2427178521
    bigint[1] = int(a >> 32);   // upper 32 bits: 0x1234'5678 = 305419896
    smallfp = float16_t(1.5);
    smallint = small;
    double b = 1.1234567890123452; // Clearly over Float32 precision.
    // 0x3FF1'F9AD'D374'6F64 = 4607738418749009764
    b += smallfp;
    // 2.6234567890123452 = 4613089918308726706 = 0x4004'FCD6'E9BA'37B2
    uint64_t c = doubleBitsToUint64(b);
    // doubles, int64, uint64 cannot be on the interface
    bigfp[0] = uint(c);         // lower 32 bits: 0xE9BA'37B2 = 3921295282
    bigfp[1] = uint(c >> 32);   // upper 32 bits: 0x4004'FCD6 = 1074068694
}
