#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GL/glx.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

/* Diagnostic preload only: same accepted-present boundary for each backend.
 * No pixel readback, heap allocation or file I/O in the measured frame path.
 * Warm 120 accepted submissions; measure the next 480, excluding teardown.
 * Intervals describe host submission cadence, not physical scanout timestamps. */
static unsigned frames;
static double previous, cpu_start, cpu_end, intervals[480];
static double seconds(clockid_t clock)
{
    struct timespec value;
    if (clock_gettime(clock, &value) != 0)
        abort();
    return (double)value.tv_sec + (double)value.tv_nsec * 1e-9;
}
static void accepted(void)
{
    ++frames;
    if (frames < 120 || frames > 600)
        return;
    const double now = seconds(CLOCK_MONOTONIC);
    if (frames == 120)
        cpu_start = seconds(CLOCK_PROCESS_CPUTIME_ID);
    else
        intervals[frames - 121] = (now - previous) * 1000;
    previous = now;
    if (frames == 600)
        cpu_end = seconds(CLOCK_PROCESS_CPUTIME_ID);
}
static void load(const char* name, void* function, size_t size)
{
    void* symbol = dlsym(RTLD_NEXT, name);
    if (!symbol || size != sizeof(symbol))
        abort();
    memcpy(function, &symbol, size);
}
EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface)
{
    static EGLBoolean (*real)(EGLDisplay, EGLSurface);
    if (!real)
        load("eglSwapBuffers", &real, sizeof(real));
    const EGLBoolean result = real(display, surface);
    if (result)
        accepted();
    return result;
}
void glXSwapBuffers(Display* display, GLXDrawable drawable)
{
    static void (*real)(Display*, GLXDrawable);
    if (!real)
        load("glXSwapBuffers", &real, sizeof(real));
    real(display, drawable);
    accepted();
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info)
{
    static PFN_vkQueuePresentKHR real;
    if (!real)
        load("vkQueuePresentKHR", &real, sizeof(real));
    const VkResult result = real(queue, info);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
        accepted();
    return result;
}
static int compare(const void* a, const void* b)
{
    const double first = *(const double*)a, second = *(const double*)b;
    return (first > second) - (first < second);
}
static void report(void) __attribute__((destructor));
static void report(void)
{
    const char* path = getenv("NEKOMOTE_PRESENT_METRICS_FILE");
    if (!path || frames != 600 || cpu_end <= cpu_start)
        return;
    double total = 0;
    for (unsigned i = 0; i < 480; ++i)
        total += intervals[i];
    qsort(intervals, 480, sizeof(double), compare);
    FILE* file = fopen(path, "w");
    if (!file)
        return;
    fprintf(file,
            "{\"warmup\":120,\"samples\":480,\"cpu_seconds\":%.9f,"
            "\"submission_fps\":%.6f,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"p99_ms\":%.6f}\n",
            cpu_end - cpu_start, 480000 / total, intervals[239], intervals[455], intervals[474]);
    fclose(file);
}
