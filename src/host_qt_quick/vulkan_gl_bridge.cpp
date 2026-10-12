#include "vulkan_gl_bridge.hpp"

#include <QtGui/qopengl.h>
#include <QtGui/qopenglext.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace nekomote::qt_quick
{
namespace
{
constexpr auto kFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr auto kUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
constexpr std::uint64_t kTimeoutNs = 5'000'000'000;

void vk_check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": Vulkan error " +
                                 std::to_string(result));
}

struct FileDescriptor
{
    int value = -1;
    ~FileDescriptor()
    {
        if (value >= 0)
            close(value);
    }
    void consumed() noexcept
    {
        value = -1;
    }
};

template <typename Properties>
bool contains(const std::vector<Properties>& properties, const char* name)
{
    return std::any_of(properties.begin(), properties.end(),
                       [name](const auto& property)
                       {
                           if constexpr (requires { property.extensionName; })
                               return std::strcmp(property.extensionName, name) == 0;
                           else
                               return std::strcmp(property.layerName, name) == 0;
                       });
}
} // namespace

struct VulkanGlBridge::Impl
{
    QOpenGLContext* gl_context = nullptr;
    QOpenGLExtraFunctions* gl = nullptr;
    PFNGLGETUNSIGNEDBYTEI_VEXTPROC get_uuid = nullptr;
    PFNGLGETUNSIGNEDBYTEVEXTPROC get_driver_uuid = nullptr;
    PFNGLCREATEMEMORYOBJECTSEXTPROC create_memory = nullptr;
    PFNGLDELETEMEMORYOBJECTSEXTPROC delete_memory = nullptr;
    PFNGLMEMORYOBJECTPARAMETERIVEXTPROC memory_parameter = nullptr;
    PFNGLIMPORTMEMORYFDEXTPROC import_memory = nullptr;
    PFNGLTEXSTORAGEMEM2DEXTPROC texture_storage = nullptr;
    PFNGLGENSEMAPHORESEXTPROC create_semaphores = nullptr;
    PFNGLDELETESEMAPHORESEXTPROC delete_semaphores = nullptr;
    PFNGLIMPORTSEMAPHOREFDEXTPROC import_semaphore = nullptr;
    PFNGLWAITSEMAPHOREEXTPROC wait_semaphore = nullptr;
    PFNGLSIGNALSEMAPHOREEXTPROC signal_semaphore = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT debug_callback = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore ready = VK_NULL_HANDLE;
    VkSemaphore done = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    PFN_vkGetMemoryFdKHR get_memory_fd = nullptr;
    PFN_vkGetSemaphoreFdKHR get_semaphore_fd = nullptr;

    GLuint gl_memory = 0;
    GLuint gl_texture = 0;
    GLuint gl_fbo = 0;
    GLuint gl_ready = 0;
    GLuint gl_done = 0;
    GLsync gl_fence = nullptr;
    QSize size;
    bool submitted = false;
    bool recording = false;
    bool wait_gl = false;
    bool image_released = false;
    bool validation_active = false;
    std::atomic_uint errors{0};

