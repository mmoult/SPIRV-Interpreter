Source has been adapted from shaders generated for dEQP-VK.ray_tracing_pipeline.barycentric_coordinates.chit from
https://github.com/KhronosGroup/VK-GL-CTS, which is licensed under Apache 2.0.

This example is interesting because substages can *modify* variables on the interface and those changes will be
preserved after exit. Thus, we cannot merely copy values to substages, it must be a deeper connection.

These variables on the interface are matched by location, not by name. Names may match, but are not guaranteed to.

The `clean` variant removed unused variables from each substage. This is particularly interesting because
`layout(set = 0, binding = 2, std430)` must appear on the rgen's interface even though it isn't explicitly mentioned.
