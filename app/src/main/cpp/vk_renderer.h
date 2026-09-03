#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "common.h"

struct ANativeWindow;

// A minimal 2D Vulkan renderer: one pipeline, push-constant transforms, a single
// vertex buffer holding every shape. Draw a frame by handing it a list of DrawCmd.
class VkRenderer {
public:
    bool initInstance();
    // Create surface + (first time) device/pipeline + swapchain for this window.
    bool initWindow(ANativeWindow* window);
    // Tear down swapchain + surface (window gone). Device is kept for resume.
    void termWindow();
    void cleanup();

    bool ready() const { return swapchainReady_; }
    // A window exists but nothing can be drawn (a failed swapchain rebuild, a
    // lost device): the main loop should keep calling tryRecover() instead of
    // blocking until the next window event.
    bool needsRecovery() const { return window_ != nullptr && !swapchainReady_; }
    void tryRecover();
    int width() const { return (int)extent_.width; }
    int height() const { return (int)extent_.height; }

    void drawFrame(const std::vector<DrawCmd>& cmds, const float clear[3]);

private:
    bool ensureDevice();
    // Everything that hangs off device_ (incl. the surface-independent
    // pipeline and sync objects). Used by cleanup() and to rebuild after a
    // device loss or a submit failure that left a fence unsignalled.
    void destroyDevice();
    bool createSurface();
    void destroySurface();
    // Device + swapchain + pipeline for window_ / surface_. Sets swapchainReady_.
    bool setupForWindow();
    bool createSwapchain();
    void destroySwapchain();
    // Drop and rebuild the swapchain (and the surface too on SURFACE_LOST).
    // Clears swapchainReady_ on failure so drawFrame never touches a null
    // swapchain; tryRecover() / INIT_WINDOW get to try again.
    void recreateSwapchain(bool surfaceToo);
    // Unrecoverable-in-frame error: stop drawing and have the next recovery
    // rebuild the device-level state.
    void stopRendering(const char* what, VkResult r);
    bool createRenderPass();
    bool createPipeline();
    bool createVertexBuffer();
    bool createSyncAndCommands();
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex,
                             const std::vector<DrawCmd>& cmds, const float clear[3]);
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props);

    ANativeWindow* window_ = nullptr;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_ = {0, 0};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkBuffer vbo_ = VK_NULL_HANDLE;
    VkDeviceMemory vboMem_ = VK_NULL_HANDLE;
    uint32_t shapeFirst_[SHAPE_COUNT] = {0};
    uint32_t shapeCount_[SHAPE_COUNT] = {0};

    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    static const int kFramesInFlight = 2;
    VkCommandBuffer cmdBufs_[kFramesInFlight] = {VK_NULL_HANDLE};
    VkSemaphore imageAvailable_[kFramesInFlight] = {VK_NULL_HANDLE};
    // One per swapchain image, not per frame in flight: the present that
    // waits on it may still be pending when the same frame slot comes round.
    std::vector<VkSemaphore> renderFinished_;
    VkFence inFlight_[kFramesInFlight] = {VK_NULL_HANDLE};
    uint32_t frame_ = 0;

    bool deviceReady_ = false;
    bool deviceBroken_ = false;   // rebuild device state before drawing again
    bool swapchainReady_ = false;
};
