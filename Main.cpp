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
#define GPU_WORKGROUP_SIZE (32u) // NVIDIA: 32, AMD: 64

struct Coloru8 {
    std::uint8_t r, g, b, a;
}; // Coloru8

struct Colorf32 {
    float r, g, b, a;
}; // Colorf32

// Constants
class Image {
private:
    const std::uint16_t m_width;
    const std::uint16_t m_height;
    const std::uint32_t m_nPixels;

    std::unique_ptr<Coloru8[]> m_pBuff;

public:
    Image() = default;

    Image(const std::uint16_t width, const std::uint16_t height) noexcept
        : m_width(width), m_height(height),
        m_nPixels(static_cast<std::uint32_t>(width)* height)
    {
        this->m_pBuff = std::make_unique<Coloru8[]>(this->m_nPixels);
    }

    inline std::uint16_t GetWidth()      const noexcept { return this->m_width; }
    inline std::uint16_t GetHeight()     const noexcept { return this->m_height; }
    inline std::uint32_t GetPixelCount() const noexcept { return this->m_nPixels; }

    inline Coloru8* GetBufferPtr() const noexcept { return this->m_pBuff.get(); }

    inline       Coloru8& operator()(const size_t i)       noexcept { return this->m_pBuff[i]; }
    inline const Coloru8& operator()(const size_t i) const noexcept { return this->m_pBuff[i]; }

    inline       Coloru8& operator()(const size_t x, const size_t y)       noexcept { return this->m_pBuff[y * this->m_width + this->m_height]; }
    inline const Coloru8& operator()(const size_t x, const size_t y) const noexcept { return this->m_pBuff[y * this->m_width + this->m_height]; }

