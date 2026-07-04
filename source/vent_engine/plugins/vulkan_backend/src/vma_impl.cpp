// vulkan_backend plugin.
// vma implementation.
// ——————————————————————
//
// includes the single-header vulkan memory allocator implementation.

#define VMA_IMPLEMENTATION

// disable warnings from vma
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include <vma/vk_mem_alloc.h>

#pragma GCC diagnostic pop
