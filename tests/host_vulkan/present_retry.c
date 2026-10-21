/* Diagnostic interposer: emulate a present that never commits a Wayland buffer.
 * Only loaded by the isolated GPU test; never linked into a production binary. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info)
{
    static PFN_vkQueuePresentKHR present = NULL;
    static int injected = 0;
    const char* enabled = getenv("NEKOMOTE_TEST_PRESENT_RETRY");
    if (!injected && enabled && strcmp(enabled, "1") == 0)
    {
        injected = 1;
        fprintf(stderr, "[present_retry] injected OUT_OF_DATE without surface commit\n");
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    if (!present)
    {
        void* address = dlsym(RTLD_NEXT, "vkQueuePresentKHR");
        _Static_assert(sizeof(address) == sizeof(present), "POSIX function pointer size");
        memcpy(&present, &address, sizeof(present));
        if (!present)
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    return present(queue, info);
}
