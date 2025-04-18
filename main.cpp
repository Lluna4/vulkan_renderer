#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#include <GLFW/glfw3.h>
#include <print>
#include <vector>
#include <fstream>

std::vector<char> read_file(const char *filename)
{
    std::ifstream file(filename);
    file.seekg(0,std::ios::end);
    std::streampos length = file.tellg();
    file.seekg(0,std::ios::beg);
    std::vector<char> buffer(length);
    file.read(&buffer[0],length);

    return buffer;
}

GLFWwindow *create_window(int width, int height, const char *title)
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow *window = glfwCreateWindow(width, height, title, nullptr, nullptr);

    if (!window)
    {
        std::println("GLFW window creation failed!");
        return nullptr;
    }
    return window;
}

int main()
{
    GLFWwindow *window = create_window(800, 800, "hello");

    if (!window)
        return -1;

    vk::detail::DynamicLoader dl;
    PFN_vkGetInstanceProcAddr pfnVkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(pfnVkGetInstanceProcAddr);

    vk::ApplicationInfo appinfo = vk::ApplicationInfo("Test_vk", VK_MAKE_VERSION(0,1,0), NULL, VK_MAKE_VERSION(0,1,0), VK_API_VERSION_1_4);
    
    uint32_t extension_count = 0;
    const char **glfwextensions = glfwGetRequiredInstanceExtensions(&extension_count);
    std::println("GLFW requested extensions:");
    std::vector<const char *> extensions;
    std::vector<const char *> layers;
    for (int i = 0; i < extension_count;i++)
    {
        extensions.push_back(glfwextensions[i]);
        std::println("{}", glfwextensions[i]);
    }
    #ifdef __APPLE__
    extensions.push_back("VK_KHR_portability_enumeration");
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); //Apple silicon extensions
    #endif
    //layers.push_back("VK_LAYER_KHRONOS_validation");
    
    vk::InstanceCreateInfo createinfo = vk::InstanceCreateInfo(
        vk::InstanceCreateFlags(VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR), 
        &appinfo,layers.size(), layers.data(), 
        extensions.size(), extensions.data());
    vk::Instance instance = vk::createInstance(createinfo);
    
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

    uint32_t count = 0;
    VkResult res = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (res != VK_SUCCESS)
    {
        std::println("Enumerating physical devices failed!");
        return -1;
    }
    std::println("Physical devices available {}", count);
    std::vector<VkPhysicalDevice> devices;
    devices.resize(count);
    VkResult res2 = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (res2 != VK_SUCCESS)
    {
        std::println("Enumerating physical devices failed!");
        return -1;
    }

    vk::PhysicalDevice selected_physical_device;
    for (auto device: devices)
    {
        VkPhysicalDeviceProperties dev_prop = {};
        vkGetPhysicalDeviceProperties(device, &dev_prop);
        std::println("{}", dev_prop.deviceName);
        selected_physical_device = device;
    }

    std::vector<vk::QueueFamilyProperties> queue_families = selected_physical_device.getQueueFamilyProperties();

    uint32_t graphics_queue_index = 0;

    for (uint32_t i = 0; i < queue_families.size(); i++)
    {
        if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics)
        {
            graphics_queue_index = i;
            break;
        }
    }
    float queue_priority = 1.0f;
    vk::DeviceQueueCreateInfo queue_info = vk::DeviceQueueCreateInfo(vk::DeviceQueueCreateFlags(), graphics_queue_index, 1, &queue_priority);


    uint32_t device_extension_count = 2;
    char *device_extension_names[device_extension_count];
    device_extension_names[0] = (char *)"VK_KHR_swapchain";
    device_extension_names[1] = (char *)"VK_EXT_shader_object";
    vk::PhysicalDeviceFeatures device_features = vk::PhysicalDeviceFeatures();
    vk::PhysicalDeviceShaderObjectFeaturesEXT shader_features = vk::PhysicalDeviceShaderObjectFeaturesEXT(true);

    vk::DeviceCreateInfo device_info = vk::DeviceCreateInfo(vk::DeviceCreateFlags(), 1, &queue_info, 0, nullptr, device_extension_count, device_extension_names, &device_features);
    device_info.pNext = &shader_features;

    vk::Device device = selected_physical_device.createDevice(device_info);

    auto dldi = vk::detail::DispatchLoaderDynamic(instance, pfnVkGetInstanceProcAddr, device);

    VkSurfaceKHR raw_surface;
    glfwCreateWindowSurface(instance, window, nullptr, &raw_surface);
    vk::SurfaceKHR surface = raw_surface;

    VkBool32 surface_supported = selected_physical_device.getSurfaceSupportKHR(graphics_queue_index, surface);

    if (surface_supported == VK_TRUE)
        std::println("Surface supported!");
    else
        std::println("Surface unsupported!");

    vk::SurfaceCapabilitiesKHR surface_capabilities = selected_physical_device.getSurfaceCapabilitiesKHR(surface);
    int width = 0;
    int height = 0;
    if (surface_capabilities.currentExtent.height == UINT32_MAX || surface_capabilities.currentExtent.width == UINT32_MAX)
    {
        glfwGetFramebufferSize(window, &width, &height);
        width = std::clamp((uint32_t)width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
        height = std::clamp((uint32_t)height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
    }
    else
    {
        width = surface_capabilities.currentExtent.width;
        height = surface_capabilities.currentExtent.height;
    }
    vk::Extent2D framebuffer_extension = vk::Extent2D(width, height);
    std::println("Extents width {} height {}", width, height);

    std::vector<vk::SurfaceFormatKHR> surface_formats = selected_physical_device.getSurfaceFormatsKHR(surface);

    vk::SurfaceFormatKHR format;
    for (auto form: surface_formats)
    {
        if (form.format == vk::Format::eB8G8R8A8Srgb && form.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            format = form;
            break;
        }
    }

    std::vector<vk::PresentModeKHR> present_modes = selected_physical_device.getSurfacePresentModesKHR(surface);
    vk::PresentModeKHR mode = vk::PresentModeKHR::eFifo;
    if (std::find(present_modes.begin(), present_modes.end(), vk::PresentModeKHR::eFifoRelaxed) != present_modes.end())
    {
        mode = vk::PresentModeKHR::eFifoRelaxed;
        std::println("Selected relaxed FIFO");
    }
    
    vk::SwapchainCreateInfoKHR swapchain_info = vk::SwapchainCreateInfoKHR(vk::SwapchainCreateFlagsKHR(), surface, 2, format.format, format.colorSpace, framebuffer_extension,
        1, vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive);
    swapchain_info.preTransform = surface_capabilities.currentTransform;
    swapchain_info.presentMode = mode;
    swapchain_info.clipped = VK_TRUE;
    swapchain_info.oldSwapchain = vk::SwapchainKHR(nullptr);
    swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;


    vk::SwapchainKHR swapchain = device.createSwapchainKHR(swapchain_info);

    std::vector<vk::Image> images = device.getSwapchainImagesKHR(swapchain);
    std::println("Got {} images from swapchain", images.size());

    std::vector<vk::ImageView> image_views;
    for (auto &image: images)
    {
        vk::ImageViewCreateInfo image_view_info = {};
        image_view_info.image = image;
        image_view_info.viewType = vk::ImageViewType::e2D;
        image_view_info.format = format.format;
        image_view_info.components.r = vk::ComponentSwizzle::eIdentity;
        image_view_info.components.g = vk::ComponentSwizzle::eIdentity;
        image_view_info.components.b = vk::ComponentSwizzle::eIdentity;
        image_view_info.components.a = vk::ComponentSwizzle::eIdentity;
        image_view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        image_view_info.subresourceRange.baseMipLevel = 0;
        image_view_info.subresourceRange.levelCount = 1;
        image_view_info.subresourceRange.baseArrayLayer = 0;
        image_view_info.subresourceRange.layerCount = 1;

        image_views.push_back(device.createImageView(image_view_info));
    }


    std::vector<char> vertex_source = read_file("../shaders/vertex.spv");
    vk::ShaderCreateInfoEXT vertex_info = {};
    vertex_info.setFlags(vk::ShaderCreateFlagsEXT(vk::ShaderCreateFlagBitsEXT::eLinkStage));
    vertex_info.setStage(vk::ShaderStageFlagBits::eVertex);
    vertex_info.setNextStage(vk::ShaderStageFlagBits::eFragment);
    vertex_info.setCodeType(vk::ShaderCodeTypeEXT::eSpirv);
    vertex_info.setCodeSize(vertex_source.size());
    vertex_info.setPCode(vertex_source.data());
    vertex_info.setPName("main");

    std::vector<char> fragment_source = read_file("../shaders/fragment.spv");
    vk::ShaderCreateInfoEXT fragment_info = {};
    fragment_info.setFlags(vk::ShaderCreateFlagsEXT(vk::ShaderCreateFlagBitsEXT::eLinkStage));
    fragment_info.setStage(vk::ShaderStageFlagBits::eFragment);
    fragment_info.setCodeType(vk::ShaderCodeTypeEXT::eSpirv);
    fragment_info.setCodeSize(fragment_source.size());
    fragment_info.setPCode(fragment_source.data());
    fragment_info.setPName("main");

    std::vector<vk::ShaderCreateInfoEXT> shader_infos;
    shader_infos.push_back(vertex_info);
    shader_infos.push_back(fragment_info);

    auto shader_result = device.createShadersEXT(shader_infos, nullptr, dldi);
    std::vector<vk::ShaderEXT> shaders;
    if (shader_result.result == vk::Result::eSuccess)
    {
        shaders = shader_result.value;
        std::println("Shaders succesfully loaded!");
    }
    
    while(!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }
    device.destroySwapchainKHR(swapchain);
    instance.destroySurfaceKHR(surface);
    device.destroy();
    instance.destroy();
    glfwTerminate();
    return 0;
}
