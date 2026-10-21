# Presentation belongs to native hosts, independently of the model renderer.
add_library(nekomote_host_vulkan STATIC host.c)
add_library(nekomote::host_vulkan ALIAS nekomote_host_vulkan)
target_include_directories(nekomote_host_vulkan PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(nekomote_host_vulkan PUBLIC Vulkan::Vulkan PRIVATE nekomote_warnings)
target_compile_features(nekomote_host_vulkan PRIVATE c_std_11)

if(NEKOMOTE_BUILD_TESTS)
    add_library(nekomote_vulkan_present_retry SHARED EXCLUDE_FROM_ALL
        ${PROJECT_SOURCE_DIR}/tests/host_vulkan/present_retry.c)
    target_link_libraries(nekomote_vulkan_present_retry PRIVATE
        Vulkan::Headers ${CMAKE_DL_LIBS} nekomote_warnings)
    target_compile_features(nekomote_vulkan_present_retry PRIVATE c_std_11)
    add_executable(nekomote_host_vulkan_failure_tests
        ${PROJECT_SOURCE_DIR}/tests/host_vulkan/test_failures.c)
    target_link_libraries(nekomote_host_vulkan_failure_tests PRIVATE
        nekomote::host_vulkan nekomote_warnings)
    target_compile_features(nekomote_host_vulkan_failure_tests PRIVATE c_std_11)
    foreach(symbol malloc calloc free vkEnumeratePhysicalDevices
            vkEnumerateDeviceExtensionProperties vkGetPhysicalDeviceQueueFamilyProperties
            vkGetPhysicalDeviceProperties vkGetPhysicalDeviceSurfaceSupportKHR vkDeviceWaitIdle
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceFormatsKHR
            vkQueuePresentKHR)
        target_link_options(nekomote_host_vulkan_failure_tests PRIVATE "LINKER:--wrap=${symbol}")
    endforeach()
    add_test(NAME host_vulkan_failures COMMAND nekomote_host_vulkan_failure_tests)
endif()
