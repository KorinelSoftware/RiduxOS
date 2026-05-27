#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

static long rdx_syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a), "S"(b), "d"(c)
        : "rcx", "r11", "memory");
    return ret;
}

static void rdx_exit(int code) {
    (void)rdx_syscall3(60, code, 0, 0);
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

static size_t rdx_strlen(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static void log_str(const char *s) {
    size_t n = rdx_strlen(s);
    if (n) (void)rdx_syscall3(1, 2, (long)(uintptr_t)s, (long)n);
}

static void log_u32(uint32_t v) {
    char buf[11];
    size_t i = sizeof(buf);
    if (!v) {
        log_str("0");
        return;
    }
    while (v && i) {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    (void)rdx_syscall3(1, 2, (long)(uintptr_t)(buf + i), (long)(sizeof(buf) - i));
}

static void log_i32(int32_t v) {
    if (v < 0) {
        log_str("-");
        log_u32((uint32_t)(-v));
    } else {
        log_u32((uint32_t)v);
    }
}

static void log_hex32(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[10];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; ++i) {
        buf[2 + i] = hex[(v >> (28 - i * 4)) & 0xFu];
    }
    (void)rdx_syscall3(1, 2, (long)(uintptr_t)buf, (long)sizeof(buf));
}

static void zero_bytes(void *ptr, size_t n) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (n--) *p++ = 0;
}

static const char *vk_result_name(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        default: return "VK_ERROR_UNKNOWN";
    }
}

static bool has_token_ci(const char *s, const char *needle) {
    size_t n;
    size_t i;
    if (!s || !needle || !*needle) return false;
    n = rdx_strlen(needle);
    for (i = 0; s[i]; ++i) {
        size_t j;
        for (j = 0; j < n; ++j) {
            char a = s[i + j];
            char b = needle[j];
            if (!a) return false;
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (j == n) return true;
    }
    return false;
}

static const char *device_type_name(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
        default: return "other";
    }
}

static bool looks_software_device(const VkPhysicalDeviceProperties *props) {
    const char *name = props ? props->deviceName : "";
    if (!props) return true;
    if (props->deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) return true;
    return has_token_ci(name, "lavapipe") ||
           has_token_ci(name, "llvmpipe") ||
           has_token_ci(name, "softpipe") ||
           has_token_ci(name, "swrast") ||
           has_token_ci(name, "software rasterizer") ||
           has_token_ci(name, "lvp");
}

static bool find_graphics_queue(VkPhysicalDevice dev, uint32_t *family_out) {
    VkQueueFamilyProperties families[32];
    uint32_t count = 0;
    uint32_t i;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, 0);
    if (!count) return false;
    if (count > 32u) count = 32u;
    zero_bytes(families, sizeof(families));
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families);
    for (i = 0; i < count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && families[i].queueCount > 0) {
            *family_out = i;
            return true;
        }
    }
    return false;
}

static void log_version(uint32_t api) {
    log_u32(VK_VERSION_MAJOR(api));
    log_str(".");
    log_u32(VK_VERSION_MINOR(api));
    log_str(".");
    log_u32(VK_VERSION_PATCH(api));
}

static void log_vk_failure(const char *prefix, VkResult rc) {
    log_str(prefix);
    log_str(vk_result_name(rc));
    log_str("(");
    log_i32((int32_t)rc);
    log_str(")\n");
}

