#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <print>
#include <vector>
#include <fstream>

std::vector<char> read_file(const char *filename)
{
    std::ifstream file(filename, std::ios::binary);
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


    uint32_t device_extension_count = 1;
    char *device_extension_names[device_extension_count];
    device_extension_names[0] = (char *)"VK_KHR_swapchain";
    vk::PhysicalDeviceFeatures device_features = vk::PhysicalDeviceFeatures();

    vk::DeviceCreateInfo device_info = vk::DeviceCreateInfo(vk::DeviceCreateFlags(), 1, &queue_info, 0, nullptr, 1, device_extension_names, &device_features);

    vk::Device device = selected_physical_device.createDevice(device_info);

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

    
    std::vector<char> vertex_shader = read_file("../shaders/vertex.spv");
    std::vector<char> fragment_shader = read_file("../shaders/fragment.spv");
    
    vk::ShaderModuleCreateInfo vertex_shader_info = vk::ShaderModuleCreateInfo(vk::ShaderModuleCreateFlags(), vertex_shader.size());
    vertex_shader_info.pCode = (const uint32_t*)(vertex_shader.data());

    vk::ShaderModuleCreateInfo fragment_shader_info = vk::ShaderModuleCreateInfo(vk::ShaderModuleCreateFlags(), fragment_shader.size());
    fragment_shader_info.pCode = (const uint32_t*)(fragment_shader.data());

    vk::ShaderModule vertex_module = device.createShaderModule(vertex_shader_info);
    vk::ShaderModule fragment_module = device.createShaderModule(fragment_shader_info);

    vk::PipelineShaderStageCreateInfo vertex_stage_info = {};
    vertex_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
    vertex_stage_info.module = vertex_module;
    vertex_stage_info.pName = "main";

    vk::PipelineShaderStageCreateInfo fragment_stage_info = {};
    fragment_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
    fragment_stage_info.module = fragment_module;
    fragment_stage_info.pName = "main";

    std::vector<vk::PipelineShaderStageCreateInfo> pipeline_shaders = {vertex_stage_info, fragment_stage_info};

    vk::PipelineVertexInputStateCreateInfo vertex_input_info = {};
    vk::PipelineInputAssemblyStateCreateInfo input_assembly_info = vk::PipelineInputAssemblyStateCreateInfo(vk::PipelineInputAssemblyStateCreateFlags(), 
                                                                                                            vk::PrimitiveTopology::eTriangleList, VK_FALSE);
    vk::Viewport viewport = vk::Viewport(0.0f, 0.0f, 
                                        framebuffer_extension.width, framebuffer_extension.height,
                                        0.0f, 1.0f);
    vk::Rect2D scissor;
    scissor.setOffset({0, 0});
    scissor.extent = framebuffer_extension;

    vk::PipelineViewportStateCreateInfo viewport_info = vk::PipelineViewportStateCreateInfo(vk::PipelineViewportStateCreateFlags(), 
                                                                                            1, &viewport, 1, &scissor);
    vk::PipelineRasterizationStateCreateInfo raster_info = {};
    raster_info.depthClampEnable = VK_FALSE;
    raster_info.polygonMode = vk::PolygonMode::eFill;
    raster_info.lineWidth = 1.0f;
    raster_info.cullMode = vk::CullModeFlagBits::eBack;
    raster_info.frontFace = vk::FrontFace::eClockwise;
    raster_info.depthBiasEnable = VK_FALSE;

    vk::PipelineMultisampleStateCreateInfo multisampling_info = {};
    multisampling_info.sampleShadingEnable = VK_FALSE;
    multisampling_info.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState color_blend_attachment = vk::PipelineColorBlendAttachmentState(VK_FALSE);
    vk::PipelineColorBlendStateCreateInfo color_blend_info = {};
    color_blend_info.logicOpEnable = VK_FALSE;
    color_blend_info.attachmentCount = 1;
    color_blend_info.pAttachments = &color_blend_attachment;

    vk::PipelineLayoutCreateInfo layout_info = {};
    vk::PipelineLayout pipeline_layout = device.createPipelineLayout(layout_info);


    vk::AttachmentDescription color_attachment = vk::AttachmentDescription(vk::AttachmentDescriptionFlags(), 
                                                                            format.format, vk::SampleCountFlagBits::e1,
                                                                            vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
                                                                            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                                                            vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
    vk::AttachmentReference attachment_ref = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);
    vk::SubpassDescription subpass_description = {};
    subpass_description.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass_description.colorAttachmentCount = 1;
    subpass_description.pColorAttachments = &attachment_ref;

    vk::RenderPassCreateInfo render_pass_info = {};
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass_description;

    vk::RenderPass render_pass = device.createRenderPass(render_pass_info);

    vk::GraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = pipeline_shaders.data();
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly_info;
    pipeline_info.pViewportState = &viewport_info;
    pipeline_info.pRasterizationState = &raster_info;
    pipeline_info.pMultisampleState = &multisampling_info;
    pipeline_info.pColorBlendState = &color_blend_info;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    auto pipeline_result = device.createGraphicsPipeline(VK_NULL_HANDLE, pipeline_info);

    if (pipeline_result.result != vk::Result::eSuccess)
    {
        std::println("Pipeline creation failed!");
        return -1;
    }
    vk::Pipeline pipeline = pipeline_result.value;
    std::println("Pipeline creation success!");

    while(!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    device.destroyPipeline(pipeline);
    device.destroyRenderPass(render_pass);
    device.destroyPipelineLayout(pipeline_layout);
    device.destroyShaderModule(vertex_module);
    device.destroyShaderModule(fragment_module);
    for (auto &image: image_views)
    {
        device.destroyImageView(image);
    }
    device.destroySwapchainKHR(swapchain);
    instance.destroySurfaceKHR(surface);
    device.destroy();
    instance.destroy();
    glfwTerminate();
    return 0;
}
