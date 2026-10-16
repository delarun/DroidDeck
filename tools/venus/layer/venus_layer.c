#include <stdio.h>
#include <string.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#define EXPORT __attribute__((visibility("default")))

static PFN_vkGetInstanceProcAddr next_gipa;
static PFN_vkGetDeviceProcAddr next_gdpa;
static PFN_vkEnumerateDeviceExtensionProperties next_enum_device_exts;
static int fake_semaphore_fd;

static VkLayerInstanceCreateInfo *instance_link(const VkInstanceCreateInfo *info) {
    VkLayerInstanceCreateInfo *link = (VkLayerInstanceCreateInfo *)info->pNext;
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
        link = (VkLayerInstanceCreateInfo *)link->pNext;
    return link;
}

static VkLayerDeviceCreateInfo *device_link(const VkDeviceCreateInfo *info) {
    VkLayerDeviceCreateInfo *link = (VkLayerDeviceCreateInfo *)info->pNext;
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
        link = (VkLayerDeviceCreateInfo *)link->pNext;
    return link;
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateInstance(const VkInstanceCreateInfo *info,
                                                          const VkAllocationCallbacks *alloc, VkInstance *instance) {
    VkLayerInstanceCreateInfo *link = instance_link(info);
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    next_gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)next_gipa(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult result = create(info, alloc, instance);
    if (result == VK_SUCCESS)
        next_enum_device_exts = (PFN_vkEnumerateDeviceExtensionProperties)next_gipa(*instance, "vkEnumerateDeviceExtensionProperties");
    return result;
}

static int driver_has_extension(VkPhysicalDevice gpu, const char *name) {
    uint32_t total = 0;
    if (next_enum_device_exts(gpu, NULL, &total, NULL) != VK_SUCCESS) return 0;
    VkExtensionProperties all[total ? total : 1];
    if (next_enum_device_exts(gpu, NULL, &total, all) < 0) return 0;
    for (uint32_t i = 0; i < total; i++)
        if (!strcmp(all[i].extensionName, name)) return 1;
    return 0;
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateDevice(VkPhysicalDevice gpu, const VkDeviceCreateInfo *info,
                                                        const VkAllocationCallbacks *alloc, VkDevice *device) {
    VkLayerDeviceCreateInfo *link = device_link(info);
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    next_gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    PFN_vkCreateDevice create = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");
    fake_semaphore_fd = !driver_has_extension(gpu, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
    if (!fake_semaphore_fd) return create(gpu, info, alloc, device);
    VkDeviceCreateInfo copy = *info;
    const char *names[info->enabledExtensionCount ? info->enabledExtensionCount : 1];
    copy.enabledExtensionCount = 0;
    copy.ppEnabledExtensionNames = names;
    for (uint32_t i = 0; i < info->enabledExtensionCount; i++)
        if (strcmp(info->ppEnabledExtensionNames[i], VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME) != 0)
            names[copy.enabledExtensionCount++] = info->ppEnabledExtensionNames[i];
    return create(gpu, &copy, alloc, device);
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR *info, int *fd) {
    (void)device; (void)info;
    fprintf(stderr, "droiddeck-venus: vkGetSemaphoreFdKHR has no driver support over vtest\n");
    *fd = -1;
    return VK_ERROR_INVALID_EXTERNAL_HANDLE;
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR *info) {
    (void)device; (void)info;
    fprintf(stderr, "droiddeck-venus: vkImportSemaphoreFdKHR has no driver support over vtest\n");
    return VK_ERROR_INVALID_EXTERNAL_HANDLE;
}

/* Venus over vtest has no DRM node, yet still advertises VK_EXT_physical_device_drm; gamescope
 * then requires a primary node and refuses the device. Without the extension it takes the
 * no-DRM path it already uses with KGSL Turnip. vtest also has no sync_fd/syncobj transport, so
 * VK_KHR_external_semaphore_fd is missing; gamescope lists it as required but only calls it for
 * DRM syncobj explicit sync, which has no DRM device to run on here, so it is advertised and its
 * two entry points fail cleanly. */
static VKAPI_ATTR VkResult VKAPI_CALL layer_EnumerateDeviceExtensionProperties(VkPhysicalDevice gpu, const char *layer,
                                                                              uint32_t *count, VkExtensionProperties *props) {
    if (layer) return next_enum_device_exts(gpu, layer, count, props);
    uint32_t total = 0;
    VkResult result = next_enum_device_exts(gpu, NULL, &total, NULL);
    if (result != VK_SUCCESS) return result;
    VkExtensionProperties all[total ? total : 1];
    result = next_enum_device_exts(gpu, NULL, &total, all);
    if (result < 0) return result;
    VkExtensionProperties out[total + 1];
    uint32_t kept = 0;
    int has_semaphore_fd = 0;
    for (uint32_t i = 0; i < total; i++) {
        if (!strcmp(all[i].extensionName, VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME)) continue;
        if (!strcmp(all[i].extensionName, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME)) has_semaphore_fd = 1;
        out[kept++] = all[i];
    }
    if (!has_semaphore_fd) {
        memset(&out[kept], 0, sizeof(out[kept]));
        strcpy(out[kept].extensionName, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
        out[kept++].specVersion = VK_KHR_EXTERNAL_SEMAPHORE_FD_SPEC_VERSION;
        fake_semaphore_fd = 1;
    }
    if (!props) {
        *count = kept;
        return VK_SUCCESS;
    }
    uint32_t n = *count < kept ? *count : kept;
    memcpy(props, out, n * sizeof(*props));
    *count = n;
    return n < kept ? VK_INCOMPLETE : VK_SUCCESS;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL droiddeck_venus_GetDeviceProcAddr(VkDevice device, const char *name) {
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)droiddeck_venus_GetDeviceProcAddr;
    if (fake_semaphore_fd && !strcmp(name, "vkGetSemaphoreFdKHR")) return (PFN_vkVoidFunction)layer_GetSemaphoreFdKHR;
    if (fake_semaphore_fd && !strcmp(name, "vkImportSemaphoreFdKHR")) return (PFN_vkVoidFunction)layer_ImportSemaphoreFdKHR;
    return next_gdpa ? next_gdpa(device, name) : NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL droiddeck_venus_GetInstanceProcAddr(VkInstance instance, const char *name) {
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)droiddeck_venus_GetInstanceProcAddr;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)droiddeck_venus_GetDeviceProcAddr;
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)layer_CreateInstance;
    if (!strcmp(name, "vkCreateDevice")) return (PFN_vkVoidFunction)layer_CreateDevice;
    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties")) return (PFN_vkVoidFunction)layer_EnumerateDeviceExtensionProperties;
    if (fake_semaphore_fd && !strcmp(name, "vkGetSemaphoreFdKHR")) return (PFN_vkVoidFunction)layer_GetSemaphoreFdKHR;
    if (fake_semaphore_fd && !strcmp(name, "vkImportSemaphoreFdKHR")) return (PFN_vkVoidFunction)layer_ImportSemaphoreFdKHR;
    return next_gipa ? next_gipa(instance, name) : NULL;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL droiddeck_venus_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *v) {
    if (v->loaderLayerInterfaceVersion > 2) v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = droiddeck_venus_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = droiddeck_venus_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
