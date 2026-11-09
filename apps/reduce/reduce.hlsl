// reduce.hlsl: adds up every element of `input` into result[0].
RWStructuredBuffer<uint> input  : register(u0);
RWStructuredBuffer<uint> result : register(u1);

groupshared uint g_sum; // one shared value per workgroup

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 lid : SV_GroupThreadID)
{
    // Setup: one thread clears the shared sum, everyone waits for it.
    if (lid.x == 0)
        g_sum = 0;
    GroupMemoryBarrierWithGroupSync();

    // Level 1 (thread): my element, or 0 if I'm past the end.
    uint count, stride;
    input.GetDimensions(count, stride);
    /* TODO 1*/
    uint value = (id.x < count) ? input[id.x] : 0;

    // Level 2 (subgroup): add up the subgroup's values, then one lane
    // per subgroup adds that total into g_sum.
    /* TODO 2 */

    uint subgroupTotal = WaveActiveSum(value);
    if(WaveIsFirstLane()){
        InterlockedAdd(g_sum, subgroupTotal);
    }

    // Wait until every subgroup has added its total.
    GroupMemoryBarrierWithGroupSync();

    // Level 3 (workgroup): one thread adds g_sum into result[0].
    /* TODO 3 */
    if (lid.x == 0){
        InterlockedAdd(result[0], g_sum);
    }

}