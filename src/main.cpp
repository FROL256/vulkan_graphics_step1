#include <vulkan/vulkan.h>

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cassert>

#include "vk_utils.h"
#include "Bitmap.h"

const int WIDTH  = 800;
const int HEIGHT = 600;

const int MAX_FRAMES_IN_FLIGHT = 2;

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

const VkFormat FRAMEBUFFER_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

class HelloTriangleApplication 
{
public:

  void run() 
  {
    InitVulkan();
    InitResources();
    
    RenderImageAndSaveItToFile();
    
    Cleanup();
  }

private:

  VkInstance instance;
  std::vector<const char*> enabledLayers;

  VkDebugUtilsMessengerEXT debugMessenger;

  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device;

  VkQueue graphicsQueue;
  VkQueue transferQueue;

  VkRenderPass     renderPassOffscreen;
  VkFramebuffer    offFrameBufferObj;
  VkPipelineLayout pipelineLayout;
  VkPipeline       graphicsPipeline;

  VkCommandPool                commandPool, commandPoolTransfer;
  std::vector<VkCommandBuffer> commandBuffers;

  VkBuffer       m_vbo;     // we will store our vertices data here 
  VkDeviceMemory m_vboMem;  // 

  // offscreen rendering resources
  //
  VkImage         offImage;      // we will render to this image 
  VkDeviceMemory  offImageMem;   //
  VkImageView     offImageView;  // 

  VkBuffer        stagingBuff;   // we will copy rendered image to this buffer to we can read from it and save ".bmp" further
  VkDeviceMemory  stagingBuffMem;


  static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallbackFn(
    VkDebugReportFlagsEXT                       flags,
    VkDebugReportObjectTypeEXT                  objectType,
    uint64_t                                    object,
    size_t                                      location,
    int32_t                                     messageCode,
    const char*                                 pLayerPrefix,
    const char*                                 pMessage,
    void*                                       pUserData)
  {
    printf("[Debug Report]: %s: %s\n", pLayerPrefix, pMessage);
    return VK_FALSE;
  }

  VkDebugReportCallbackEXT debugReportCallback;
  
