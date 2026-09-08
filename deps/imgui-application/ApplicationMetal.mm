#include "ApplicationMetal.hpp"

#include "imgui.h"
#include "imgui_impl_metal.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

/**
 * @brief Objective-C/Metal state owned by the opaque application renderer.
 *
 * The source file is compiled with ARC. The strong Objective-C references keep
 * the drawable, command buffer, and command encoder alive until the frame is
 * explicitly finished.
 */
struct MetalRenderer
{
    /// GPU device used by the renderer and ImGui Metal backend.
    id<MTLDevice> device = nil;
    /// Queue used to create and submit one command buffer per frame.
    id<MTLCommandQueue> commandQueue = nil;
    /// Layer attached to the GLFW-created Cocoa view.
    CAMetalLayer* layer = nil;
    /// Drawable acquired from the layer for the current frame.
    id<CAMetalDrawable> drawable = nil;
    /// Command buffer containing the current frame's GPU work.
    id<MTLCommandBuffer> commandBuffer = nil;
    /// Render encoder that records the current frame's draw commands.
    id<MTLRenderCommandEncoder> commandEncoder = nil;
    /// Render-pass configuration used by both Metal and ImGui.
    MTLRenderPassDescriptor* renderPassDescriptor = nil;
};

/**
 * @brief Ends and optionally presents the renderer's current frame.
 *
 * The renderer clears its handles before releasing the encoder so this helper
 * is idempotent from the caller's point of view. It is used both for normal
 * presentation and as a cleanup guard when a frame is abandoned or the
 * application is destroyed.
 *
 * @param renderer Renderer state to finish.
 * @param presentDrawable Whether the current drawable should be presented.
 */
static void finishMetalFrame(MetalRenderer* renderer, bool presentDrawable)
{
    if (renderer == nullptr || renderer->commandEncoder == nil)
        return;

    id<MTLRenderCommandEncoder> commandEncoder = renderer->commandEncoder;
    id<MTLCommandBuffer> commandBuffer = renderer->commandBuffer;
    id<CAMetalDrawable> drawable = renderer->drawable;

    renderer->commandEncoder = nil;
    renderer->commandBuffer = nil;
    renderer->drawable = nil;

    [commandEncoder endEncoding];
    if (presentDrawable && commandBuffer != nil && drawable != nil)
        [commandBuffer presentDrawable:drawable];
    if (commandBuffer != nil)
        [commandBuffer commit];
}

/**
 * @brief Creates the Metal device objects and binds Metal to the GLFW view.
 */
void* ImGuiApplicationMetal_Create(GLFWwindow* window)
{
    MetalRenderer* renderer = new MetalRenderer;
    renderer->device = MTLCreateSystemDefaultDevice();
    if (renderer->device == nil)
    {
        delete renderer;
        return nullptr;
    }

    renderer->commandQueue = [renderer->device newCommandQueue];
    renderer->layer = [CAMetalLayer layer];
    renderer->layer.device = renderer->device;
    renderer->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    // GLFW owns the NSWindow; the renderer only installs the layer on its view.
    NSWindow* nativeWindow = glfwGetCocoaWindow(window);
    nativeWindow.contentView.wantsLayer = YES;
    nativeWindow.contentView.layer = renderer->layer;

    if (!ImGui_ImplMetal_Init(renderer->device))
    {
        delete renderer;
        return nullptr;
    }
    return renderer;
}

/**
 * @brief Finishes any outstanding frame and releases the ImGui Metal backend.
 */
void ImGuiApplicationMetal_Destroy(void* opaqueRenderer)
{
    MetalRenderer* renderer = static_cast<MetalRenderer*>(opaqueRenderer);
    if (renderer == nullptr)
        return;

    finishMetalFrame(renderer, false);
    ImGui_ImplMetal_Shutdown();
    delete renderer;
}

/**
 * @brief Acquires a drawable and creates the command encoder for one frame.
 *
 * The autorelease pool is deliberately local to the frame setup. ARC keeps the
 * objects stored in MetalRenderer alive after this pool is drained.
 */
bool ImGuiApplicationMetal_BeginFrame(void* opaqueRenderer, GLFWwindow* window)
{
    @autoreleasepool
    {
    MetalRenderer* renderer = static_cast<MetalRenderer*>(opaqueRenderer);
    if (renderer == nullptr)
        return false;

    finishMetalFrame(renderer, false);

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0)
        return false;

    // CAMetalLayer sizes are expressed in physical framebuffer pixels.
    renderer->layer.drawableSize = CGSizeMake(width, height);
    renderer->drawable = [renderer->layer nextDrawable];
    if (renderer->drawable == nil)
        return false;

    renderer->renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    renderer->renderPassDescriptor.colorAttachments[0].texture = renderer->drawable.texture;
    renderer->renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    renderer->renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    renderer->renderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.45, 0.55, 0.60, 1.0);

    // The encoder must be ended exactly once before its command buffer is
    // committed or the Metal validation layer will abort the application.
    renderer->commandBuffer = [renderer->commandQueue commandBuffer];
    renderer->commandEncoder = [renderer->commandBuffer renderCommandEncoderWithDescriptor:renderer->renderPassDescriptor];
    ImGui_ImplMetal_NewFrame(renderer->renderPassDescriptor);
    return renderer->commandEncoder != nil;
    }
}

/**
 * @brief Encodes ImGui draw data, presents the drawable, and commits the GPU work.
 */
void ImGuiApplicationMetal_RenderFrame(void* opaqueRenderer, ImDrawData* drawData)
{
    MetalRenderer* renderer = static_cast<MetalRenderer*>(opaqueRenderer);
    if (renderer == nullptr || renderer->commandEncoder == nil)
        return;

    ImGui_ImplMetal_RenderDrawData(drawData, renderer->commandBuffer, renderer->commandEncoder);
    finishMetalFrame(renderer, true);
}
