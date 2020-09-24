#include <set>
#include <cmath>
#include <stdio.h>
#include <fstream>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <optional>
#include <iostream>
#include <algorithm>

#include <vulkan/vulkan.hpp>

#define RENDER_SURFACE_WIDTH  (1920 / 2)
#define RENDER_SURFACE_HEIGHT (1080 / 2)
#define RENDER_SURFACE_PIXEL_COUNT (RENDER_SURFACE_WIDTH * RENDER_SURFACE_HEIGHT)
#define RENDER_SURFACE_SHADER_BPP    (32*4)
#define RENDER_SURFACE_SHADER_SIZE_BYTES (RENDER_SURFACE_PIXEL_COUNT * RENDER_SURFACE_SHADER_BPP)

#define WORKGROUP_SIZE 32

int main(int argc, char** argv) {
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
                physicalDevice                      = currentDevice;
                physicalDeviceProperties            = currentDevice.getProperties();
                physicalDeviceMemoryProperties      = currentDevice.getMemoryProperties();
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
                RENDER_SURFACE_SHADER_SIZE_BYTES,
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
            const auto descriptorPoolSize       = vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1u);
            const auto descriptorPoolCreateInfo = vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlags{}, 1u, 1u, &descriptorPoolSize);
            descriptorPool = logicalDevice.createDescriptorPool(descriptorPoolCreateInfo);

            // Allocate Descriptor Set
            auto descriptorSetAllocateInfo = vk::DescriptorSetAllocateInfo(descriptorPool, 1u, &descriptorSetLayout);
            descriptorSet = std::move(logicalDevice.allocateDescriptorSets(descriptorSetAllocateInfo)[0]);

            // Initialize Descriptor Set
            const auto descriptorBufferInfo = vk::DescriptorBufferInfo(pixelBuffer, 0u, RENDER_SURFACE_SHADER_SIZE_BYTES);
            const auto writeDescriptorSet   = vk::WriteDescriptorSet(
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
            std::ifstream shaderFile("shader.bin", std::ios::binary | std::ios::ate);
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
            commandBuffer.dispatch((uint32_t)std::ceil(RENDER_SURFACE_WIDTH / (float)WORKGROUP_SIZE),
                                   (uint32_t)std::ceil(RENDER_SURFACE_HEIGHT / (float)WORKGROUP_SIZE), 1);

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
            float* f32Image = (float*)logicalDevice.mapMemory(pixelBufferDeviceMemory, 0, RENDER_SURFACE_SHADER_SIZE_BYTES);

            std::unique_ptr<std::uint8_t[]> pU8Image = std::make_unique<std::uint8_t[]>(RENDER_SURFACE_PIXEL_COUNT * 3);

            for (std::uint64_t i = 0; i < RENDER_SURFACE_PIXEL_COUNT; i++) {
                pU8Image[i * 3 + 0] = (std::uint8_t)std::clamp(f32Image[i * 4 + 0] * 255.f, 0.f, 255.f);
                pU8Image[i * 3 + 1] = (std::uint8_t)std::clamp(f32Image[i * 4 + 1] * 255.f, 0.f, 255.f);
                pU8Image[i * 3 + 2] = (std::uint8_t)std::clamp(f32Image[i * 4 + 2] * 255.f, 0.f, 255.f);
            }

            logicalDevice.unmapMemory(pixelBufferDeviceMemory);

            // Open
            FILE* fp = std::fopen("output.ppm", "wb");

            // Header
            std::fprintf(fp, "P6\n%d %d 255\n", RENDER_SURFACE_WIDTH, RENDER_SURFACE_HEIGHT);

            // Write Contents
            std::fwrite(pU8Image.get(), RENDER_SURFACE_PIXEL_COUNT * 3, 1u, fp);

            // Close
            std::fclose(fp);
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
    } catch (vk::SystemError err) {
        std::printf("Fatal Error: %s\n", err.what());
    } catch (std::runtime_error re) {
        std::printf("Fatal Error %s\n", re.what());
    }

    return 0;
}