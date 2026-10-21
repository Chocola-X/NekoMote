#include <nekomote/host_vulkan/host.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fault injection only: no device, display, queue, or Vulkan driver is used.
 * Link wrapping keeps the real host failure/cleanup paths under test. */
enum Failure
{
    FAIL_NONE,
    FAIL_DEVICE_READ,
    FAIL_EXTENSION_COUNT,
    FAIL_EXTENSION_READ,
    FAIL_QUEUE_SUPPORT,
    FAIL_FORMAT_READ
};

static enum Failure failure;
static unsigned allocation_attempts;
static unsigned fail_allocation;
static unsigned live_allocations;
static void* allocations[16];
static VkResult present_result;
static unsigned present_calls;
static int failures;

void* __real_malloc(size_t size);
void* __real_calloc(size_t count, size_t size);
void __real_free(void* pointer);
void* __wrap_malloc(size_t size);
void* __wrap_calloc(size_t count, size_t size);
void __wrap_free(void* pointer);
VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkEnumeratePhysicalDevices(VkInstance instance,
                                                                 uint32_t* count,
                                                                 VkPhysicalDevice* devices);
VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice device, const char* layer, uint32_t* count, VkExtensionProperties* extensions);
VKAPI_ATTR void VKAPI_CALL __wrap_vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice device, uint32_t* count, VkQueueFamilyProperties* queues);
VKAPI_ATTR void VKAPI_CALL __wrap_vkGetPhysicalDeviceProperties(
    VkPhysicalDevice device, VkPhysicalDeviceProperties* properties);
VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice device,
                                                                           uint32_t family,
                                                                           VkSurfaceKHR surface,
                                                                           VkBool32* supported);
VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkDeviceWaitIdle(VkDevice device);
VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* capabilities);
VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t* count, VkSurfaceFormatKHR* formats);
VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkQueuePresentKHR(VkQueue queue,
                                                        const VkPresentInfoKHR* info);

static void* track(void* pointer)
{
    if (!pointer)
        return NULL;
    for (unsigned i = 0; i < sizeof(allocations) / sizeof(allocations[0]); ++i)
        if (!allocations[i])
        {
            allocations[i] = pointer;
            ++live_allocations;
            return pointer;
        }
    abort();
}

void* __wrap_malloc(size_t size)
{
    if (++allocation_attempts == fail_allocation)
        return NULL;
    return track(__real_malloc(size));
}

void* __wrap_calloc(size_t count, size_t size)
{
    if (++allocation_attempts == fail_allocation)
        return NULL;
    return track(__real_calloc(count, size));
}

void __wrap_free(void* pointer)
{
    if (pointer)
        for (unsigned i = 0; i < sizeof(allocations) / sizeof(allocations[0]); ++i)
            if (allocations[i] == pointer)
            {
                allocations[i] = NULL;
                --live_allocations;
                break;
            }
    __real_free(pointer);
}

VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkEnumeratePhysicalDevices(VkInstance instance,
                                                                 uint32_t* count,
                                                                 VkPhysicalDevice* devices)
{
    (void)instance;
    *count = 1;
    if (devices)
    {
        if (failure == FAIL_DEVICE_READ)
            return VK_INCOMPLETE;
        devices[0] = (VkPhysicalDevice)(uintptr_t)1;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice device, const char* layer, uint32_t* count, VkExtensionProperties* extensions)
{
    (void)device;
    (void)layer;
    *count = 1;
    if (!extensions && failure == FAIL_EXTENSION_COUNT)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (extensions)
    {
        if (failure == FAIL_EXTENSION_READ)
            return VK_INCOMPLETE;
        memset(extensions, 0, sizeof(*extensions));
        strcpy(extensions[0].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL __wrap_vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice device, uint32_t* count, VkQueueFamilyProperties* queues)
{
    (void)device;
    *count = 1;
    if (queues)
    {
        memset(queues, 0, sizeof(*queues));
        queues[0].queueFlags = VK_QUEUE_GRAPHICS_BIT;
        queues[0].queueCount = 1;
    }
}

VKAPI_ATTR void VKAPI_CALL __wrap_vkGetPhysicalDeviceProperties(
    VkPhysicalDevice device, VkPhysicalDeviceProperties* properties)
{
    (void)device;
    memset(properties, 0, sizeof(*properties));
    strcpy(properties->deviceName, "synthetic failure-path device");
}

VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice device,
                                                                           uint32_t family,
                                                                           VkSurfaceKHR surface,
                                                                           VkBool32* supported)
{
    (void)device;
    (void)family;
    (void)surface;
    *supported = VK_TRUE;
    return failure == FAIL_QUEUE_SUPPORT ? VK_ERROR_SURFACE_LOST_KHR : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkDeviceWaitIdle(VkDevice device)
{
    (void)device;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* capabilities)
{
    (void)device;
    (void)surface;
    memset(capabilities, 0, sizeof(*capabilities));
    capabilities->supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    capabilities->currentExtent = (VkExtent2D){64, 64};
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t* count, VkSurfaceFormatKHR* formats)
{
    (void)device;
    (void)surface;
    *count = 1;
    if (formats)
        return failure == FAIL_FORMAT_READ ? VK_INCOMPLETE : VK_ERROR_INITIALIZATION_FAILED;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info)
{
    (void)queue;
    if (info->swapchainCount != 1 || info->waitSemaphoreCount != 1)
        abort();
    ++present_calls;
    return present_result;
}

static void expect(int condition, const char* context)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s (injection=%d allocation=%u)\n", context, (int)failure,
                fail_allocation);
        ++failures;
    }
}

