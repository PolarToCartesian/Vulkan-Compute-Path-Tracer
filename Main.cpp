#include <set>
#include <cmath>
#include <ctime>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <optional>
#include <iostream>
#include <algorithm>

#include <vulkan/vulkan.hpp>

#ifdef _WIN32

#include <wrl.h>
#include <wincodec.h>

#define THROW_FATAL_ERROR(msg) MessageBoxA(NULL, msg, "Error", MB_ICONERROR)

#else

#define THROW_FATAL_ERROR(msg) std::cerr << msg << '\n'

#endif

// Meant to be modified
#define RENDER_SURFACE_WIDTH  (1920 / 2)
#define RENDER_SURFACE_HEIGHT (1080 / 2)
#define GPU_WORKGROUP_SIZE    (32)       // NVIDIA: 32, AMD: 64

// Constants
#define RENDER_SURFACE_PIXEL_COUNT           (RENDER_SURFACE_WIDTH * RENDER_SURFACE_HEIGHT)
#define RENDER_SURFACE_COLOR_COMPONENT_COUNT (4u)

#define RENDER_SURFACE_U8_BPP    (RENDER_SURFACE_COLOR_COMPONENT_COUNT)
#define RENDER_SURFACE_U8_STRIDE (RENDER_SURFACE_WIDTH     * RENDER_SURFACE_U8_BPP)
#define RENDER_SURFACE_U8_SIZE   (RENDER_SURFACE_U8_STRIDE * RENDER_SURFACE_HEIGHT)

#define RENDER_SURFACE_FLT_BPP    (RENDER_SURFACE_COLOR_COMPONENT_COUNT * sizeof(float))
#define RENDER_SURFACE_FLT_STRIDE (RENDER_SURFACE_WIDTH      * RENDER_SURFACE_FLT_BPP)
#define RENDER_SURFACE_FLT_SIZE   (RENDER_SURFACE_FLT_STRIDE * RENDER_SURFACE_HEIGHT)

std::string GenerateOutpuFilename() noexcept { // the filename w/o the extension
    const std::time_t cTime = std::time(NULL);
    const std::tm* timePtr = std::localtime(&cTime);

    char buff[100] = { 0 }; // filename
    std::snprintf(buff, sizeof(buff), "%d-%d-%d, %d-%d-%d", timePtr->tm_mday, timePtr->tm_mon, timePtr->tm_year, timePtr->tm_hour, timePtr->tm_min, timePtr->tm_sec);

    return std::string(buff);
}

