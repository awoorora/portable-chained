// Hello-world compute: fill a buffer on the GPU, read it back, check it on
// the CPU. The build setup is done; the Vulkan part below is yours.
#include <volk.h>          // always include volk.h, never vulkan/vulkan.h
#include <vk_mem_alloc.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream> // so long my dearest cout
#include <ostream>
#include <vector>
#include <cstdlib>
#define VK_CHECK(call)                                                      \
    do {                                                                    \
        VkResult vk_check_result_ = (call);                                 \
        if (vk_check_result_ != VK_SUCCESS) {                               \
            std::fprintf(stderr, "%s:%d: %s returned VkResult %d\n",        \
                         __FILE__, __LINE__, #call, (int)vk_check_result_); \
            std::exit(1);                                                   \
        }                                                                   \
    } while (0)

#include "reduce_spv.h"     // generated from hello.hlsl by DXC at build time

int main(int argc, char** argv)
{
    printf("\n");

    (void)argc; // you will use these to choose the GPU
    (void)argv;

    if (volkInitialize() != VK_SUCCESS)
    {
        std::fprintf(stderr, "No Vulkan loader found.\n");
        return 1;
    }
    const uint32_t v = volkGetInstanceVersion();
    std::printf("Vulkan loader: %u.%u.%u\n",
                VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));

    // vkCreateShaderModule expects 4-byte-aligned 32-bit words, but the
    // generated array is plain bytes, so copy it into a vector of words.
    std::vector<uint32_t> spirv(sizeof(g_reduce_spv) / sizeof(uint32_t));
    std::memcpy(spirv.data(), g_reduce_spv, sizeof(g_reduce_spv));
    std::printf("Embedded SPIR-V: %zu words\n", spirv.size());

    // ---- Your part starts here ----

    VkApplicationInfo appInfo = {};

    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &appInfo;

#ifndef NDEBUG
    const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
    instanceCreateInfo.enabledLayerCount = 1;
    instanceCreateInfo.ppEnabledLayerNames = validationLayers;

    // Synchronization validation, plus tracking of what the shader reads and writes through descriptors
    const VkBool32 settingOn = VK_TRUE;
    VkLayerSettingEXT syncvalSettings[2] = {};
    syncvalSettings[0].pLayerName = "VK_LAYER_KHRONOS_validation";
    syncvalSettings[0].pSettingName = "validate_sync";
    syncvalSettings[0].type = VK_LAYER_SETTING_TYPE_BOOL32_EXT;
    syncvalSettings[0].valueCount = 1;
    syncvalSettings[0].pValues = &settingOn;
    syncvalSettings[1].pLayerName = "VK_LAYER_KHRONOS_validation";
    syncvalSettings[1].pSettingName = "syncval_shader_accesses_heuristic";
    syncvalSettings[1].type = VK_LAYER_SETTING_TYPE_BOOL32_EXT;
    syncvalSettings[1].valueCount = 1;
    syncvalSettings[1].pValues = &settingOn;

    VkLayerSettingsCreateInfoEXT layerSettings = {};
    layerSettings.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
    layerSettings.settingCount = 2;
    layerSettings.pSettings = syncvalSettings;

    const char* instanceExtensions[] = {VK_EXT_LAYER_SETTINGS_EXTENSION_NAME};
    instanceCreateInfo.enabledExtensionCount = 1;
    instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions;
    instanceCreateInfo.pNext = &layerSettings;
#endif

    VkInstance instance = VK_NULL_HANDLE;
    VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));

    volkLoadInstance(instance);

    uint32_t devicesCount = 0;

    VK_CHECK(vkEnumeratePhysicalDevices(instance, &devicesCount, nullptr));

    if (devicesCount == 0)
    {
        fprintf(stderr, "\nNo Vulkan devices found.\n");
        return 1;
    }

    printf("Physical Devices: %u\n", devicesCount);

    std::vector<VkPhysicalDevice> physicalDevices(devicesCount);

    VK_CHECK(vkEnumeratePhysicalDevices(instance, &devicesCount, physicalDevices.data()));

    for (uint32_t i = 0; i < devicesCount; i++)
    {
        printf("\n");
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevices[i], &props);
        printf("GPU %u: ", i);
        printf("%s (", props.deviceName);
        switch (props.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            printf("integrated");
            break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            printf("discrete");
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            printf("virtual");
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            printf("cpu");
            break;
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
            printf("other");
            break;
        default:
            printf("some other");
            break;
        }
        printf(", vendor 0x%x, Vulkan ", props.vendorID);
        uint32_t packedVersion = props.apiVersion;
        uint32_t major = VK_API_VERSION_MAJOR(packedVersion);
        uint32_t minor = VK_API_VERSION_MINOR(packedVersion);
        uint32_t patch = VK_API_VERSION_PATCH(packedVersion);
        printf("%u.%u.%u)", major, minor, patch);
    }

    printf("\n");

    if (argc < 2)
    {
        fprintf(stderr, "Give me a GPU index in arguments\n");
        return 1;
    }

    char* p;
    uint32_t physDevChosen = strtol(argv[1], &p, 10);
    if (p == argv[1] || *p != '\0' || physDevChosen >= devicesCount)
    {
        fprintf(stderr, "In the arguments choose a GPU index between 0 and %u.\n", devicesCount - 1);
        return 1;
    }

    printf("\nGPU id chosen: %u\n", physDevChosen);

    uint32_t queueFamilyPropertyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[physDevChosen], &queueFamilyPropertyCount, nullptr);
    // populate queueFamilyCount per device aka per GPU

    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyPropertyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[physDevChosen], &queueFamilyPropertyCount,
                                             queueFamilyProperties.data());

    VkDeviceQueueCreateInfo queueCreateInfo = {};
    const float pQueuePrioritiesNumber = 1.0f;
    uint32_t computeFamily = UINT32_MAX;

    for (uint32_t queueFamPropIndex = 0; queueFamPropIndex < queueFamilyProperties.size(); queueFamPropIndex++)
    {
        if ((queueFamilyProperties[queueFamPropIndex].queueFlags & VK_QUEUE_COMPUTE_BIT) == VK_QUEUE_COMPUTE_BIT)
        {
            printf("Found a queue family capable of computing in gpu %u and at index %u\n", physDevChosen,
                   queueFamPropIndex);
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamPropIndex;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &pQueuePrioritiesNumber;
            computeFamily = queueFamPropIndex;
            break;
        }
    }

    if (computeFamily == UINT32_MAX)
    {
        fprintf(stderr, "No queue family capable of computing was found\n");
        return 1;
    }

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    VkDevice device = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(physicalDevices[physDevChosen], &deviceCreateInfo, nullptr, &device));
    volkLoadDevice(device);

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, computeFamily, 0, &queue);

    // now what do I do with this queue

    printf("Got the queue and made the device\n");

    // buffer, pipeline, running it

    // allocation parameters
    VmaVulkanFunctions vulkanFunctions = {};

    VmaAllocatorCreateInfo allocatorCreateInfo = {};
    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorCreateInfo.physicalDevice = physicalDevices[physDevChosen];
    allocatorCreateInfo.device = device;
    allocatorCreateInfo.instance = instance;
    VK_CHECK(vmaImportVulkanFunctionsFromVolk(&allocatorCreateInfo, &vulkanFunctions));
    allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
    VmaAllocator allocator;
    VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &allocator));

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationCreateInfo allocCreateInfoDevice = {};
    allocCreateInfoDevice.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VmaAllocationCreateInfo allocCreateInfoStaging = {};
    allocCreateInfoStaging.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfoStaging.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    // buffer definitions
    constexpr uint32_t N = (1u << 22) + 13;

    VkBufferCreateInfo bufferInfoIn = {};
    bufferInfoIn.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfoIn.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfoIn.size = sizeof(uint32_t) * N;

    VkBufferCreateInfo bufferInfoRes = {};
    bufferInfoRes.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfoRes.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfoRes.size = sizeof(uint32_t);

    VkBufferCreateInfo bufferInfoStaging = {};
    bufferInfoStaging.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfoStaging.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfoStaging.size = sizeof(uint32_t) * N;

    VkBuffer bufferIn, bufferRes, bufferStaging;
    VmaAllocation allocationIn, allocationRes, allocationStaging;
    VmaAllocationInfo allocationInfoIn, allocationInfoRes, allocationInfoStaging; // these hold the pointers to mapped memory

    // creating the buffers and allocating memory in one shot
    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfoIn, &allocCreateInfoDevice, &bufferIn, &allocationIn, &allocationInfoIn));
    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfoRes, &allocCreateInfo, &bufferRes, &allocationRes, &allocationInfoRes));
    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfoStaging, &allocCreateInfoStaging, &bufferStaging, &allocationStaging, &allocationInfoStaging));

    // accessing the data

    void* cpuDataIn = allocationInfoStaging.pMappedData; // previously was allocationInfoIn
    uint32_t* dataIn = static_cast<uint32_t*>(cpuDataIn);
    void* cpuDataRes = allocationInfoRes.pMappedData;
    uint32_t* dataRes = static_cast<uint32_t*>(cpuDataRes);
    dataRes[0] = 0;
    uint32_t dataExpected = 0;

    for (uint32_t i = 0; i < N; i++)
    {
        dataIn[i] = i; // 1 or i or whatever
        dataExpected += dataIn[i];
    }

    // Flush makes CPU writes visible to the GPU
    VK_CHECK(vmaFlushAllocation(allocator, allocationStaging, 0, VK_WHOLE_SIZE));
    VK_CHECK(vmaFlushAllocation(allocator, allocationRes, 0, VK_WHOLE_SIZE));

    printf("dataIn[0]: 0x%x\n", dataIn[0]);
    printf("dataRes[0]: 0x%x\n", dataRes[0]);

    // shader, pipeline, descriptors

    // Every Vulkan object is made following a pattern: fill a ...CreateInfo struct,
    // call vkCreate..., and Vulkan hands back a handle. I never fill a
    // handle in myself.

    // also check hello.hlsl

    // Shader module: the compiled shader code, handed to the driver.
    VkShaderModuleCreateInfo shaderModuleCreateInfo = {};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = spirv.size() * sizeof(uint32_t); // BYTES, not words
    shaderModuleCreateInfo.pCode = spirv.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule));
    printf("Shader module created (%zu bytes)\n", shaderModuleCreateInfo.codeSize);

    // Descriptor set layout: the plug's shape.
    // "Binding 0 is one storage buffer, used by a compute shader."
    std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBinding = {
        // Binding 0
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        // Binding 1
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        }
    };

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.bindingCount = 2;
    descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding[0];

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &setLayout));

    // Pipeline layout: which set layouts the pipeline uses.
    // Position in pSetLayouts = set number, so this one is set 0,
    // matching register(u0) in hello.hlsl.
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &setLayout;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

    // Pipeline: the shader compiled against that layout.
    // Validation checks the shader's declarations against the layout here.
    VkPipelineShaderStageCreateInfo computeShaderStageInfo = {};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = shaderModule;
    computeShaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stage = computeShaderStageInfo; // what code to run
    pipelineCreateInfo.layout = pipelineLayout; // what data inputs the code expects

    // The function can create several pipelines at once into an array;
    // with a count of 1, the address of one handle is an array of one.
    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE /* no pipeline cache */, 1,
        &pipelineCreateInfo, nullptr, &pipeline));
    printf("Compute pipeline created\n");

    // The pipeline has its own copy of the compiled code now.
    vkDestroyShaderModule(device, shaderModule, nullptr);

    // Descriptor pool: reserves room for descriptor sets up front.
    // We need one set holding one storage-buffer descriptor.
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 2;

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.maxSets = 1;
    descriptorPoolCreateInfo.poolSizeCount = 1;
    descriptorPoolCreateInfo.pPoolSizes = &poolSize;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

    // Descriptor set: the actual plug, shaped by setLayout. Empty for now.
    VkDescriptorSetAllocateInfo setAllocateInfo = {};
    setAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocateInfo.descriptorPool = descriptorPool;
    setAllocateInfo.descriptorSetCount = 1;
    setAllocateInfo.pSetLayouts = &setLayout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(device, &setAllocateInfo, &descriptorSet));

    // Fill it: "binding 0 is THIS buffer, all of it."
    std::vector<VkDescriptorBufferInfo> descriptorBufferInfo =
    {
        {
            .buffer = bufferIn,
            .offset = 0,
            .range = VK_WHOLE_SIZE
        },
        {
            .buffer = bufferRes,
            .offset = 0,
            .range = VK_WHOLE_SIZE
        }
    };

    std::vector<VkWriteDescriptorSet> descriptorWrite =
    {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &descriptorBufferInfo[0]
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &descriptorBufferInfo[1]
        }
    };

    // VkBuffer bufferResDev; VmaAllocation allocationResDev;
    // VK_CHECK(vmaCreateBuffer(allocator, &bufferInfoRes, &allocCreateInfoDevice,
    //                          &bufferResDev, &allocationResDev, nullptr));
    // descriptorBufferInfo[1].buffer = bufferResDev;

    vkUpdateDescriptorSets(device, 2, &descriptorWrite[0], 0, nullptr); // 1 write, 0 copies
    printf("Descriptor set points at the buffer\n");

    // Record, submit, wait, check

    VkCommandPoolCreateInfo commandPoolCreateInfo = {};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.queueFamilyIndex = computeFamily;
    commandPoolCreateInfo.flags = 0; // each command buffer is recorded exactly once: no reset flag needed

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool));

    // Two command buffers from the same pool:
    // [0] the upload, submitted once; [1] the timed dispatch, resubmitted every run.
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = commandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 2;

    VkCommandBuffer commandBuffers[2] = {};
    VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers));
    VkCommandBuffer uploadCommandBuffer = commandBuffers[0];
    VkCommandBuffer commandBuffer = commandBuffers[1]; // the timed one

    // Created unsignaled. The upload uses it first, then every run resets and reuses it.
    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = VK_NULL_HANDLE;
    VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));

    // Upload: staging -> bufferIn; once

    VkCommandBufferBeginInfo uploadBeginInfo = {};
    uploadBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    uploadBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // submitted once, never again

    VK_CHECK(vkBeginCommandBuffer(uploadCommandBuffer, &uploadBeginInfo));
    VkBufferCopy region = {0, 0, sizeof(uint32_t) * N};
    vkCmdCopyBuffer(uploadCommandBuffer, bufferStaging, bufferIn, 1, &region);
    VK_CHECK(vkEndCommandBuffer(uploadCommandBuffer));

    VkSubmitInfo uploadSubmitInfo = {};
    uploadSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    uploadSubmitInfo.commandBufferCount = 1;
    uploadSubmitInfo.pCommandBuffers = &uploadCommandBuffer;

    // Two different jobs, two different mechanisms:
    // - This host wait is for the MEASUREMENT: the upload is finished before the
    //   first timed submit, so nothing earlier is still running when ts[0] is taken.
    // - The barrier at the top of the timed command buffer is for CORRECTNESS:
    //   it orders the copy's writes before the shader's reads on the GPU.
    VK_CHECK(vkQueueSubmit(queue, 1, &uploadSubmitInfo, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000));
    printf("Input uploaded\n");

    // Timed command buffer: only the dispatch between the timestamps

    VkCommandBufferBeginInfo commandBufferBeginInfo = {};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevices[physDevChosen], &props);
    float timestampPeriod = props.limits.timestampPeriod; // thank you auto for showing me its type :]
    printf("timestampLimit = %f\n", timestampPeriod);
    uint32_t timestampValidBits = queueFamilyProperties[computeFamily].timestampValidBits;
    if (timestampValidBits == 0)
    {
        fprintf(stderr, "No valid timestamp bits found.\n");
        return 1;
    }
    printf("timestampValidBits = %u\n", timestampValidBits);

    VkQueryPoolCreateInfo queryPoolCreateInfo = {};
    queryPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolCreateInfo.queryCount = 2;

    VkQueryPool queryPool = VK_NULL_HANDLE;

    VK_CHECK(vkCreateQueryPool(device, &queryPoolCreateInfo, nullptr, &queryPool));

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

    vkCmdResetQueryPool(commandBuffer, queryPool, 0, 2);

    // The copy is in another submission now, but submission order runs across
    // command buffers and vkQueueSubmit calls on the same queue. So this
    // barrier's first scope ("earlier in submission order") still reaches back
    // to the upload's copy. On runs 1..9 there is nothing left for it to wait on.
    VkMemoryBarrier copyToShader = {};
    copyToShader.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    copyToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    copyToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &copyToShader, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0,
                            nullptr);

    // Every earlier submission has finished before this one is submitted (the host
    // waited on its fence), so TOP_OF_PIPE has nothing to run ahead of.
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 0);
    vkCmdDispatch(commandBuffer, (N + 255) / 256, 1, 1);
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);


    VkMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &memoryBarrier, 0, nullptr, 0, nullptr); // whole lotta parameters

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    const uint64_t mask = (timestampValidBits == 64) ? ~0ull : ((1ull << timestampValidBits) - 1);
    constexpr int runs = 10;
    bool ok = true; // no longer const: it collects the result of every run

    printf("Expected: %u\n", dataExpected);

    VkMemoryPropertyFlags resFlags = 0;
    vmaGetAllocationMemoryProperties(allocator, allocationRes, &resFlags);
    printf("bufferRes device-local: %s\n", (resFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? "yes" : "no");

    for (int run = 0; run < runs; run++)
    {
        // Clear the result and make that CPU write visible to the GPU
        dataRes[0] = 0;
        VK_CHECK(vmaFlushAllocation(allocator, allocationRes, 0, VK_WHOLE_SIZE));

        // Still signaled from the upload on run 0, from the previous run after that
        VK_CHECK(vkResetFences(device, 1, &fence));

        // Same command buffer every time
        VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));
        VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000)); // nanoseconds xd

        // Make the GPU's write visible to the CPU, then check
        VK_CHECK(vmaInvalidateAllocation(allocator, allocationRes, 0, sizeof(uint32_t)));
        const bool runOk = (dataExpected == dataRes[0]);
        ok = ok && runOk;

        // Timestamps
        uint64_t ts[2] = {};
        VK_CHECK(vkGetQueryPoolResults(device, queryPool, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        const double ns = double((ts[1] - ts[0]) & mask) * timestampPeriod;
        const double gbps = double(N * sizeof(uint32_t)) / ns; // bytes per ns = GB/s
        printf("Run %2d: %.3f ms, %.1f GB/s | Got: %u %s\n",
               run, ns / 1e6, gbps, dataRes[0], runOk ? "" : "<- MISMATCH");
    }

    if (!ok)
    {
        printf("\ndog\n");
    }

    // Cleanup: reverse order of creation

    vkDestroyFence(device, fence, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr); // frees both command buffers

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);

    vkDestroyPipeline(device, pipeline, nullptr);

    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(device, setLayout, nullptr);

    //vmaDestroyBuffer(allocator, bufferResDev, allocationResDev);

    vmaDestroyBuffer(allocator, bufferIn, allocationIn);

    vmaDestroyBuffer(allocator, bufferRes, allocationRes);

    vmaDestroyBuffer(allocator, bufferStaging, allocationStaging);

    vmaDestroyAllocator(allocator);

    vkDestroyQueryPool(device, queryPool, nullptr);

    vkDestroyDevice(device, nullptr);

    vkDestroyInstance(instance, nullptr);

    return ok ? 0 : 1; // gonna have *so* much fun when I forget this ok check is here
}