#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

struct Object {
    int a;
    int b;
    uint c;
    uint d;
};

layout(set = 1, binding = 2, std140) buffer Test
{
    Object obj[2];
};

void main()
{
    obj[1].a = atomicAdd(obj[0].a, 1);
    obj[1].b = atomicMax(obj[0].b, 10);
    obj[1].c = atomicExchange(obj[0].c, 3);
    obj[1].d = atomicXor(obj[0].d, 0x7453);
}
