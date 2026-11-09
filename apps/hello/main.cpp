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

#include "hello_spv.h"     // generated from hello.hlsl by DXC at build time

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
    std::vector<uint32_t> spirv(sizeof(g_hello_spv) / sizeof(uint32_t));
    std::memcpy(spirv.data(), g_hello_spv, sizeof(g_hello_spv));
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
            printf("Found a queue family capable of computing in gpu %u and at index %u\n", physDevChosen, queueFamPropIndex);
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

    // buffer definition
    constexpr uint32_t N = 1000;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.size = sizeof(uint32_t) * N;

    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocationInfo; // holds the pointer to mapped memory

    // creating the buffer and allocating memory in one shot
    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, &buffer, &allocation, &allocationInfo));

    // accessing the data

    void* myCpuData = allocationInfo.pMappedData;
    uint32_t* data = static_cast<uint32_t*>(myCpuData);

    for (uint32_t i = 0; i < N; i++)
    {
        data[i] = 0xDEADBEEF; // ? lol dead beef
    }

    // Flush makes CPU writes visible to the GPU
    VK_CHECK(vmaFlushAllocation(allocator, allocation, 0, sizeof(uint32_t) * N)); // or VK_WHOLE_SIZE ?

    printf("Element 0: 0x%x\n", data[0]);

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
    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {};
    descriptorSetLayoutBinding.binding = 0;
    descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBinding.descriptorCount = 1;
    descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.bindingCount = 1;
    descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;

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
    pipelineCreateInfo.layout = pipelineLayout;        // what data inputs the code expects

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
    poolSize.descriptorCount = 1;

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
    VkDescriptorBufferInfo descriptorBufferInfo = {};
    descriptorBufferInfo.buffer = buffer;
    descriptorBufferInfo.offset = 0;
    descriptorBufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.pBufferInfo = &descriptorBufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr); // 1 write, 0 copies
    printf("Descriptor set points at the buffer\n");

    // Record, submit, wait, check

    VkCommandPoolCreateInfo commandPoolCreateInfo = {};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.queueFamilyIndex = computeFamily;
    commandPoolCreateInfo.flags = 0; // recorded once, resubmitted every run: no reset flag needed

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool));

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = commandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer));

    VkCommandBufferBeginInfo commandBufferBeginInfo = {};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    vkCmdDispatch(commandBuffer, (N + 64 - 1) / 64, 1, 1);

    VkMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); // whole lotta parameters

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = VK_NULL_HANDLE;
    VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000)); // nanoseconds xd

    // Invalidate makes GPU writes visible to the CPU
    VK_CHECK(vmaInvalidateAllocation(allocator, allocation, 0, sizeof(uint32_t) * N));

    printf("\nShowing first 4 indexes: \n");

    bool ok = true;
    uint32_t firstFailingIndex = UINT32_MAX;
    uint32_t secondFailingIndex = UINT32_MAX;
    uint32_t thirdFailingIndex = UINT32_MAX;
    uint32_t failingCount = 0;
    for (uint32_t i = 0; i < N; i++)
    {
        if (i < 4)
        {
            printf("Expected: %u | Got: %u \n", 2 * i + 1, data[i]);
        }
        if (data[i] != 2 * i + 1)
        {
            ok = false;
            if (firstFailingIndex == UINT32_MAX)
            {
                firstFailingIndex = i;
            }else if (secondFailingIndex == UINT32_MAX)
            {
                secondFailingIndex = i;
            }else if (thirdFailingIndex == UINT32_MAX)
            {
                thirdFailingIndex = i;
            }
            failingCount++;
        }
    }

    if (ok)
    {
        printf("\nGPU wrote everything successfully\n");
    }else
    {
        // well not pretty because second or third could still be UINT32_T but eeeehhhh
        printf("\nGPU failed writing everything successfully\n. First failing index: %u; second failing index: %u; third failing index: %u. Failed count: %u\n", firstFailingIndex, secondFailingIndex, thirdFailingIndex, failingCount);
    }

    // Cleanup: reverse order of creation

    vkDestroyFence(device, fence, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);

    vkDestroyPipeline(device, pipeline, nullptr);

    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(device, setLayout, nullptr);

    vmaDestroyBuffer(allocator, buffer, allocation);

    vmaDestroyAllocator(allocator);

    vkDestroyDevice(device, nullptr);

    vkDestroyInstance(instance, nullptr);

    return 0;
}