    Impl()
    {
        try
        {
            initialize_gl();
            initialize_vk();
            initialize_commands();
            initialize_semaphores();
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }
    ~Impl()
    {
        cleanup();
    }

    void require_context() const
    {
        if (QOpenGLContext::currentContext() != gl_context)
            throw std::runtime_error("Vulkan bridge requires its original current Qt GL context");
    }

    void gl_check(const char* operation)
    {
        const auto error = gl->glGetError();
        if (error != GL_NO_ERROR)
            throw std::runtime_error(std::string(operation) + ": OpenGL error " +
                                     std::to_string(error));
    }

    template <typename Function>
    Function load_gl(const char* name)
    {
        const auto function = reinterpret_cast<Function>(gl_context->getProcAddress(name));
        if (!function)
            throw std::runtime_error(std::string("Missing GL interop function: ") + name);
        return function;
    }

    void initialize_gl()
    {
        gl_context = QOpenGLContext::currentContext();
        if (!gl_context || gl_context->isOpenGLES())
            throw std::runtime_error("Vulkan wallpaper requires a current desktop OpenGL context");
        gl = gl_context->extraFunctions();
        for (const auto* name : {"GL_EXT_memory_object", "GL_EXT_memory_object_fd",
                                 "GL_EXT_semaphore", "GL_EXT_semaphore_fd"})
            if (!gl_context->hasExtension(name))
                throw std::runtime_error(std::string("Vulkan/Qt interop requires ") + name);
        get_uuid = load_gl<PFNGLGETUNSIGNEDBYTEI_VEXTPROC>("glGetUnsignedBytei_vEXT");
        get_driver_uuid = load_gl<PFNGLGETUNSIGNEDBYTEVEXTPROC>("glGetUnsignedBytevEXT");
        create_memory = load_gl<PFNGLCREATEMEMORYOBJECTSEXTPROC>("glCreateMemoryObjectsEXT");
        delete_memory = load_gl<PFNGLDELETEMEMORYOBJECTSEXTPROC>("glDeleteMemoryObjectsEXT");
        memory_parameter =
            load_gl<PFNGLMEMORYOBJECTPARAMETERIVEXTPROC>("glMemoryObjectParameterivEXT");
        import_memory = load_gl<PFNGLIMPORTMEMORYFDEXTPROC>("glImportMemoryFdEXT");
        texture_storage = load_gl<PFNGLTEXSTORAGEMEM2DEXTPROC>("glTexStorageMem2DEXT");
        create_semaphores = load_gl<PFNGLGENSEMAPHORESEXTPROC>("glGenSemaphoresEXT");
        delete_semaphores = load_gl<PFNGLDELETESEMAPHORESEXTPROC>("glDeleteSemaphoresEXT");
        import_semaphore = load_gl<PFNGLIMPORTSEMAPHOREFDEXTPROC>("glImportSemaphoreFdEXT");
        wait_semaphore = load_gl<PFNGLWAITSEMAPHOREEXTPROC>("glWaitSemaphoreEXT");
        signal_semaphore = load_gl<PFNGLSIGNALSEMAPHOREEXTPROC>("glSignalSemaphoreEXT");
        gl_check("Query GL external-object functions");
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL report(VkDebugReportFlagsEXT flags,
                                                 VkDebugReportObjectTypeEXT, std::uint64_t,
                                                 std::size_t, std::int32_t, const char*,
                                                 const char* message, void* user)
    {
        auto* self = static_cast<Impl*>(user);
        if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
            self->errors.fetch_add(1, std::memory_order_relaxed);
        // Qt may route qWarning to journald. Validation evidence must reach
        // the invoking diagnostic process even under a desktop session.
        std::fprintf(stderr, "NekoMote Vulkan: %s\n", message);
        return VK_FALSE;
    }

    void initialize_vk()
    {
        std::uint32_t count = 0;
        vk_check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
                 "Enumerate Vulkan instance extensions");
        std::vector<VkExtensionProperties> extensions(count);
        vk_check(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()),
                 "Read Vulkan instance extensions");
        std::vector<const char*> enabled = {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
                                            VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
                                            VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME};
        for (const auto* extension : enabled)
            if (!contains(extensions, extension))
                throw std::runtime_error(std::string("Required Vulkan instance extension: ") +
                                         extension);
        vk_check(vkEnumerateInstanceLayerProperties(&count, nullptr), "Enumerate Vulkan layers");
        std::vector<VkLayerProperties> layers(count);
        vk_check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "Read Vulkan layers");
        bool validation_requested = qEnvironmentVariableIntValue("NEKOMOTE_VK_VALIDATION") == 1;
#ifndef NDEBUG
        validation_requested = true;
#endif
        const bool validation = validation_requested &&
                                contains(layers, "VK_LAYER_KHRONOS_validation") &&
                                contains(extensions, VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        validation_active = validation;
        if (validation_requested && !validation)
            qWarning("NekoMote: Vulkan validation layer unavailable");
        if (qEnvironmentVariableIntValue("NEKOMOTE_VK_VALIDATION") == 1 && !validation)
            throw std::runtime_error("Requested Vulkan validation layer is unavailable");
        if (validation)
            enabled.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        const char* layer_name = "VK_LAYER_KHRONOS_validation";
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "NekoMote Qt external-image bridge";
        application.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create.pApplicationInfo = &application;
        create.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
        create.ppEnabledExtensionNames = enabled.data();
        create.enabledLayerCount = validation ? 1U : 0U;
        create.ppEnabledLayerNames = validation ? &layer_name : nullptr;
        vk_check(vkCreateInstance(&create, nullptr, &instance), "Create Vulkan 1.0 instance");
        if (validation)
        {
            const auto create_debug = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
            if (!create_debug)
                throw std::runtime_error("Missing Vulkan debug-report function");
            VkDebugReportCallbackCreateInfoEXT callback{};
            callback.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
            callback.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
            callback.pfnCallback = report;
            callback.pUserData = this;
            vk_check(create_debug(instance, &callback, nullptr, &debug_callback),
                     "Create Vulkan validation callback");
        }
        const auto properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2KHR"));
        const auto image_properties2 =
            reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2KHR>(
                vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceImageFormatProperties2KHR"));
        const auto semaphore_properties =
            reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR>(
                vkGetInstanceProcAddr(instance,
                                      "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"));
        if (!properties2 || !image_properties2 || !semaphore_properties)
            throw std::runtime_error("Missing Vulkan external capability query functions");
        GLint uuid_count = 0;
        gl->glGetIntegerv(GL_NUM_DEVICE_UUIDS_EXT, &uuid_count);
        if (uuid_count <= 0 || uuid_count > 32)
            throw std::runtime_error("GL did not expose a usable device UUID");
        std::vector<std::array<GLubyte, GL_UUID_SIZE_EXT>> uuids(
            static_cast<std::size_t>(uuid_count));
        for (GLint i = 0; i < uuid_count; ++i)
            get_uuid(GL_DEVICE_UUID_EXT, static_cast<GLuint>(i),
                     uuids[static_cast<std::size_t>(i)].data());
        std::array<GLubyte, GL_UUID_SIZE_EXT> driver_uuid{};
        get_driver_uuid(GL_DRIVER_UUID_EXT, driver_uuid.data());
        gl_check("Query GL device and driver UUIDs");
        vk_check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "Enumerate Vulkan devices");
        std::vector<VkPhysicalDevice> devices(count);
        vk_check(vkEnumeratePhysicalDevices(instance, &count, devices.data()),
                 "Read Vulkan devices");
        const std::vector<const char*> device_extensions = {
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
            VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
            VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME};
        for (const auto candidate : devices)
        {
            VkPhysicalDeviceIDPropertiesKHR ids{};
            ids.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR;
            VkPhysicalDeviceProperties2KHR properties{};
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
            properties.pNext = &ids;
            properties2(candidate, &properties);
            // Opaque FD payloads are driver-private. Device identity alone
            // does not establish GL/Vulkan import compatibility.
            if (std::memcmp(driver_uuid.data(), ids.driverUUID, VK_UUID_SIZE) != 0)
                continue;
            if (std::none_of(
                    uuids.begin(), uuids.end(), [&ids](const auto& uuid)
                    { return std::memcmp(uuid.data(), ids.deviceUUID, VK_UUID_SIZE) == 0; }))
                continue;
            vk_check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr),
                     "Enumerate device extensions");
            std::vector<VkExtensionProperties> available(count);
            vk_check(
                vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, available.data()),
                "Read device extensions");
            if (std::any_of(device_extensions.begin(), device_extensions.end(),
                            [&available](const auto* extension)
                            { return !contains(available, extension); }))
                continue;
            VkPhysicalDeviceExternalImageFormatInfoKHR external{};
            external.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO_KHR;
            external.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
            VkPhysicalDeviceImageFormatInfo2KHR format{};
            format.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2_KHR;
            format.pNext = &external;
            format.format = kFormat;
            format.type = VK_IMAGE_TYPE_2D;
            format.tiling = VK_IMAGE_TILING_OPTIMAL;
            format.usage = kUsage;
            VkExternalImageFormatPropertiesKHR external_properties{};
            external_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES_KHR;
            VkImageFormatProperties2KHR image_properties{};
            image_properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2_KHR;
            image_properties.pNext = &external_properties;
            if (image_properties2(candidate, &format, &image_properties) != VK_SUCCESS ||
                !(external_properties.externalMemoryProperties.externalMemoryFeatures &
                  VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT_KHR))
                continue;
            VkPhysicalDeviceExternalSemaphoreInfoKHR semaphore_info{};
            semaphore_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO_KHR;
            semaphore_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
            VkExternalSemaphorePropertiesKHR semaphore_caps{};
            semaphore_caps.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES_KHR;
            semaphore_properties(candidate, &semaphore_info, &semaphore_caps);
            if (!(semaphore_caps.externalSemaphoreFeatures &
                  VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT_KHR))
                continue;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
            for (std::uint32_t i = 0; i < count; ++i)
                if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                {
                    physical = candidate;
                    queue_family = i;
                    break;
                }
            if (physical)
                break;
        }
        if (!physical)
            throw std::runtime_error(
                "No matching GL/Vulkan GPU with exportable RGBA8 memory and semaphores");
        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queue_create{};
        queue_create.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create.queueFamilyIndex = queue_family;
        queue_create.queueCount = 1;
        queue_create.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_create{};
        device_create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create.queueCreateInfoCount = 1;
        device_create.pQueueCreateInfos = &queue_create;
        device_create.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
        device_create.ppEnabledExtensionNames = device_extensions.data();
        vk_check(vkCreateDevice(physical, &device_create, nullptr, &device),
                 "Create Vulkan interop device");
        vkGetDeviceQueue(device, queue_family, 0, &queue);
        get_memory_fd =
            reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
        get_semaphore_fd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
            vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
        if (!get_memory_fd || !get_semaphore_fd)
            throw std::runtime_error("Missing Vulkan fd export functions");
    }

    void initialize_commands()
    {
        VkCommandPoolCreateInfo create_pool{};
        create_pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        create_pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        create_pool.queueFamilyIndex = queue_family;
        vk_check(vkCreateCommandPool(device, &create_pool, nullptr, &pool),
                 "Create bridge command pool");
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        vk_check(vkAllocateCommandBuffers(device, &allocate, &commands),
                 "Allocate bridge commands");
        VkFenceCreateInfo create_fence{};
        create_fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vk_check(vkCreateFence(device, &create_fence, nullptr, &fence), "Create bridge fence");
        VkAttachmentDescription attachment{};
        attachment.format = kFormat;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        // ABI target compatibility includes the external dependencies, not
        // only attachment format. Match the frozen render_vk target contract.
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        deps[0].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        VkRenderPassCreateInfo create_pass{};
        create_pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        create_pass.attachmentCount = 1;
        create_pass.pAttachments = &attachment;
        create_pass.subpassCount = 1;
        create_pass.pSubpasses = &subpass;
        create_pass.dependencyCount = 2;
        create_pass.pDependencies = deps;
        vk_check(vkCreateRenderPass(device, &create_pass, nullptr, &render_pass),
                 "Create bridge render pass");
    }

    void initialize_semaphores()
    {
        VkExportSemaphoreCreateInfoKHR export_info{};
        export_info.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO_KHR;
        export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
        VkSemaphoreCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        create.pNext = &export_info;
        vk_check(vkCreateSemaphore(device, &create, nullptr, &ready),
                 "Create Vulkan-ready semaphore");
        vk_check(vkCreateSemaphore(device, &create, nullptr, &done), "Create GL-done semaphore");
        create_semaphores(1, &gl_ready);
        create_semaphores(1, &gl_done);
        gl_check("Create GL semaphores");
        for (const auto [vk_semaphore, gl_semaphore] :
             {std::pair{ready, gl_ready}, std::pair{done, gl_done}})
        {
            VkSemaphoreGetFdInfoKHR get{};
            get.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
            get.semaphore = vk_semaphore;
            get.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
            FileDescriptor fd;
            vk_check(get_semaphore_fd(device, &get, &fd.value), "Export Vulkan semaphore fd");
            import_semaphore(gl_semaphore, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd.value);
            gl_check("Import GL semaphore fd");
            fd.consumed();
        }
    }

    void wait_ready()
    {
        require_context();
        if (submitted)
        {
            vk_check(vkWaitForFences(device, 1, &fence, VK_TRUE, kTimeoutNs),
                     "Wait prior Vulkan bridge frame");
            submitted = false;
        }
        if (errors.load(std::memory_order_relaxed))
            throw std::runtime_error("Vulkan bridge validation reported errors");
    }

    void wait_gl_complete()
    {
        if (!gl_fence)
            return;
        const auto result = gl->glClientWaitSync(gl_fence, GL_SYNC_FLUSH_COMMANDS_BIT, kTimeoutNs);
        if (result != GL_ALREADY_SIGNALED && result != GL_CONDITION_SATISFIED)
            throw std::runtime_error("Wait Qt GL shared-image work failed or timed out");
        gl->glDeleteSync(gl_fence);
        gl_fence = nullptr;
    }

    void destroy_output() noexcept
    {
        if (gl_fbo)
            gl->glDeleteFramebuffers(1, &gl_fbo);
        if (gl_texture)
            gl->glDeleteTextures(1, &gl_texture);
        if (gl_memory)
            delete_memory(1, &gl_memory);
        gl_fbo = gl_texture = gl_memory = 0;
        if (framebuffer)
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        if (view)
            vkDestroyImageView(device, view, nullptr);
        if (image)
            vkDestroyImage(device, image, nullptr);
        if (memory)
            vkFreeMemory(device, memory, nullptr);
        framebuffer = VK_NULL_HANDLE;
        view = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        image_released = false;
        size = {};
    }

    void create_output(QSize requested)
    {
        wait_gl_complete();
        destroy_output();
        VkExternalMemoryImageCreateInfoKHR external{};
        external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR;
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
        VkImageCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        create.pNext = &external;
        create.imageType = VK_IMAGE_TYPE_2D;
        create.format = kFormat;
        create.extent = {static_cast<std::uint32_t>(requested.width()),
                         static_cast<std::uint32_t>(requested.height()), 1};
        create.mipLevels = 1;
        create.arrayLayers = 1;
        create.samples = VK_SAMPLE_COUNT_1_BIT;
        create.tiling = VK_IMAGE_TILING_OPTIMAL;
        create.usage = kUsage;
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vk_check(vkCreateImage(device, &create, nullptr, &image), "Create shared RGBA8 image");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, image, &requirements);
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        std::uint32_t memory_type = properties.memoryTypeCount;
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1U << i)) &&
                (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                memory_type = i;
                break;
            }
        if (memory_type == properties.memoryTypeCount)
            throw std::runtime_error("No device-local memory for exported image");
        VkMemoryDedicatedAllocateInfoKHR dedicated{};
        dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
        dedicated.image = image;
        VkExportMemoryAllocateInfoKHR export_info{};
        export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR;
        export_info.pNext = &dedicated;
        export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.pNext = &export_info;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = memory_type;
        vk_check(vkAllocateMemory(device, &allocate, nullptr, &memory),
                 "Allocate exported dedicated image memory");
        vk_check(vkBindImageMemory(device, image, memory, 0), "Bind shared image memory");
        VkImageViewCreateInfo create_view{};
        create_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_view.image = image;
        create_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create_view.format = kFormat;
        create_view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk_check(vkCreateImageView(device, &create_view, nullptr, &view),
                 "Create shared image view");
        VkFramebufferCreateInfo create_fbo{};
        create_fbo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        create_fbo.renderPass = render_pass;
        create_fbo.attachmentCount = 1;
        create_fbo.pAttachments = &view;
        create_fbo.width = static_cast<std::uint32_t>(requested.width());
        create_fbo.height = static_cast<std::uint32_t>(requested.height());
        create_fbo.layers = 1;
        vk_check(vkCreateFramebuffer(device, &create_fbo, nullptr, &framebuffer),
                 "Create shared Vulkan framebuffer");
        VkMemoryGetFdInfoKHR get{};
        get.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        get.memory = memory;
        get.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
        FileDescriptor fd;
        vk_check(get_memory_fd(device, &get, &fd.value), "Export shared image memory fd");
        create_memory(1, &gl_memory);
        const GLint dedicated_gl = GL_TRUE;
        memory_parameter(gl_memory, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated_gl);
        gl_check("Create dedicated GL memory object");
        import_memory(gl_memory, requirements.size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd.value);
        gl_check("Import Vulkan image memory into GL");
        fd.consumed();
        GLint previous_texture = 0;
        GLint previous_read_fbo = 0;
        gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
        gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
        gl->glGenTextures(1, &gl_texture);
        gl->glBindTexture(GL_TEXTURE_2D, gl_texture);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT);
        texture_storage(GL_TEXTURE_2D, 1, GL_RGBA8, requested.width(), requested.height(),
                        gl_memory, 0);
        gl->glGenFramebuffers(1, &gl_fbo);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_fbo);
        gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   gl_texture, 0);
        const auto status = gl->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_fbo));
        gl->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
        gl_check("Create imported GL framebuffer");
        if (status != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Imported Vulkan image is not a complete GL framebuffer");
        size = requested;
    }

    void barrier(VkImageLayout old_layout, VkImageLayout new_layout, VkAccessFlags source_access,
                 VkAccessFlags target_access, VkPipelineStageFlags source_stage,
                 VkPipelineStageFlags target_stage,
                 std::uint32_t source_family = VK_QUEUE_FAMILY_IGNORED,
                 std::uint32_t target_family = VK_QUEUE_FAMILY_IGNORED)
    {
        VkImageMemoryBarrier change{};
        change.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        change.srcAccessMask = source_access;
        change.dstAccessMask = target_access;
        change.oldLayout = old_layout;
        change.newLayout = new_layout;
        change.srcQueueFamilyIndex = source_family;
        change.dstQueueFamilyIndex = target_family;
        change.image = image;
        change.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(commands, source_stage, target_stage, 0, 0, nullptr, 0, nullptr, 1,
                             &change);
    }

    nekomote_vk_target begin_frame(QSize requested)
    {
        require_context();
        if (recording)
            throw std::runtime_error("Vulkan bridge begin_frame without presenting prior frame");
        if (requested.width() <= 0 || requested.height() <= 0)
            throw std::runtime_error("Vulkan bridge output dimensions must be positive");
        wait_ready();
        if (size != requested)
            create_output(requested);
        vk_check(vkResetCommandBuffer(commands, 0), "Reset bridge command buffer");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk_check(vkBeginCommandBuffer(commands, &begin), "Begin bridge command buffer");
        recording = true;
        if (image_released)
            barrier(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_QUEUE_FAMILY_EXTERNAL_KHR, queue_family);
        barrier(image_released ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkClearColorValue clear{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1,
                             &range);
        barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        return {commands, reinterpret_cast<void*>(framebuffer), requested.width(),
                requested.height()};
    }

    void present(unsigned qt_fbo)
    {
        require_context();
        if (!recording)
            throw std::runtime_error("Vulkan bridge present without begin_frame");
        barrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                queue_family, VK_QUEUE_FAMILY_EXTERNAL_KHR);
        vk_check(vkEndCommandBuffer(commands), "End bridge commands");
        recording = false;
        vk_check(vkResetFences(device, 1, &fence), "Reset bridge fence before submit");
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = wait_gl ? 1U : 0U;
        submit.pWaitSemaphores = wait_gl ? &done : nullptr;
        submit.pWaitDstStageMask = wait_gl ? &wait_stage : nullptr;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &ready;
        vk_check(vkQueueSubmit(queue, 1, &submit, fence), "Submit Vulkan wallpaper frame");
        submitted = true;
        image_released = true;
        const GLenum layout = GL_LAYOUT_GENERAL_EXT;
        wait_semaphore(gl_ready, 0, nullptr, 1, &gl_texture, &layout);
        GLint previous_read = 0;
        GLint previous_draw = 0;
        gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
        gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw);
        const bool scissor = gl->glIsEnabled(GL_SCISSOR_TEST);
        const bool srgb = gl->glIsEnabled(GL_FRAMEBUFFER_SRGB);
        gl->glDisable(GL_SCISSOR_TEST);
        gl->glDisable(GL_FRAMEBUFFER_SRGB);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_fbo);
        gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, qt_fbo);
        gl->glBlitFramebuffer(0, 0, size.width(), size.height(), 0, 0, size.width(), size.height(),
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read));
        gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw));
        if (scissor)
            gl->glEnable(GL_SCISSOR_TEST);
        if (srgb)
            gl->glEnable(GL_FRAMEBUFFER_SRGB);
        signal_semaphore(gl_done, 0, nullptr, 1, &gl_texture, &layout);
        wait_gl = true;
        if (gl_fence)
            gl->glDeleteSync(gl_fence);
        gl_fence = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        gl->glFlush();
        gl_check("Wait/copy/signal shared Vulkan image in Qt GL");
        if (!gl_fence)
            throw std::runtime_error("Create GL bridge completion fence failed");
    }

    void cleanup() noexcept
    {
        if (device)
        {
            // Teardown is allowed to drain both queues. Normal frames never use
            // device/queue idle or glFinish. A failed device is reported upstream.
            const auto result = vkDeviceWaitIdle(device);
            if (result != VK_SUCCESS)
                qWarning("NekoMote Vulkan bridge teardown: device wait error %d", result);
        }
        const bool current = gl_context && QOpenGLContext::currentContext() == gl_context;
        if (current)
        {
            try
            {
                wait_gl_complete();
            }
            catch (const std::exception& error)
            {
                qWarning("NekoMote Vulkan bridge teardown: %s", error.what());
                // A teardown-only drain protects external storage after a
                // bounded GL sync wait fails. It is not part of steady playback.
                gl->glFinish();
                if (gl_fence)
                    gl->glDeleteSync(gl_fence);
                gl_fence = nullptr;
            }
            destroy_output();
            if (gl_ready)
                delete_semaphores(1, &gl_ready);
            if (gl_done)
                delete_semaphores(1, &gl_done);
        }
        else if (gl_memory || gl_texture || gl_fbo || gl_ready || gl_done)
        {
            qCritical("NekoMote Vulkan bridge destroyed without its current GL context");
            // The owner must destroy on Qt's render thread; no other context may
            // safely delete these objects. Preserve GL object IDs for diagnosis.
        }
        if (!current && device)
        {
            if (framebuffer)
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            if (view)
                vkDestroyImageView(device, view, nullptr);
            if (image)
                vkDestroyImage(device, image, nullptr);
            if (memory)
                vkFreeMemory(device, memory, nullptr);
        }
        if (device)
        {
            if (render_pass)
                vkDestroyRenderPass(device, render_pass, nullptr);
            if (ready)
                vkDestroySemaphore(device, ready, nullptr);
            if (done)
                vkDestroySemaphore(device, done, nullptr);
            if (fence)
                vkDestroyFence(device, fence, nullptr);
            if (pool)
                vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (debug_callback)
        {
            const auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
            if (destroy_debug)
                destroy_debug(instance, debug_callback, nullptr);
        }
        if (instance)
            qInfo("NekoMote Vulkan bridge: validation=%s, errors=%u",
                  validation_active ? "enabled" : "disabled", errors.load());
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }
};

VulkanGlBridge::VulkanGlBridge() : impl_(std::make_unique<Impl>())
{
}
VulkanGlBridge::~VulkanGlBridge() = default;
nekomote_vk_device_context VulkanGlBridge::device_context() const noexcept
{
    return {impl_->instance, impl_->physical,     impl_->device,
            impl_->queue,    impl_->queue_family, static_cast<std::int32_t>(kFormat)};
}
void VulkanGlBridge::wait_ready()
{
    impl_->wait_ready();
}
void VulkanGlBridge::wait_noexcept() noexcept
{
    if (impl_->device)
    {
        const auto result = vkDeviceWaitIdle(impl_->device);
        if (result != VK_SUCCESS)
            qWarning("NekoMote Vulkan wallpaper teardown: device wait error %d", result);
        impl_->submitted = false;
    }
}
nekomote_vk_target VulkanGlBridge::begin_frame(QSize size)
{
    return impl_->begin_frame(size);
}
void VulkanGlBridge::present(unsigned qt_fbo)
{
    impl_->present(qt_fbo);
}
unsigned VulkanGlBridge::validation_errors() const noexcept
{
    return impl_->errors.load(std::memory_order_relaxed);
}
} // namespace nekomote::qt_quick
