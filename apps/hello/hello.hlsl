// Compiled to SPIR-V by DXC at build time (see target_hlsl_shader in the top
// CMakeLists.txt). To read the SPIR-V it produced, open
// build/<preset>/apps/hello/shaders/hello_spv.h after a build.

RWStructuredBuffer<uint> buf : register(u0); // syntax [[vk::binding(binding_slot, set_slot)]]


[numthreads(64, 1, 1)]

void main(uint3 id : SV_DispatchThreadID)
{
    // Your part: declare a buffer and write to it.
    uint count, stride;
    buf.GetDimensions(count, stride); // count = number of elements

    if(id.x < count)
    {
        buf[id.x] = id.x * 2 + 1;
    }
}