int main(int argc, char** argv) {
#ifdef _WIN32

    if (CoInitialize(NULL) != S_OK)
        THROW_FATAL_ERROR("[COM] Failed To Init COM");

#endif // _WIN32

    try {
#ifndef _DEBUG
        constexpr std::uint32_t validationLayerCount = 0u;
#else
        constexpr std::uint32_t validationLayerCount = 1u;
#endif

        constexpr std::array<const char*, 1u> validationLayers = { "VK_LAYER_KHRONOS_validation" };

        vk::Instance instance;
        { // Create Instance
            constexpr auto appInfo = vk::ApplicationInfo(
                "Polar-PTX",
                VK_MAKE_VERSION(1, 0, 0),
                "Polar-PTX",
                VK_MAKE_VERSION(1, 0, 0),
                VK_MAKE_VERSION(1, 0, 0)
            );

            const auto instanceCreateInfo = vk::InstanceCreateInfo(
                vk::InstanceCreateFlags(),
                &appInfo,
                validationLayerCount,
                validationLayers.data(),
                0u,
                nullptr
            );

            instance = vk::createInstance(instanceCreateInfo);
        }

        std::uint32_t computeQueueIndex = 0, physicalDeviceScore = 0;

        vk::PhysicalDevice                     physicalDevice;
        vk::PhysicalDeviceProperties           physicalDeviceProperties;
        vk::PhysicalDeviceMemoryProperties     physicalDeviceMemoryProperties;
        std::vector<vk::QueueFamilyProperties> physicalDeviceQueueFamilyProperties;

        { // Pick Physical Devices
            bool bPickedPhysicalDevice = false;

            const auto devices = instance.enumeratePhysicalDevices();

            if (!devices.size())
                throw std::runtime_error("No Physical Devices Found");

            // Loop through each device to find the best compatible one by scoring them
            for (const auto& currentDevice : devices) {
                physicalDevice = currentDevice;
                physicalDeviceProperties = currentDevice.getProperties();
                physicalDeviceMemoryProperties = currentDevice.getMemoryProperties();
                physicalDeviceQueueFamilyProperties = currentDevice.getQueueFamilyProperties();

                // Test Compatibility
                auto findComputeQueueIndex = [](const vk::QueueFamilyProperties& qfp) {
                    if ((bool)(qfp.queueFlags & vk::QueueFlagBits::eCompute))
                        return true;

                    return false;
                };

                const auto it = std::find_if(physicalDeviceQueueFamilyProperties.begin(), physicalDeviceQueueFamilyProperties.end(), findComputeQueueIndex);
                if (it == physicalDeviceQueueFamilyProperties.end())
                    continue;

                if (physicalDeviceProperties.deviceType != vk::PhysicalDeviceType::eDiscreteGpu)
                    continue;

                bPickedPhysicalDevice = true;

                break;
            }

            if (!bPickedPhysicalDevice)
                throw std::runtime_error("No Compatible Physical Device Found");
        }

        vk::Device logicalDevice;
        { // Create Logical Device
            const float queuePriority = 1.f;

            const size_t queueCount = 1u;
            vk::DeviceQueueCreateInfo queueCreateInfos[queueCount] = {
                vk::DeviceQueueCreateInfo(vk::DeviceQueueCreateFlags(), computeQueueIndex, 1, &queuePriority)
            };

            auto enabledFeatures = vk::PhysicalDeviceFeatures();

            auto createInfo = vk::DeviceCreateInfo(
                vk::DeviceCreateFlags(),
                queueCount,
                queueCreateInfos,
                validationLayerCount,
                validationLayers.data(),
                0, nullptr, &enabledFeatures
            );

            logicalDevice = physicalDevice.createDevice(createInfo);
        }

        vk::Queue computeQueue;
        { // Fetch Queues
            computeQueue = logicalDevice.getQueue(computeQueueIndex, 0);
        }

        vk::Buffer       pixelBuffer;
        vk::DeviceMemory pixelBufferDeviceMemory;
        { // Create And Allocate Shader Resources
            const auto PickCompatibleMemoryType = [&logicalDevice, &physicalDeviceMemoryProperties](const vk::Buffer& buffer, const vk::MemoryPropertyFlags& memoryFlagRequirements) {
                const auto bufferMemoryRequirements = logicalDevice.getBufferMemoryRequirements(buffer);

                for (std::uint32_t i = 0u; i < physicalDeviceMemoryProperties.memoryTypeCount; i++) {
                    const bool condition0 = bufferMemoryRequirements.memoryTypeBits & (1 << i);
                    const bool condition1 = (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & memoryFlagRequirements) == memoryFlagRequirements;

                    if (condition0 && condition1)
                        return vk::MemoryAllocateInfo(bufferMemoryRequirements.size, i);
                }

                throw std::runtime_error("Could not find a suitable memory type to allocate a buffer");
            };

            // Create Render Buffer
            const auto bufferCreateInfo = vk::BufferCreateInfo(
                vk::BufferCreateFlags{},
                RENDER_SURFACE_FLT_SIZE,
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::SharingMode::eExclusive,
                1u,
                &computeQueueIndex
            );

            pixelBuffer = logicalDevice.createBuffer(bufferCreateInfo);

            // Allocate Buffer Memory
            const auto memoryAllocateInfo = PickCompatibleMemoryType(pixelBuffer, vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible);
            pixelBufferDeviceMemory = logicalDevice.allocateMemory(memoryAllocateInfo);

            // Bind Buffer Memory
            logicalDevice.bindBufferMemory(pixelBuffer, pixelBufferDeviceMemory, 0);
        }

        vk::DescriptorSetLayout        descriptorSetLayout;
        vk::DescriptorPool             descriptorPool;
        vk::DescriptorSetAllocateInfo  descriptorSetAllocateInfo;
        vk::DescriptorSet              descriptorSet;
        { // Create Descriptors
            // Create Descriptor Set Layout
            const std::array<vk::DescriptorSetLayoutBinding, 1u> descriptorSetLayoutBindings = {
                // PixelBuffer
                vk::DescriptorSetLayoutBinding(
                    0u, vk::DescriptorType::eStorageBuffer,
                    1u, vk::ShaderStageFlagBits::eCompute,
                    nullptr
                )
            };

            const auto descriptorSetLayoutCreateInfo = vk::DescriptorSetLayoutCreateInfo(
                vk::DescriptorSetLayoutCreateFlags{},
                static_cast<std::uint32_t>(descriptorSetLayoutBindings.size()),
                descriptorSetLayoutBindings.data()
            );

            descriptorSetLayout = logicalDevice.createDescriptorSetLayout(descriptorSetLayoutCreateInfo);

            // Create Descriptor Pool
            const auto descriptorPoolSize = vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1u);
            const auto descriptorPoolCreateInfo = vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlags{}, 1u, 1u, &descriptorPoolSize);
            descriptorPool = logicalDevice.createDescriptorPool(descriptorPoolCreateInfo);

            // Allocate Descriptor Set
            auto descriptorSetAllocateInfo = vk::DescriptorSetAllocateInfo(descriptorPool, 1u, &descriptorSetLayout);
            descriptorSet = std::move(logicalDevice.allocateDescriptorSets(descriptorSetAllocateInfo)[0]);

            // Initialize Descriptor Set
            const auto descriptorBufferInfo = vk::DescriptorBufferInfo(pixelBuffer, 0u, RENDER_SURFACE_FLT_SIZE);
            const auto writeDescriptorSet = vk::WriteDescriptorSet(
                descriptorSet, 0u, 0u, 1u,
                vk::DescriptorType::eStorageBuffer, nullptr, &descriptorBufferInfo
            );
            logicalDevice.updateDescriptorSets(1u, &writeDescriptorSet, 0, nullptr);
        }

        vk::ShaderModule   shaderModule;
        vk::PipelineLayout computePipelineLayout;
        vk::Pipeline       computePipeline;
        { // Create The Pipeline
            // Read Shader Binary File To A Buffer
            std::ifstream shaderFile("shader.spv", std::ios::binary | std::ios::ate);
            if (!shaderFile.is_open())
                throw std::runtime_error("Failed To Open Shader File");

            const size_t codeSize = shaderFile.tellg();
            std::unique_ptr<char[]> shaderFileBuffer = std::make_unique<char[]>(codeSize);

            shaderFile.seekg(0);
            shaderFile.read(shaderFileBuffer.get(), codeSize);
            shaderFile.close();

            // Create Shader Module
            const auto shaderModuleCreateInfo = vk::ShaderModuleCreateInfo(vk::ShaderModuleCreateFlags{}, codeSize, (std::uint32_t*)shaderFileBuffer.get());
            shaderModule = logicalDevice.createShaderModule(shaderModuleCreateInfo);

            const auto shaderStageCreateInfo = vk::PipelineShaderStageCreateInfo(
                vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eCompute,
                shaderModule, "main", nullptr
            );

            const auto pipelineLayoutCreateInfo = vk::PipelineLayoutCreateInfo(
                vk::PipelineLayoutCreateFlags{},
                1u, &descriptorSetLayout,
                0u, nullptr
            );

            computePipelineLayout = logicalDevice.createPipelineLayout(pipelineLayoutCreateInfo);

            const auto computePipelineCreateInfo = vk::ComputePipelineCreateInfo(
                vk::PipelineCreateFlags{}, shaderStageCreateInfo, computePipelineLayout, {}, 0
            );

            computePipeline = logicalDevice.createComputePipeline({}, computePipelineCreateInfo).value;
        }

        vk::CommandPool   commandPool;
        vk::CommandBuffer commandBuffer;
        { // Create Command Pool & Buffer
            const auto commandPoolCreateInfo = vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlags{}, computeQueueIndex);
            commandPool = logicalDevice.createCommandPool(commandPoolCreateInfo);

            const auto commandBufferAllocateInfo = vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, 1u);
            commandBuffer = std::move(logicalDevice.allocateCommandBuffers(commandBufferAllocateInfo)[0]);
        }

        { // Fill Command Buffer
            const auto commandBufferSubmitInfo = vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);

            commandBuffer.begin(commandBufferSubmitInfo);

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout, 0, 1u, &descriptorSet, 0, nullptr);
            commandBuffer.dispatch((uint32_t)std::ceil(RENDER_SURFACE_WIDTH / (float)GPU_WORKGROUP_SIZE),
                (uint32_t)std::ceil(RENDER_SURFACE_HEIGHT / (float)GPU_WORKGROUP_SIZE), 1);

            commandBuffer.end();
        }

        vk::Fence fence;
        { // Create Synch Objects
            const auto fenceCreateInfo = vk::FenceCreateInfo(vk::FenceCreateFlags{});

            fence = logicalDevice.createFence(fenceCreateInfo);
        }

        { // Run
            const auto submitInfo = vk::SubmitInfo(
                0, nullptr, nullptr,
                1u, &commandBuffer,
                0u, nullptr
            );

            computeQueue.submit(1u, &submitInfo, fence);
            logicalDevice.waitForFences(1, &fence, VK_TRUE, UINT64_MAX);
        }

        { // Save Image
            float* f32Image = (float*)logicalDevice.mapMemory(pixelBufferDeviceMemory, 0, RENDER_SURFACE_FLT_SIZE);

            std::unique_ptr<std::uint8_t[]> pU8Image = std::make_unique<std::uint8_t[]>(RENDER_SURFACE_U8_SIZE);

            for (std::uint64_t i = 0; i < RENDER_SURFACE_PIXEL_COUNT; i++) {
                pU8Image[i * 4 + 0] = (std::uint8_t)std::clamp(f32Image[i * 4 + 0] * 255.f, 0.f, 255.f);
                pU8Image[i * 4 + 1] = (std::uint8_t)std::clamp(f32Image[i * 4 + 1] * 255.f, 0.f, 255.f);
                pU8Image[i * 4 + 2] = (std::uint8_t)std::clamp(f32Image[i * 4 + 2] * 255.f, 0.f, 255.f);
                pU8Image[i * 4 + 3] = (std::uint8_t)std::clamp(f32Image[i * 4 + 3] * 255.f, 0.f, 255.f);
            }

            logicalDevice.unmapMemory(pixelBufferDeviceMemory);

#ifdef _WIN32 // Save as .PNG

            const std::string  filename = GenerateOutpuFilename() + ".png";
            const std::wstring filenameW(filename.begin(), filename.end());

            Microsoft::WRL::ComPtr<IWICImagingFactory>    factory;
            Microsoft::WRL::ComPtr<IWICBitmapEncoder>     bitmapEncoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> bitmapFrame;
            Microsoft::WRL::ComPtr<IWICStream>            outputStream;

            if (CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)) != S_OK)
                THROW_FATAL_ERROR("[WIC] Could Not Create IWICImagingFactory");

            if (factory->CreateStream(&outputStream) != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Create Output Stream");

            if (outputStream->InitializeFromFilename(filenameW.c_str(), GENERIC_WRITE) != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Initialize Output Stream From Filename");

            if (factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &bitmapEncoder) != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Create Bitmap Encoder");

            if (bitmapEncoder->Initialize(outputStream.Get(), WICBitmapEncoderNoCache) != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Initialize Bitmap ");

            if (bitmapEncoder->CreateNewFrame(&bitmapFrame, NULL) != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Create A New Frame");

            if (bitmapFrame->Initialize(NULL) != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Initialize A Bitmap's Frame");

            if (bitmapFrame->SetSize(RENDER_SURFACE_WIDTH, RENDER_SURFACE_HEIGHT) != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Set A Bitmap's Frame's Size");

            WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
            if (bitmapFrame->SetPixelFormat(&pixelFormat) != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Set Pixel Format On A Bitmap Frame's");

            if (!IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA))
                THROW_FATAL_ERROR("[WIC] The Requested Pixel Format Is Not Supported");

            if (bitmapFrame->WritePixels(RENDER_SURFACE_HEIGHT, RENDER_SURFACE_U8_STRIDE, RENDER_SURFACE_U8_SIZE, (BYTE*)pU8Image.get()) != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Write Pixels To A Bitmap's Frame");

            if (bitmapFrame->Commit() != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Commit A Bitmap's Frame");

            if (bitmapEncoder->Commit() != S_OK)
                THROW_FATAL_ERROR("[WIC] Failed To Commit Bitmap Encoder");

#else // Save as .PAM (P7 https://en.wikipedia.org/wiki/Netpbm)

            const std::string filename = GenerateOutpuFilename() + ".pam";

            // Open
            FILE* fp = std::fopen(filename.c_str(), "wb");

            if (fp) {
                // Header
                std::fprintf(fp, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\n MAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", RENDER_SURFACE_WIDTH, RENDER_SURFACE_HEIGHT);

                // Write Contents
                std::fwrite(pU8Image.get(), RENDER_SURFACE_U8_SIZE, 1u, fp);

                // Close
                std::fclose(fp);
            }

#endif
        }

        { // Destroy Vulkan Objects
            logicalDevice.destroyFence(fence);
            logicalDevice.freeCommandBuffers(commandPool, 1u, &commandBuffer);
            logicalDevice.destroyCommandPool(commandPool);
            logicalDevice.destroyShaderModule(shaderModule);
            logicalDevice.destroyPipeline(computePipeline);
            logicalDevice.destroyPipelineLayout(computePipelineLayout);
            logicalDevice.resetDescriptorPool(descriptorPool);
            logicalDevice.destroyDescriptorPool(descriptorPool);
            logicalDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            logicalDevice.destroyBuffer(pixelBuffer);
            logicalDevice.freeMemory(pixelBufferDeviceMemory);
            logicalDevice.destroy();
            instance.destroy();
        }
    }
    catch (vk::SystemError err) {
        std::printf("Fatal Error: %s\n", err.what());
    }
    catch (std::runtime_error re) {
        std::printf("Fatal Error %s\n", re.what());
    }

#ifdef _WIN32

    CoUninitialize();

#endif // _WIN32

    return 0;
}