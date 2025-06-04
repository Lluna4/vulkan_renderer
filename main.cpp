#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <print>
#include <filesystem>
#include <vector>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <atomic>
#include <thread>

bool skip_rendering = false;
vk::SurfaceFormatKHR format;
vk::Extent2D framebuffer_extension;

std::vector<glm::vec2> identity_mat_2d = {{1, 0}, {0,1}};

struct vertex
{
    glm::vec2 position;
    glm::vec3 color;
};

struct vertex_3d
{
    glm::vec3 position;
    glm::vec3 color;
};

struct bounding_box
{
    float x;
    float y;
    float width;
    float height;
};

struct uniform
{
    glm::mat4 transform;
};

bounding_box player;
float velocityX = 1.0f;
float velocityY = 0.50f;
std::atomic_bool thread = true;

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

vk::PhysicalDevice select_physical_device(std::vector<vk::PhysicalDevice> devices)
{
    if (devices.size() == 1)
        return devices[0];
    
    vk::PhysicalDevice selected_physical_device;
    for (auto device: devices)
    {
        vk::PhysicalDeviceProperties device_propierties = device.getProperties();
        if (device_propierties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        {
            selected_physical_device = device;
            break;
        }
        selected_physical_device = device;
    }
    return selected_physical_device; 
}

vk::SwapchainKHR create_swapchain(vk::PhysicalDevice phy_device, vk::SurfaceKHR surface, GLFWwindow *window, vk::Device device)
{
    vk::SurfaceCapabilitiesKHR surface_capabilities = phy_device.getSurfaceCapabilitiesKHR(surface);
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
    
    
    if (width == 0 && height == 0)
    {
        skip_rendering = true;
    }
    framebuffer_extension = vk::Extent2D(width, height);
    std::println("Extents width {} height {}", width, height);

    std::vector<vk::SurfaceFormatKHR> surface_formats = phy_device.getSurfaceFormatsKHR(surface);

    for (auto form: surface_formats)
    {
        if (form.format == vk::Format::eB8G8R8A8Srgb && form.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            format = form;
            break;
        }
    }

    std::vector<vk::PresentModeKHR> present_modes = phy_device.getSurfacePresentModesKHR(surface);
    vk::PresentModeKHR mode = vk::PresentModeKHR::eFifo;
    if (std::find(present_modes.begin(), present_modes.end(), vk::PresentModeKHR::eFifoRelaxed) != present_modes.end())
    {
        mode = vk::PresentModeKHR::eFifoRelaxed;
        std::println("Selected relaxed FIFO");
    }
    else 
    {
        std::println("Selected FIFO");
    }
    
    vk::SwapchainCreateInfoKHR swapchain_info = vk::SwapchainCreateInfoKHR(vk::SwapchainCreateFlagsKHR(), surface, 2, format.format, format.colorSpace, framebuffer_extension,
        1, vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive);
    swapchain_info.preTransform = surface_capabilities.currentTransform;
    swapchain_info.presentMode = mode;
    swapchain_info.clipped = VK_TRUE;
    swapchain_info.oldSwapchain = vk::SwapchainKHR(nullptr);
    swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;


    return device.createSwapchainKHR(swapchain_info);
}

void compile_shader(const char *filename)
{
    system(std::format("glslc {} -o {}.spv", filename, filename).c_str());
}

std::vector<vertex> convert_quad_to_triangles(std::vector<vertex> vertices)
{
    const vertex end_vertex = vertices[3];
    vertices.pop_back();
    vertices.push_back(vertices[0]);
    vertices.push_back(vertices[2]);
    vertices.push_back(end_vertex);
    return vertices;
}

std::vector<vertex_3d> convert_quad_to_triangles(std::vector<vertex_3d> vertices)
{
    const vertex_3d end_vertex = vertices[3];
    vertices.pop_back();
    vertices.push_back(vertices[0]);
    vertices.push_back(vertices[2]);
    vertices.push_back(end_vertex);
    return vertices;
}

glm::mat4 rotate(float angle)
{
    float c = glm::cos(glm::radians(angle));
    float s = glm::sin(glm::radians(angle));
    glm::mat4 transform({c, s, 0.0f, 0.0f}, {-s, c, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f});
    return transform;
}

glm::mat2 scale(glm::vec2 scale)
{
    glm::mat2 transform({scale.x, 0.0f}, {0.0f, scale.y});
    return transform;
}

glm::mat4 move(glm::vec2 pos)
{
    glm::mat4 transform({1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {pos.x, pos.y, 0.0f, 1.0f});
    return transform;
}

void simple_physics()
{
    while (thread.load() == true)
    {
        bool collision_x = player.x + player.width / 2 >= 1.0f || player.x - player.width / 2 <= -1.0f;
        bool collision_y = player.y + player.height / 2 >=1.0f || player.y - player.height / 2 <= -1.0f;
        if (collision_x)
            velocityX = -velocityX;
        else if (collision_y)
            velocityY = -velocityY;
        player.x = player.x + (velocityX * 0.002f);
        player.y = player.y + (velocityY * 0.002f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void simple_physics_step(float t)
{
    bool collision_x = player.x + player.width / 2 >= 1.0f || player.x - player.width / 2 <= -1.0f;
    bool collision_y = player.y + player.height / 2 >=1.0f || player.y - player.height / 2 <= -1.0f;
    if (collision_x)
        velocityX = -velocityX;
    else if (collision_y)
        velocityY = -velocityY;
    player.x = player.x + (velocityX * t);
    player.y = player.y + (velocityY * t);
}

std::pair<vk::DeviceMemory, vk::Buffer> create_buffer(const vk::Device &device, vk::PhysicalDevice selected_physical_device, vk::BufferUsageFlagBits usage, size_t size)
{
    vk::BufferCreateInfo buffer_info = vk::BufferCreateInfo(vk::BufferCreateFlags(), size, usage, vk::SharingMode::eExclusive);
    vk::Buffer vertex_buffer = device.createBuffer(buffer_info);
    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device, vertex_buffer, &memory_requirements);
    vk::PhysicalDeviceMemoryProperties memory_properties = selected_physical_device.getMemoryProperties();

    int propierty_index = -1;
    for (int i = 0; i < memory_properties.memoryTypeCount; i++)
    {
        if (memory_properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlags(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))
        {
            propierty_index = i;
            break;
        }
    }
    if (propierty_index == -1)
    {
        throw std::runtime_error("Didnt find a suitable memory");
    }

    vk::MemoryAllocateInfo alloc_info = vk::MemoryAllocateInfo(memory_requirements.size, propierty_index);

    vk::DeviceMemory vertex_buffer_memory = device.allocateMemory(alloc_info);
    device.bindBufferMemory(vertex_buffer, vertex_buffer_memory, 0);
    return std::make_pair(vertex_buffer_memory, vertex_buffer);
}

std::pair<vk::DeviceMemory, vk::Image> create_image(vk::Device &device, size_t width, size_t height, size_t depth, vk::Format format, vk::ImageUsageFlags usage)
{
    vk::Extent3D extent(width, height, depth);
    vk::ImageCreateInfo image_create_info(vk::ImageCreateFlags(), vk::ImageType::e2D, format, extent, 1,
                                            1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
                                            usage);
    vk::Image image = device.createImage(image_create_info);

    vk::MemoryRequirements memory_requirements;
    device.getImageMemoryRequirements(image, &memory_requirements);
    vk::MemoryAllocateInfo memory_allocate_info(memory_requirements.size, 0x0);
    vk::DeviceMemory memory = device.allocateMemory(memory_allocate_info, nullptr);
    device.bindImageMemory(image, memory, 0);

    return std::make_pair(memory, image);
}

vk::ImageView create_image_view(vk::Device &device, vk::Image image, vk::Format format, vk::ImageAspectFlagBits aspect)
{
    vk::ImageViewCreateInfo image_view_info = {};
    image_view_info.image = image;
    image_view_info.viewType = vk::ImageViewType::e2D;
    image_view_info.format = format;
    image_view_info.components.r = vk::ComponentSwizzle::eIdentity;
    image_view_info.components.g = vk::ComponentSwizzle::eIdentity;
    image_view_info.components.b = vk::ComponentSwizzle::eIdentity;
    image_view_info.components.a = vk::ComponentSwizzle::eIdentity;
    image_view_info.subresourceRange.aspectMask = aspect;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;

    return device.createImageView(image_view_info);
}

void add_quad_to_vertices(std::vector<vertex_3d> &vertices, std::vector<vertex_3d> new_quad)
{
    new_quad = convert_quad_to_triangles(new_quad);

    for (auto vert: new_quad)
    {
        std::println("Vertex pos x: {}, y: {}, z: {}", vert.position.x, vert.position.y, vert.position.z);
        vertices.push_back(vert);
    }
}


int main()
{
    using clock = std::chrono::system_clock;
    using ms = std::chrono::duration<double, std::milli>;
    GLFWwindow *window = create_window(1000, 800, "hello");

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
    #ifndef NDEBUG
    layers.push_back("VK_LAYER_KHRONOS_validation");
    #endif
    
    vk::InstanceCreateInfo createinfo = vk::InstanceCreateInfo(
        vk::InstanceCreateFlags(VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR), 
        &appinfo,layers.size(), layers.data(), 
        extensions.size(), extensions.data());
    vk::Instance instance = vk::createInstance(createinfo);

    std::vector<vk::PhysicalDevice> devices = instance.enumeratePhysicalDevices();
    vk::PhysicalDevice selected_physical_device = select_physical_device(devices);

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

    std::vector<const char *> device_extensions;

    device_extensions.push_back("VK_KHR_swapchain");
    #ifdef __APPLE__
    device_extensions.push_back("VK_KHR_portability_subset");
    #endif
    vk::PhysicalDeviceFeatures device_features = vk::PhysicalDeviceFeatures();

    vk::DeviceCreateInfo device_info = vk::DeviceCreateInfo(vk::DeviceCreateFlags(), 1, &queue_info, 0, nullptr, device_extensions.size(), device_extensions.data(), &device_features);

    vk::Device device = selected_physical_device.createDevice(device_info);

    vk::Queue graphics_queue = device.getQueue(graphics_queue_index, 0);

    VkSurfaceKHR raw_surface;
    glfwCreateWindowSurface(instance, window, nullptr, &raw_surface);
    vk::SurfaceKHR surface = raw_surface;

    VkBool32 surface_supported = selected_physical_device.getSurfaceSupportKHR(graphics_queue_index, surface);

    if (surface_supported == VK_TRUE)
        std::println("Surface supported!");
    else
        std::println("Surface unsupported!");

    
    vk::SwapchainKHR swapchain = create_swapchain(selected_physical_device, surface, window, device);
    
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
    if (!std::filesystem::exists("../shaders/vertex.vert.spv") && !std::filesystem::exists("../shaders/fragment.frag.spv"))
    {    
        std::println("Compiling shaders");
        compile_shader("../shaders/vertex.vert");
        compile_shader("../shaders/fragment.frag");
        std::println("Shaders compiled");
    }
    std::vector<char> vertex_shader = read_file("../shaders/vertex.vert.spv");
    std::vector<char> fragment_shader = read_file("../shaders/fragment.frag.spv");
    
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

    vk::VertexInputBindingDescription binding_description = {};
    binding_description.binding = 0;
    binding_description.stride = sizeof(vertex_3d);
    binding_description.inputRate = vk::VertexInputRate::eVertex;

    vk::VertexInputAttributeDescription att_description_pos = {};
    att_description_pos.binding = 0;
    att_description_pos.location = 0;
    att_description_pos.format = vk::Format::eR32G32B32Sfloat;
    att_description_pos.offset = offsetof(vertex_3d, position);

    vk::VertexInputAttributeDescription att_description_color = {};
    att_description_color.binding = 0;
    att_description_color.location = 1;
    att_description_color.format = vk::Format::eR32G32B32Sfloat;
    att_description_color.offset = offsetof(vertex_3d, color);

    std::vector<vk::VertexInputAttributeDescription> att_descriptions = {att_description_pos, att_description_color};
    vk::PipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.vertexAttributeDescriptionCount = 2;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_info.pVertexAttributeDescriptions = att_descriptions.data();


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
    raster_info.cullMode = vk::CullModeFlagBits::eNone;
    raster_info.frontFace = vk::FrontFace::eClockwise;
    raster_info.depthBiasEnable = VK_FALSE;

    vk::PipelineMultisampleStateCreateInfo multisampling_info = {};
    multisampling_info.sampleShadingEnable = VK_FALSE;
    multisampling_info.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState color_blend_attachment = vk::PipelineColorBlendAttachmentState(VK_FALSE);
    vk::PipelineColorBlendStateCreateInfo color_blend_info = {};
    color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    color_blend_info.logicOpEnable = VK_FALSE;
    color_blend_info.attachmentCount = 1;
    color_blend_info.pAttachments = &color_blend_attachment;

    vk::DescriptorSetLayoutBinding descriptor_binding;
    descriptor_binding.binding = 0;
    descriptor_binding.descriptorCount = 1;
    descriptor_binding.descriptorType = vk::DescriptorType::eUniformBuffer;
    descriptor_binding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutCreateInfo descriptor_layout_info(vk::DescriptorSetLayoutCreateFlags(), 1, &descriptor_binding);
    vk::DescriptorSetLayout descriptor_layout = device.createDescriptorSetLayout(descriptor_layout_info);

    vk::PipelineLayoutCreateInfo layout_info = {};
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &descriptor_layout;
    vk::PipelineLayout pipeline_layout = device.createPipelineLayout(layout_info);

    uniform u{};
    u.transform = glm::mat4{1.0f};
    auto rec = create_buffer(device, selected_physical_device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(uniform));
    vk::DeviceMemory uniform_buffer_data = rec.first;
    vk::Buffer uniform_buffer = rec.second;
    char *uniform_data = (char *)device.mapMemory(uniform_buffer_data, 0, sizeof(uniform));
    memcpy(uniform_data, &u, sizeof(uniform));

    vk::AttachmentDescription color_attachment = vk::AttachmentDescription(vk::AttachmentDescriptionFlags(), 
                                                                            format.format, vk::SampleCountFlagBits::e1,
                                                                            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                                                            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                                                            vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
    vk::AttachmentReference attachment_ref = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);

    auto image_ret = create_image(device, framebuffer_extension.width, framebuffer_extension.height, 1,vk::Format::eD32Sfloat, vk::ImageUsageFlags(vk::ImageUsageFlagBits::eDepthStencilAttachment));
    vk::Image depth_image = image_ret.second;
    vk::DeviceMemory depht_image_memory = image_ret.first;
    vk::ImageView depth_image_view = create_image_view(device, depth_image, vk::Format::eD32Sfloat, vk::ImageAspectFlagBits::eDepth);
    vk::AttachmentDescription depth_attachment_desc(vk::AttachmentDescriptionFlags(), vk::Format::eD32Sfloat,
        vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthStencilAttachmentOptimal);
    vk::AttachmentReference depth_attachment_ref(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::SubpassDescription subpass_description = {};
    subpass_description.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass_description.colorAttachmentCount = 1;
    subpass_description.pColorAttachments = &attachment_ref;
    subpass_description.pDepthStencilAttachment = &depth_attachment_ref;

    vk::SubpassDependency subpass_dependency = {};
    subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpass_dependency.dstSubpass = 0;
    subpass_dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    subpass_dependency.dstStageMask = vk::PipelineStageFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests);
    subpass_dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    std::vector<vk::AttachmentDescription> attachments = {color_attachment, depth_attachment_desc};
    vk::RenderPassCreateInfo render_pass_info = {};
    render_pass_info.attachmentCount = attachments.size();
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass_description;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &subpass_dependency;

    vk::RenderPass render_pass = device.createRenderPass(render_pass_info);

    vk::PipelineDepthStencilStateCreateInfo depht_stencil_info;
    depht_stencil_info.depthTestEnable = VK_TRUE;
    depht_stencil_info.depthWriteEnable = VK_TRUE;
    depht_stencil_info.depthCompareOp = vk::CompareOp::eLessOrEqual;
    depht_stencil_info.depthBoundsTestEnable = VK_FALSE;
    depht_stencil_info.stencilTestEnable = VK_FALSE;

    vk::GraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = pipeline_shaders.data();
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly_info;
    pipeline_info.pViewportState = &viewport_info;
    pipeline_info.pRasterizationState = &raster_info;
    pipeline_info.pMultisampleState = &multisampling_info;
    pipeline_info.pColorBlendState = &color_blend_info;
    pipeline_info.pDepthStencilState = &depht_stencil_info;
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


    std::vector<vk::Framebuffer> framebuffers;

    for (auto &view: image_views)
    {
        std::vector<vk::ImageView> view_attachments = {view, depth_image_view};
        vk::FramebufferCreateInfo framebuffer_info = vk::FramebufferCreateInfo(vk::FramebufferCreateFlags(),
                                                                        render_pass, view, framebuffer_extension.width,
                                                                        framebuffer_extension.height, 1);
        framebuffer_info.attachmentCount = view_attachments.size();
        framebuffer_info.pAttachments = view_attachments.data();
        framebuffers.push_back(device.createFramebuffer(framebuffer_info));
    }

    std::vector<vertex_3d> vertices;

    std::vector<vertex_3d> front_quad
    {
        {{-0.2f, -0.2f,  0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.0f, -0.2f,  0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.0f,  0.0f,  0.0f}, {1.0f, 0.0f, 0.0f}},
        {{-0.2f,  0.0f,  0.0f}, {1.0f, 0.0f, 0.0f}}
    };
    add_quad_to_vertices(vertices, front_quad);

    std::vector<vertex_3d> back_quad
    {
            {{-0.15f, -0.15f,  0.2f}, {1.0f, 0.0f, 1.0f}},
            {{ 0.05f, -0.15f,  0.2f}, {1.0f, 0.0f, 1.0f}},
            {{ 0.05f,  0.05f,  0.2f}, {1.0f, 0.0f, 1.0f}},
            {{-0.15f,  0.05f,  0.2f}, {1.0f, 0.0f, 1.0f}}
    };
    add_quad_to_vertices(vertices, back_quad);

    std::vector<vertex_3d> right_quad
    {
                {{0.0f, -0.2f,  0.0f}, {0.0f, 1.0f, 0.0f}},
                {{ 0.05f, -0.15f,  0.2f}, {0.0f, 1.0f, 0.0f}},
                {{ 0.05f,  0.05f,  0.2f}, {0.0f, 1.0f, 0.0f}},
                {{0.0f,  0.0f,  0.0f}, {0.0f, 1.0f, 0.0f}}
    };
    add_quad_to_vertices(vertices, right_quad);

    std::vector<vertex_3d> bottom_quad
    {
            {front_quad[3].position, {1.0f, 1.0f, 0.0f}},
            {front_quad[2].position, {1.0f, 1.0f, 0.0f}},
            {back_quad[2].position, {1.0f, 1.0f, 0.0f}},
            {back_quad[3].position, {1.0f, 1.0f, 0.0f}}
    };
    std::println("Bottom quad");
    add_quad_to_vertices(vertices, bottom_quad);

    auto ret = create_buffer(device, selected_physical_device, vk::BufferUsageFlagBits::eVertexBuffer, sizeof(vertices[0]) * vertices.size());
    vk::DeviceMemory vertex_buffer_memory = ret.first;
    vk::Buffer vertex_buffer = ret.second;
    char *data = (char *)device.mapMemory(vertex_buffer_memory, 0, sizeof(vertices[0]) * vertices.size());
    memcpy(data, vertices.data(), sizeof(vertices[0]) * vertices.size());
    

    vk::CommandPoolCreateInfo command_pool_info = {};
    command_pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    command_pool_info.queueFamilyIndex = graphics_queue_index;
    vk::CommandPool command_pool = device.createCommandPool(command_pool_info);

    vk::CommandBufferAllocateInfo cmd_alloc_info = vk::CommandBufferAllocateInfo(command_pool, 
                                                                                vk::CommandBufferLevel::ePrimary,
                                                                                1);
    auto command_buffers = device.allocateCommandBuffers(cmd_alloc_info);

    vk::DescriptorPoolSize descriptor_pool_size(vk::DescriptorType::eUniformBuffer, sizeof(uniform));
    vk::DescriptorPoolCreateInfo descriptor_pool_info;
    descriptor_pool_info.maxSets = 1;
    descriptor_pool_info.poolSizeCount = 1;
    descriptor_pool_info.pPoolSizes = &descriptor_pool_size;

    vk::DescriptorPool descriptor_pool = device.createDescriptorPool(descriptor_pool_info);

    vk::DescriptorSetAllocateInfo descriptor_set_allocate_info;
    descriptor_set_allocate_info.descriptorPool = descriptor_pool;
    descriptor_set_allocate_info.descriptorSetCount = 1;
    descriptor_set_allocate_info.pSetLayouts = &descriptor_layout;

    auto descriptor_sets = device.allocateDescriptorSets(descriptor_set_allocate_info);
    vk::DescriptorSet descriptor_set = descriptor_sets[0];

    vk::DescriptorBufferInfo descriptor_buffer_info;
    descriptor_buffer_info.buffer = uniform_buffer;
    descriptor_buffer_info.offset = 0;
    descriptor_buffer_info.range = sizeof(uniform);

    vk::WriteDescriptorSet write_descriptor;
    write_descriptor.descriptorCount = 1;
    write_descriptor.descriptorType = vk::DescriptorType::eUniformBuffer;
    write_descriptor.dstBinding = 0;
    write_descriptor.dstArrayElement = 0;
    write_descriptor.dstSet = descriptor_set;
    write_descriptor.pBufferInfo = &descriptor_buffer_info;
    device.updateDescriptorSets(write_descriptor, nullptr);

    vk::SemaphoreCreateInfo semaphore_info = vk::SemaphoreCreateInfo();
    vk::FenceCreateInfo fence_info = vk::FenceCreateInfo();
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;
    vk::Semaphore image_semaphore = device.createSemaphore(semaphore_info);
    vk::Semaphore render_semaphore = device.createSemaphore(semaphore_info);
    vk::Fence next_frame_fence = device.createFence(fence_info);
    player.x = 0.0f;
    player.y = 0.0f;
    player.height = 0.2f;
    player.width = 0.2f;
    //velocityX = 0.008f;
    std::vector<vertex_3d> render_vertices = vertices;
    float angle = 0.0f;
    float vel2 = 0.005f;
    //std::thread phy_thread(simple_physics);
    auto before = clock::now();
    while(!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        auto res_wait = device.waitForFences(next_frame_fence, VK_TRUE, UINT64_MAX);
        if (res_wait != vk::Result::eSuccess)
            throw std::runtime_error("failed waiting!");
        device.resetFences(next_frame_fence);
        if (skip_rendering)
            continue;
        auto image_result = device.acquireNextImageKHR(swapchain, UINT64_MAX, image_semaphore);
        if (image_result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Getting next image failed!");
        }
        uint32_t image_index = image_result.value;
        vkResetCommandBuffer(command_buffers[0], 0);
        auto time_elapsed = clock::now() - before;
        simple_physics_step(std::chrono::duration_cast<std::chrono::duration<float>>(time_elapsed).count());
        before = clock::now();

        //u.transform = glm::rotate(u.transform, 0.01f, glm::vec3(1.0f, 0.0f, 1.0f));
        //u.transform = move({player.x, player.y}) * rotate(angle);
        //memcpy(uniform_data, &u, sizeof(uniform));

        angle -= 1.0f;
        memcpy(data, render_vertices.data(), sizeof(vertices[0]) * vertices.size());


        vk::CommandBufferBeginInfo begin_info = {};
        std::array<vk::ClearValue, 2> clear_values;
        clear_values[0].color = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        clear_values[1].depthStencil = {{1.0f, 0}};
        vk::Rect2D render_area = {{0, 0}, framebuffer_extension};
        vk::RenderPassBeginInfo render_pass_begin = vk::RenderPassBeginInfo(render_pass, framebuffers[image_index],
                                                                            render_area, clear_values.size(), clear_values.data());
        vk::DeviceSize offset = 0;
        command_buffers[0].begin(begin_info);
        command_buffers[0].beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
        command_buffers[0].bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        //command_buffers[0].setViewport(0, viewport);
        //command_buffers[0].setScissor(0, scissor);
        command_buffers[0].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, descriptor_sets, nullptr);
        command_buffers[0].bindVertexBuffers(0, 1, &vertex_buffer, &offset);
        command_buffers[0].draw(vertices.size(), 1, 0, 0);
        command_buffers[0].endRenderPass();
        if (vkEndCommandBuffer(command_buffers[0]) != VK_SUCCESS)
        {
            throw std::runtime_error("Command buffer creation failed!");
        }

        vk::PipelineStageFlags flags =  vk::PipelineStageFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        vk::SubmitInfo submit_info = vk::SubmitInfo();
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &image_semaphore;
        submit_info.pWaitDstStageMask = &flags;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_semaphore;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffers[0];

        graphics_queue.submit(submit_info, next_frame_fence);

        vk::PresentInfoKHR present_info = {};
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_semaphore;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain;
        present_info.pImageIndices = &image_index;

        auto present_result = graphics_queue.presentKHR(present_info);
        if (present_result != vk::Result::eSuccess)
            throw std::runtime_error("Presenting to the graphics queue failed");
    }
    thread = false;
    //phy_thread.join();
    device.unmapMemory(vertex_buffer_memory);
    device.waitIdle();
    device.freeMemory(uniform_buffer_data);
    device.freeMemory(depht_image_memory);
    device.destroyBuffer(uniform_buffer);
    device.destroyImageView(depth_image_view);
    device.destroyImage(depth_image);
    device.destroyDescriptorPool(descriptor_pool);
    device.destroyDescriptorSetLayout(descriptor_layout);
    device.destroyBuffer(vertex_buffer);
    device.freeMemory(vertex_buffer_memory);
    for (auto &framebuffer: framebuffers)
    {
        device.destroyFramebuffer(framebuffer);
    }
    device.destroyFence(next_frame_fence);
    device.destroySemaphore(render_semaphore);
    device.destroySemaphore(image_semaphore);
    device.destroyCommandPool(command_pool);
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