  void InitVulkan() 
  {
    std::cout << "[InitVulkan]: begin ... " << std::endl;

    const int deviceId = 0;

    instance = vk_utils::CreateInstance(enableValidationLayers, enabledLayers);
    if (enableValidationLayers)
      vk_utils::InitDebugReportCallback(instance, &debugReportCallbackFn, &debugReportCallback);

  
    physicalDevice = vk_utils::FindPhysicalDevice(instance, true, deviceId);
    auto queueFID  = vk_utils::GetQueueFamilyIndex(physicalDevice, VK_QUEUE_GRAPHICS_BIT);
    auto queueTID  = vk_utils::GetQueueFamilyIndex(physicalDevice, VK_QUEUE_TRANSFER_BIT);


    device = vk_utils::CreateLogicalDevice(queueFID, physicalDevice, enabledLayers);
    vkGetDeviceQueue(device, queueFID, 0, &graphicsQueue);
    vkGetDeviceQueue(device, queueTID, 0, &transferQueue);
    
    // ==> commandPools
    {
      VkCommandPoolCreateInfo poolInfo = {};
      poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
      poolInfo.queueFamilyIndex = vk_utils::GetQueueFamilyIndex(physicalDevice, VK_QUEUE_GRAPHICS_BIT);

      if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        throw std::runtime_error("[CreateCommandPool]: failed to create graphics command pool!");

      poolInfo.queueFamilyIndex = vk_utils::GetQueueFamilyIndex(physicalDevice, VK_QUEUE_TRANSFER_BIT);
      if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPoolTransfer) != VK_SUCCESS)
        throw std::runtime_error("[CreateCommandPool]: failed to create transfer command pool!");
    }

    std::cout << "[InitVulkan]: end. " << std::endl;
  }

  void InitResources() 
  {
    std::cout << "[InitResources]: begin ... " << std::endl;

    CreateVertexBuffer(device, physicalDevice, 6*sizeof(float),
                       &m_vbo, &m_vboMem);

    //// create resources for offscreen rendering
    {
      CreateRenderPass(device, FRAMEBUFFER_FORMAT,
                       &renderPassOffscreen, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

      CreateTextureForRenderToIt(device, physicalDevice, WIDTH, HEIGHT,
                                 &offImage, &offImageMem, &offImageView);

      CreateFBO(device, renderPassOffscreen, offImageView, WIDTH, HEIGHT,
                &offFrameBufferObj);

      CreateStagingBuffer(device, physicalDevice, WIDTH*HEIGHT * sizeof(int),
                          &stagingBuff, &stagingBuffMem);
    }
    ///// \\\\ 

    CreateGraphicsPipeline(device, VkExtent2D{WIDTH, HEIGHT}, renderPassOffscreen,
                           &pipelineLayout, &graphicsPipeline);
    
    std::cout << "[InitResources]: end. " << std::endl;
  }

  void RenderImageAndSaveItToFile()
  {
    std::cout << "[RenderImageAndSaveItToFile]: updating vertex buffer ... " << std::endl;

    float trianglePos[] =
    {
      -0.5f, -0.5f,
      0.5f, -0.5f,
      0.0f, +0.5f,
    };

    PutTriangleVerticesToVBO_Now(device, commandPoolTransfer, graphicsQueue, trianglePos, 6 * 2,
                                 m_vbo);

    std::cout << "[RenderImageAndSaveItToFile]: rendering ... " << std::endl;

    RenderToTexture_Now(device, commandPool, graphicsQueue, graphicsPipeline, m_vbo,
                        offFrameBufferObj, VkExtent2D{ WIDTH, HEIGHT }, renderPassOffscreen);

    std::cout << "[RenderImageAndSaveItToFile]: copying ... " << std::endl;

    CopyTextureToBuffer_Now(device, commandPoolTransfer, transferQueue, offImage, WIDTH, HEIGHT,
                            stagingBuff);

    std::cout << "[RenderImageAndSaveItToFile]: saving ... " << std::endl;

    // Get data from stagingBuff and save it to file
    //
    {
      void *mappedMemory = nullptr;
      vkMapMemory(device, stagingBuffMem, 0, WIDTH * HEIGHT * sizeof(int), 0, &mappedMemory);
      SaveBMP("outimage.bmp", (const uint32_t*)mappedMemory, WIDTH, HEIGHT);
      vkUnmapMemory(device, stagingBuffMem);  // Done reading, so unmap.
    }

    std::cout << "[RenderImageAndSaveItToFile]: end. " << std::endl;
  }


  void Cleanup() 
  { 
    std::cout << "[Cleanup]: begin ... " << std::endl;

    // free our vbo
    vkDestroyBuffer(device, m_vbo, nullptr);
    vkFreeMemory(device, m_vboMem, nullptr);

    // destroy intermediate i. e. "staging" buffer
    vkDestroyBuffer(device, stagingBuff, nullptr);
    vkFreeMemory(device, stagingBuffMem, nullptr);

    // free our offscreen resources
    vkDestroyImageView(device, offImageView, nullptr);
    vkDestroyImage    (device, offImage, nullptr);
    vkFreeMemory      (device, offImageMem, nullptr);
    vkDestroyFramebuffer(device, offFrameBufferObj, nullptr);
    vkDestroyRenderPass (device, renderPassOffscreen, nullptr);

    if (enableValidationLayers)
    {
      // destroy callback.
      auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
      if (func == nullptr)
        throw std::runtime_error("Could not load vkDestroyDebugReportCallbackEXT");
      func(instance, debugReportCallback, NULL);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyCommandPool(device, commandPoolTransfer, nullptr);

    vkDestroyPipeline      (device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    std::cout << "[Cleanup]: end. " << std::endl;
  }

  static void CreateRenderPass(VkDevice a_device, VkFormat a_imageFormat,
                               VkRenderPass* a_pRenderPass, VkImageLayout a_finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
  {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format         = a_imageFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = a_finalLayout;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass  = {};
    subpass.pipelineBindPoint     = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount  = 1;
    subpass.pColorAttachments     = &colorAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments    = &colorAttachment;
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;

    if (vkCreateRenderPass(a_device, &renderPassInfo, nullptr, a_pRenderPass) != VK_SUCCESS)
      throw std::runtime_error("[CreateRenderPass]: failed to create render pass!");
  }


  static void CreateGraphicsPipeline(VkDevice a_device, VkExtent2D a_screenExtent, VkRenderPass a_renderPass,
                                     VkPipelineLayout* a_pLayout, VkPipeline* a_pPipiline)
  {
    auto vertShaderCode = vk_utils::ReadFile("shaders/vert.spv");
    auto fragShaderCode = vk_utils::ReadFile("shaders/frag.spv");

    VkShaderModule vertShaderModule = vk_utils::CreateShaderModule(a_device, vertShaderCode);
    VkShaderModule fragShaderModule = vk_utils::CreateShaderModule(a_device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName  = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName  = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };


    VkVertexInputBindingDescription vInputBinding = { };
    vInputBinding.binding   = 0;
    vInputBinding.stride    = sizeof(float) * 2;
    vInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vAttribute = {};
    vAttribute.binding  = 0;
    vAttribute.location = 0;
    vAttribute.format   = VK_FORMAT_R32G32_SFLOAT;
    vAttribute.offset   = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount   = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions      = &vInputBinding;
    vertexInputInfo.pVertexAttributeDescriptions    = &vAttribute;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = (float)a_screenExtent.width;
    viewport.height   = (float)a_screenExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = a_screenExtent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = VK_CULL_MODE_NONE; // VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace               = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable  = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable    = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable     = VK_FALSE;
    colorBlending.logicOp           = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount   = 1;
    colorBlending.pAttachments      = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount         = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(a_device, &pipelineLayoutInfo, nullptr, a_pLayout) != VK_SUCCESS)
      throw std::runtime_error("[CreateGraphicsPipeline]: failed to create pipeline layout!");

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = shaderStages;
    pipelineInfo.pVertexInputState   = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.layout              = (*a_pLayout);
    pipelineInfo.renderPass          = a_renderPass;
    pipelineInfo.subpass             = 0;
    pipelineInfo.basePipelineHandle  = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(a_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, a_pPipiline) != VK_SUCCESS)
      throw std::runtime_error("[CreateGraphicsPipeline]: failed to create graphics pipeline!");

    vkDestroyShaderModule(a_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(a_device, vertShaderModule, nullptr);
  }

  static void RenderToTexture_Now(VkDevice a_device, VkCommandPool a_cmdPool, VkQueue a_queue, VkPipeline a_graphicsPipeline, VkBuffer a_vPosBuffer,
                                  VkFramebuffer a_fbo, VkExtent2D a_frameBufferExtent, VkRenderPass a_renderPass)
  {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = a_cmdPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuff;
    if (vkAllocateCommandBuffers(a_device, &allocInfo, &cmdBuff) != VK_SUCCESS)
      throw std::runtime_error("[RenderToTexture_Now]: failed to allocate command buffer!");

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuff, &beginInfo);
    {
      VkRenderPassBeginInfo renderPassInfo = {};
      renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      renderPassInfo.renderPass        = a_renderPass;
      renderPassInfo.framebuffer       = a_fbo;
      renderPassInfo.renderArea.offset = { 0, 0 };
      renderPassInfo.renderArea.extent = a_frameBufferExtent;

      VkClearValue clearColor        = { 0.0f, 0.0f, 0.25f, 1.0f };
      renderPassInfo.clearValueCount = 1;
      renderPassInfo.pClearValues    = &clearColor;

      vkCmdBeginRenderPass(cmdBuff, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
      {
        vkCmdBindPipeline(cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, a_graphicsPipeline);

        // say we want to take vertices pos from a_vPosBuffer
        {
          VkBuffer vertexBuffers[] = { a_vPosBuffer };
          VkDeviceSize offsets[] = { 0 };
          vkCmdBindVertexBuffers(cmdBuff, 0, 1, vertexBuffers, offsets);
        }

        vkCmdDraw(cmdBuff, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmdBuff);
      }
    }
    vkEndCommandBuffer(cmdBuff);

    RunCommandBuffer(cmdBuff, a_queue, a_device);

    vkFreeCommandBuffers(a_device, a_cmdPool, 1, &cmdBuff);
  }


  void CopyTextureToBuffer_Now(VkDevice a_device, VkCommandPool a_cmdPool, VkQueue a_queue, VkImage a_image, int a_width, int a_height,
                               VkBuffer out_buffer)
  {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = a_cmdPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuff;
    if (vkAllocateCommandBuffers(a_device, &allocInfo, &cmdBuff) != VK_SUCCESS)
      throw std::runtime_error("[RenderToTexture_Now]: failed to allocate command buffer!");

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuff, &beginInfo);
    {
      VkImageSubresourceLayers shittylayers = {};
      shittylayers.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      shittylayers.mipLevel       = 0;
      shittylayers.baseArrayLayer = 0;
      shittylayers.layerCount     = 1;

      VkBufferImageCopy wholeRegion = {};
      wholeRegion.bufferOffset      = 0;
      wholeRegion.bufferRowLength   = uint32_t(a_width);
      wholeRegion.bufferImageHeight = uint32_t(a_height);
      wholeRegion.imageExtent       = VkExtent3D{ uint32_t(a_width), uint32_t(a_height), 1 };
      wholeRegion.imageOffset       = VkOffset3D{ 0,0,0 };
      wholeRegion.imageSubresource  = shittylayers;

      vkCmdCopyImageToBuffer(cmdBuff, a_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out_buffer, 1, &wholeRegion);
    }
    vkEndCommandBuffer(cmdBuff);

    RunCommandBuffer(cmdBuff, a_queue, a_device);

    vkFreeCommandBuffers(a_device, a_cmdPool, 1, &cmdBuff);
  }


  static void CreateVertexBuffer(VkDevice a_device, VkPhysicalDevice a_physDevice, const size_t a_bufferSize,
                                 VkBuffer *a_pBuffer, VkDeviceMemory *a_pBufferMemory)
  {
   
    VkBufferCreateInfo bufferCreateInfo = {};
    bufferCreateInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.pNext       = nullptr;
    bufferCreateInfo.size        = a_bufferSize;                         
    bufferCreateInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;            

    VK_CHECK_RESULT(vkCreateBuffer(a_device, &bufferCreateInfo, NULL, a_pBuffer)); // create bufferStaging.

                
    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(a_device, (*a_pBuffer), &memoryRequirements);


    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext           = nullptr;
    allocateInfo.allocationSize  = memoryRequirements.size; // specify required memory.
    allocateInfo.memoryTypeIndex = vk_utils::FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, a_physDevice); // #NOTE VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT

    VK_CHECK_RESULT(vkAllocateMemory(a_device, &allocateInfo, NULL, a_pBufferMemory));   // allocate memory on device.
    VK_CHECK_RESULT(vkBindBufferMemory(a_device, (*a_pBuffer), (*a_pBufferMemory), 0));  // Now associate that allocated memory with the bufferStaging. With that, the bufferStaging is backed by actual memory.
  }

  static void CreateStagingBuffer(VkDevice a_device, VkPhysicalDevice a_physDevice, const size_t a_bufferSize,
                                  VkBuffer *a_pBuffer, VkDeviceMemory *a_pBufferMemory)
  {
    VkBufferCreateInfo bufferCreateInfo = {};
    bufferCreateInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size        = a_bufferSize; // bufferStaging size in bytes.
    bufferCreateInfo.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; // bufferStaging is used as a storage bufferStaging and we can _copy_to_ it. #NOTE this!
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // bufferStaging is exclusive to a single queue family at a time.

    VK_CHECK_RESULT(vkCreateBuffer(a_device, &bufferCreateInfo, NULL, a_pBuffer)); // create bufferStaging.

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(a_device, (*a_pBuffer), &memoryRequirements);

  
    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize  = memoryRequirements.size; // specify required memory.
    allocateInfo.memoryTypeIndex = vk_utils::FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, a_physDevice);

    VK_CHECK_RESULT(vkAllocateMemory(a_device, &allocateInfo, NULL, a_pBufferMemory));  // allocate memory on device.                                                                                       
    VK_CHECK_RESULT(vkBindBufferMemory(a_device, (*a_pBuffer), (*a_pBufferMemory), 0)); // Now associate that allocated memory with the bufferStaging. With that, the bufferStaging is backed by actual memory.
  }

  static void CreateTextureForRenderToIt(VkDevice a_device, VkPhysicalDevice a_physDevice, const int a_width, const int a_height,
                                         VkImage *a_image, VkDeviceMemory *a_pImagesMemory, VkImageView* a_attachmentView)
  {
    // first create desired objects, but still don't allocate memory for them
    //
    VkImageCreateInfo imgCreateInfo = {};
    imgCreateInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCreateInfo.pNext         = nullptr;
    imgCreateInfo.flags         = 0; 
    imgCreateInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgCreateInfo.format        = FRAMEBUFFER_FORMAT;
    imgCreateInfo.extent        = VkExtent3D{ uint32_t(a_width), uint32_t(a_height), 1 };
    imgCreateInfo.mipLevels     = 1;
    imgCreateInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCreateInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCreateInfo.usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // copy from the texture and render to it
    imgCreateInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgCreateInfo.arrayLayers   = 1;
    VK_CHECK_RESULT(vkCreateImage(a_device, &imgCreateInfo, nullptr, a_image));

    // now allocate memory for image
    //
    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(a_device, *a_image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize  = memoryRequirements.size; // specify required memory.
    allocateInfo.memoryTypeIndex = vk_utils::FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, a_physDevice);
    VK_CHECK_RESULT(vkAllocateMemory(a_device, &allocateInfo, NULL, a_pImagesMemory)); // allocate memory on device.
    VK_CHECK_RESULT(vkBindImageMemory(a_device, (*a_image), (*a_pImagesMemory), 0));

    // and create image view finally
    //
    VkImageViewCreateInfo imageViewInfo = {};
    {
      imageViewInfo.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      imageViewInfo.flags      = 0;
      imageViewInfo.viewType   = VK_IMAGE_VIEW_TYPE_2D;
      imageViewInfo.format     = FRAMEBUFFER_FORMAT;
      imageViewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
      imageViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      imageViewInfo.subresourceRange.baseMipLevel   = 0;
      imageViewInfo.subresourceRange.baseArrayLayer = 0;
      imageViewInfo.subresourceRange.layerCount     = 1;
      imageViewInfo.subresourceRange.levelCount     = 1;
      imageViewInfo.image                           = (*a_image);
    }
    VK_CHECK_RESULT(vkCreateImageView(a_device, &imageViewInfo, nullptr, a_attachmentView));

  }

  static void CreateFBO(VkDevice a_device, VkRenderPass a_renderPass, VkImageView a_view, int a_width, int a_heiht,
                        VkFramebuffer* a_fbo)
  {
   
    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass      = a_renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments    = &a_view;
    framebufferInfo.width           = a_width;
    framebufferInfo.height          = a_heiht;
    framebufferInfo.layers          = 1;

    if (vkCreateFramebuffer(a_device, &framebufferInfo, nullptr, a_fbo) != VK_SUCCESS)
      throw std::runtime_error("[CreateFBO]: failed to create framebuffer!");
  }

  static void RunCommandBuffer(VkCommandBuffer a_cmdBuff, VkQueue a_queue, VkDevice a_device)
  {
    // Now we shall finally submit the recorded command bufferStaging to a queue.
    //
    VkSubmitInfo submitInfo = {};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1; // submit a single command bufferStaging
    submitInfo.pCommandBuffers    = &a_cmdBuff; // the command bufferStaging to submit.
                                         
    VkFence fence;
    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = 0;
    VK_CHECK_RESULT(vkCreateFence(a_device, &fenceCreateInfo, NULL, &fence));

    // We submit the command bufferStaging on the queue, at the same time giving a fence.
    //
    VK_CHECK_RESULT(vkQueueSubmit(a_queue, 1, &submitInfo, fence));

    // The command will not have finished executing until the fence is signalled.
    // So we wait here. We will directly after this read our bufferStaging from the GPU,
    // and we will not be sure that the command has finished executing unless we wait for the fence.
    // Hence, we use a fence here.
    //
    VK_CHECK_RESULT(vkWaitForFences(a_device, 1, &fence, VK_TRUE, 100000000000));

    vkDestroyFence(a_device, fence, NULL);
  }

  // An example function that immediately copy vertex data to GPU
  //
  static void PutTriangleVerticesToVBO_Now(VkDevice a_device, VkCommandPool a_pool, VkQueue a_queue, float* a_triPos, int a_floatsNum,
                                           VkBuffer a_buffer)
  {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = a_pool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuff;
    if (vkAllocateCommandBuffers(a_device, &allocInfo, &cmdBuff) != VK_SUCCESS)
      throw std::runtime_error("[PutTriangleVerticesToVBO_Now]: failed to allocate command buffer!");

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; 
    
    vkBeginCommandBuffer(cmdBuff, &beginInfo);
    vkCmdUpdateBuffer   (cmdBuff, a_buffer, 0, a_floatsNum * sizeof(float), a_triPos);
    vkEndCommandBuffer  (cmdBuff);

    RunCommandBuffer(cmdBuff, a_queue, a_device);

    vkFreeCommandBuffers(a_device, a_pool, 1, &cmdBuff);
  }


};

int main() 
{
  HelloTriangleApplication app;

  try 
  {
    app.run();
  }
  catch (const std::exception& e) 
  {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}