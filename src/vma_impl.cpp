// The one file that compiles VMA's implementation.
//
// volk.h must come first. It declares every vk* function as a pointer that
// volk fills in at runtime, and it unlocks vmaImportVulkanFunctionsFromVolk(),
// which is how VMA gets those pointers later.
#include <volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
