// Compiled to SPIR-V by DXC at build time (see target_hlsl_shader in the top
// CMakeLists.txt). To read the SPIR-V it produced, open
// build/<preset>/apps/hello/shaders/hello_spv.h after a build.

// Resources are declared at file scope, never inside a function: they are
// plugged in from outside the shader. DXC maps register(u0) to set 0,
// binding 0, which is what the descriptor set layout in main.cpp describes.
// (The explicit alternative would be [[vk::binding(0, 0)]].)
RWStructuredBuffer<uint> buf : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint count, stride;
    buf.GetDimensions(count, stride); // count = number of elements, stride = bytes per element

    // Threads run in groups of 64, so covering 1000 elements takes 16 groups,
    // which is 1024 threads. The last 24 must not write past the end.
    if (id.x < count)
    {
        buf[id.x] = id.x * 2 + 1;
    }
}