static void reset(enum Failure selected, unsigned allocation)
{
    expect(live_allocations == 0, "previous case released all host allocations");
    failure = selected;
    allocation_attempts = 0;
    fail_allocation = allocation;
}

static void selection_case(enum Failure selected, unsigned allocation)
{
    reset(selected, allocation);
    VkHostC host = {0};
    const VkSurfaceKHR surface = (VkSurfaceKHR)(uintptr_t)1;
    const int result = vk_host_c_select_device(&host, surface);
    expect(result == (selected == FAIL_NONE && !allocation ? 0 : -1),
           "selection reports injected error");
    const unsigned expected_attempts =
        allocation ? allocation
                   : (selected == FAIL_DEVICE_READ || selected == FAIL_EXTENSION_COUNT
                          ? 1U
                          : (selected == FAIL_EXTENSION_READ ? 2U : 3U));
    expect(allocation_attempts == expected_attempts, "selection reached intended failure branch");
    expect(host.surface == surface, "selection retains surface ownership even on failure");
    expect(live_allocations == 0, "selection releases all temporary allocations");
}

static void format_case(unsigned allocation)
{
    reset(FAIL_FORMAT_READ, allocation);
    VkHostC host = {0};
    nekomote_vk_target target = {0};
    expect(vk_host_c_begin_frame(&host, 64, 64, 1, &target) == -1,
           "swapchain reports injected format error");
    expect(allocation_attempts == 1, "format failure reached format allocation/read branch");
    expect(live_allocations == 0, "format failure releases temporary array");
}

static void presentation_case(VkResult injected, int expected, int recreate)
{
    reset(FAIL_NONE, 0);
    VkHostC host = {0};
    VkSemaphore finished = VK_NULL_HANDLE;
    host.swap_finished = &finished;
    host.present_frame = 1;
    present_result = injected;
    present_calls = 0;
    /* recording=0 models the already-submitted frame, bypassing GPU work. */
    expect(vk_host_c_present(&host) == expected, "present distinguishes accepted/retry/error");
    expect(present_calls == 1, "present reached injected queue result");
    expect(host.recreate == recreate, "present requests rebuild only when required");
    if (expected >= 0)
        expect(!host.present_frame, "accepted/retry result consumes present request");
    if (expected == 1)
    {
        expect(vk_host_c_present(&host) == 0, "retry cannot present the obsolete image twice");
        expect(present_calls == 1, "retry result clears obsolete present request");
    }
}

int main(void)
{
    selection_case(FAIL_NONE, 0);
    selection_case(FAIL_DEVICE_READ, 0);
    selection_case(FAIL_EXTENSION_COUNT, 0);
    selection_case(FAIL_EXTENSION_READ, 0);
    selection_case(FAIL_QUEUE_SUPPORT, 0);
    for (unsigned allocation = 1; allocation <= 3; ++allocation)
        selection_case(FAIL_NONE, allocation);
    format_case(0);
    format_case(1);
    presentation_case(VK_SUCCESS, 0, 0);
    presentation_case(VK_SUBOPTIMAL_KHR, 0, 1);
    presentation_case(VK_ERROR_OUT_OF_DATE_KHR, 1, 1);
    presentation_case(VK_ERROR_SURFACE_LOST_KHR, -1, 0);
    if (failures)
        return EXIT_FAILURE;
    puts("host_vulkan synthetic failure paths passed (no GPU/display)");
    return EXIT_SUCCESS;
}