int main(void) {
    VkApplicationInfo app;
    VkInstanceCreateInfo ici;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice devices[16];
    uint32_t dev_count = 0;
    uint32_t api = VK_API_VERSION_1_0;
    VkResult rc;
    uint32_t i;
    int status = 3;

    rc = vkEnumerateInstanceVersion(&api);
    if (rc != VK_SUCCESS) {
        log_vk_failure("[ridux-vulkan] vkEnumerateInstanceVersion failed rc=", rc);
        rdx_exit(2);
    }

    log_str("[ridux-vulkan] loader-api=");
    log_version(api);
    log_str("\n");

    zero_bytes(&app, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Ridux Vulkan Probe";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "RiduxOS";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = api >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;

    zero_bytes(&ici, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    rc = vkCreateInstance(&ici, 0, &instance);
    if (rc != VK_SUCCESS) {
        log_vk_failure("[ridux-vulkan] vkCreateInstance failed rc=", rc);
        rdx_exit(2);
    }

    log_str("[ridux-vulkan] enumerate-count begin\n");
    rc = vkEnumeratePhysicalDevices(instance, &dev_count, 0);
    log_str("[ridux-vulkan] enumerate-count done rc=");
    log_str(vk_result_name(rc));
    log_str("(");
    log_i32((int32_t)rc);
    log_str(") count=");
    log_u32(dev_count);
    log_str("\n");
    if (rc != VK_SUCCESS || dev_count == 0) {
        log_vk_failure("[ridux-vulkan] no physical devices rc=", rc);
        log_str("[ridux-vulkan] physical-device-count=");
        log_u32(dev_count);
        log_str("\n");
        status = 4;
        goto out;
    }

    if (dev_count > 16u) dev_count = 16u;
    zero_bytes(devices, sizeof(devices));
    log_str("[ridux-vulkan] enumerate-list begin cap=");
    log_u32(dev_count);
    log_str("\n");
    rc = vkEnumeratePhysicalDevices(instance, &dev_count, devices);
    log_str("[ridux-vulkan] enumerate-list done rc=");
    log_str(vk_result_name(rc));
    log_str("(");
    log_i32((int32_t)rc);
    log_str(") count=");
    log_u32(dev_count);
    log_str("\n");
    if (rc != VK_SUCCESS) {
        log_vk_failure("[ridux-vulkan] enumerate devices failed rc=", rc);
        status = 6;
        goto out;
    }

    for (i = 0; i < dev_count; ++i) {
        VkPhysicalDeviceProperties props;
        uint32_t queue_family = 0;
        bool software;
        zero_bytes(&props, sizeof(props));
        log_str("[ridux-vulkan] device-props begin #");
        log_u32(i);
        log_str("\n");
        vkGetPhysicalDeviceProperties(devices[i], &props);
        log_str("[ridux-vulkan] device-props done #");
        log_u32(i);
        log_str("\n");
        software = looks_software_device(&props);
        log_str("[ridux-vulkan] device#");
        log_u32(i);
        log_str(" name=");
        log_str(props.deviceName);
        log_str(" type=");
        log_str(device_type_name(props.deviceType));
        log_str(" vendor=");
        log_hex32(props.vendorID);
        log_str(" device=");
        log_hex32(props.deviceID);
        log_str(" api=");
        log_version(props.apiVersion);
        log_str(" software=");
        log_str(software ? "yes" : "no");
        log_str("\n");
        if (!software) {
            bool has_graphics;
            log_str("[ridux-vulkan] queue-scan begin #");
            log_u32(i);
            log_str("\n");
            has_graphics = find_graphics_queue(devices[i], &queue_family);
            log_str("[ridux-vulkan] queue-scan done #");
            log_u32(i);
            log_str(" graphics=");
            log_str(has_graphics ? "yes" : "no");
            log_str(" family=");
            log_u32(queue_family);
            log_str("\n");
            if (has_graphics) {
            float priority = 1.0f;
            VkDeviceQueueCreateInfo qci;
            VkDeviceCreateInfo dci;
            VkDevice device = VK_NULL_HANDLE;
            zero_bytes(&qci, sizeof(qci));
            qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qci.queueFamilyIndex = queue_family;
            qci.queueCount = 1;
            qci.pQueuePriorities = &priority;
            zero_bytes(&dci, sizeof(dci));
            dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            dci.queueCreateInfoCount = 1;
            dci.pQueueCreateInfos = &qci;
            log_str("[ridux-vulkan] create-device begin #");
            log_u32(i);
            log_str("\n");
            rc = vkCreateDevice(devices[i], &dci, 0, &device);
            log_str("[ridux-vulkan] create-device done #");
            log_u32(i);
            log_str(" rc=");
            log_str(vk_result_name(rc));
            log_str("(");
            log_i32((int32_t)rc);
            log_str(")\n");
            if (rc == VK_SUCCESS) {
                log_str("[ridux-vulkan-real] device=");
                log_str(props.deviceName);
                log_str(" type=");
                log_str(device_type_name(props.deviceType));
                log_str(" queue=");
                log_u32(queue_family);
                log_str(" status=hardware-required accepted\n");
                vkDestroyDevice(device, 0);
                status = 0;
                goto out;
            }
            log_str("[ridux-vulkan] vkCreateDevice rejected ");
            log_str(props.deviceName);
            log_str(" rc=");
            log_str(vk_result_name(rc));
            log_str("(");
            log_i32((int32_t)rc);
            log_str(")\n");
            }
        }
    }

    log_str("[ridux-vulkan] rejected software/no-graphics Vulkan path\n");
    status = 7;

out:
    if (instance != VK_NULL_HANDLE)
        vkDestroyInstance(instance, 0);
    rdx_exit(status);
}