    void Save(const std::string& filename) noexcept { // filename shouldn't contain the file extension
#ifdef _WIN32 // Use WIC (Windows Imaging Component) To Save A .PNG File Natively

        const std::string  fullFilename = filename + ".png";
        const std::wstring fullFilenameW(fullFilename.begin(), fullFilename.end());

        Microsoft::WRL::ComPtr<IWICImagingFactory>    factory;
        Microsoft::WRL::ComPtr<IWICBitmapEncoder>     bitmapEncoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> bitmapFrame;
        Microsoft::WRL::ComPtr<IWICStream>            outputStream;

        if (CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)) != S_OK)
            THROW_FATAL_ERROR("[WIC] Could Not Create IWICImagingFactory");

        if (factory->CreateStream(&outputStream) != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Create Output Stream");

        if (outputStream->InitializeFromFilename(fullFilenameW.c_str(), GENERIC_WRITE) != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Initialize Output Stream From Filename");

        if (factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &bitmapEncoder) != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Create Bitmap Encoder");

        if (bitmapEncoder->Initialize(outputStream.Get(), WICBitmapEncoderNoCache) != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Initialize Bitmap ");

        if (bitmapEncoder->CreateNewFrame(&bitmapFrame, NULL) != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Create A New Frame");

        if (bitmapFrame->Initialize(NULL) != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Initialize A Bitmap's Frame");

        if (bitmapFrame->SetSize(this->m_width, this->m_height) != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Set A Bitmap's Frame's Size");

        const WICPixelFormatGUID desiredPixelFormat = GUID_WICPixelFormat32bppBGRA;

        WICPixelFormatGUID currentPixelFormat = {};
        if (bitmapFrame->SetPixelFormat(&currentPixelFormat) != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Set Pixel Format On A Bitmap Frame's");

        if (!IsEqualGUID(currentPixelFormat, desiredPixelFormat))
            THROW_FATAL_ERROR("[WIC] The Requested Pixel Format Is Not Supported");

        if (bitmapFrame->WritePixels(this->m_height, this->m_width * sizeof(Coloru8), this->m_nPixels * sizeof(Coloru8), (BYTE*)this->m_pBuff.get()) != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Write Pixels To A Bitmap's Frame");

        if (bitmapFrame->Commit() != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Commit A Bitmap's Frame");

        if (bitmapEncoder->Commit() != S_OK)
            THROW_FATAL_ERROR("[WIC] Failed To Commit Bitmap Encoder");

#else // On Other Operating Systems, Simply Write A .PAM File

        const std::string fullFilename = filename + ".pam";

        // Open
        FILE* fp = std::fopen(fullFilename.c_str(), "wb");

        if (fp) {
            // Header
            std::fprintf(fp, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\n MAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", this->m_width, this->m_height);

            // Write Contents
            std::fwrite(this->m_pBuff.get(), this->m_nPixels * sizeof(Coloru8), 1u, fp);

            // Close
            std::fclose(fp);
        }

#endif
    }
}; // Image

std::string GenerateOutputFilename() noexcept { // the filename w/o the extension
    const std::time_t cTime = std::time(NULL);
    const std::tm* timePtr = std::localtime(&cTime);

    char buff[100] = { 0 }; // filename
    std::snprintf(buff, sizeof(buff), "%d-%d-%d, %d-%d-%d", timePtr->tm_mday, timePtr->tm_mon, timePtr->tm_year, timePtr->tm_hour, timePtr->tm_min, timePtr->tm_sec);

    return std::string(buff);
}

struct CommandLineArguments {
    std::uint16_t surfaceWidth;
    std::uint16_t surfaceHeight;
}; // CommandLineArguments

CommandLineArguments ParseCommandLineArguments(int argc, char** argv) noexcept {
    CommandLineArguments result;

    auto ExtractCommandLineValueForOption = [argc, argv](const char* option) {
        for (size_t i = 1u; i < argc; ++i)
            if (std::strcmp(argv[i], option) == 0)
                return argv[i + 1];

        throw std::runtime_error("Missing Command Line Argument !");
    };

    result.surfaceWidth = std::atoi(ExtractCommandLineValueForOption("-w"));
    result.surfaceHeight = std::atoi(ExtractCommandLineValueForOption("-h"));

    return result;
}

void InitOSApis() {
#ifdef _WIN32

    if (CoInitialize(NULL) != S_OK)
        THROW_FATAL_ERROR("[COM] Failed To Init COM");

#endif // _WIN32
}

void UninitOSApis() {
#ifdef _WIN32

    CoUninitialize();

#endif // _WIN32
}

class VulkanBuffer {
private:
    size_t m_size;

    const vk::Device& m_device;

    vk::Buffer       m_buffer;
    vk::DeviceMemory m_memory;

public:
    VulkanBuffer() = default;

    VulkanBuffer(const vk::Device& device, const size_t size, const vk::BufferUsageFlagBits& usage, const std::vector<std::uint32_t>& queues) noexcept
        : m_size(size), m_device(device)
    {
        // Create The Buffer
        const auto bufferCreateInfo = vk::BufferCreateInfo(
            vk::BufferCreateFlags{}, this->m_size,
            usage,                   vk::SharingMode::eExclusive,
            queues.size(),           queues.data()
        );

        this->m_buffer = device.createBuffer(bufferCreateInfo);
    }

    void Allocate(const vk::PhysicalDevice& physicalDevice, const vk::MemoryPropertyFlags& memoryRequirements) noexcept {
        // Prelogue
        const auto physicalDeviceMemoryProperties = physicalDevice.getMemoryProperties();

        // Pick A Compatible Memory Type And Get Its Index
        const auto bufferMemoryRequirements = this->m_device.getBufferMemoryRequirements(this->m_buffer);

        for (std::uint32_t i = 0u; i < physicalDeviceMemoryProperties.memoryTypeCount; i++) {
            const bool condition0 = bufferMemoryRequirements.memoryTypeBits & (1 << i);
            const bool condition1 = (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & memoryRequirements) == memoryRequirements;

            if (condition0 && condition1) {
                // When Compatible
                // Allocate Buffer Memory
                const auto memoryAllocateInfo = vk::MemoryAllocateInfo(bufferMemoryRequirements.size, i);
                this->m_memory = this->m_device.allocateMemory(memoryAllocateInfo);
                return;
            }
        }

        throw std::runtime_error("Could not find a suitable memory type to allocate a buffer");
    }

    void Bind() const noexcept {
        // TODO: use one big buffer with suballocations
        this->m_device.bindBufferMemory(this->m_buffer, this->m_memory, 0u);
    }

    void* MapMemory() const noexcept {
        return this->m_device.mapMemory(this->m_memory, 0u, this->m_size);
    }

    void UnMapMemory() const noexcept {
        this->m_device.unmapMemory(this->m_memory);
    }

    void UnAllocate() const noexcept {
        this->m_device.freeMemory(this->m_memory);
    }

    void Destroy() const noexcept {
        this->m_device.destroyBuffer(this->m_buffer);
    }

    vk::DescriptorBufferInfo GetDescriptorBufferInfo() const noexcept {
        return vk::DescriptorBufferInfo(this->m_buffer, 0, this->m_size);
    }

    const vk::Buffer&       GetBuffer()       const noexcept { return this->m_buffer; }
    const vk::DeviceMemory& GetDeviceMemory() const noexcept { return this->m_memory; }
};

int main(int argc, char** argv) {
    InitOSApis();

    const auto commandLineArguments = ParseCommandLineArguments(argc, argv);

    std::printf("Width: %d, Height: %d\n", commandLineArguments.surfaceWidth, commandLineArguments.surfaceHeight);

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

        const std::vector<std::uint32_t> pQueues = { computeQueueIndex };
        VulkanBuffer pixelBuffer(logicalDevice, commandLineArguments.surfaceWidth* commandLineArguments.surfaceHeight * sizeof(Colorf32), vk::BufferUsageFlagBits::eStorageBuffer, pQueues);
        pixelBuffer.Allocate(physicalDevice, vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eDeviceLocal);
        pixelBuffer.Bind();

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
            const auto descriptorBufferInfo = pixelBuffer.GetDescriptorBufferInfo();
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
            commandBuffer.dispatch((uint32_t)std::ceil(commandLineArguments.surfaceWidth / (float)GPU_WORKGROUP_SIZE),
                (uint32_t)std::ceil(commandLineArguments.surfaceHeight / (float)GPU_WORKGROUP_SIZE), 1);

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
            float* f32Image = reinterpret_cast<float*>(pixelBuffer.MapMemory());

            Image frame(commandLineArguments.surfaceWidth, commandLineArguments.surfaceHeight);

            for (std::uint32_t i = 0; i < frame.GetPixelCount(); i++) {
                Coloru8& pixel = frame(i);
                pixel.r = static_cast<std::uint8_t>(std::clamp(f32Image[i * 4 + 0] * 255.f, 0.f, 255.f));
                pixel.g = static_cast<std::uint8_t>(std::clamp(f32Image[i * 4 + 1] * 255.f, 0.f, 255.f));
                pixel.b = static_cast<std::uint8_t>(std::clamp(f32Image[i * 4 + 2] * 255.f, 0.f, 255.f));
                pixel.a = static_cast<std::uint8_t>(std::clamp(f32Image[i * 4 + 3] * 255.f, 0.f, 255.f));
            }

            pixelBuffer.UnMapMemory();

            frame.Save(GenerateOutputFilename());
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
            pixelBuffer.UnAllocate();
            pixelBuffer.Destroy();
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

    return 0;
}