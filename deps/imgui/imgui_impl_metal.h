#pragma once

#include "imgui.h"

#ifndef IMGUI_DISABLE

#ifdef __OBJC__
@class MTLRenderPassDescriptor;
@protocol MTLDevice, MTLCommandBuffer, MTLRenderCommandEncoder;

IMGUI_IMPL_API bool ImGui_ImplMetal_Init(id<MTLDevice> device);
IMGUI_IMPL_API void ImGui_ImplMetal_Shutdown();
IMGUI_IMPL_API void ImGui_ImplMetal_NewFrame(MTLRenderPassDescriptor* renderPassDescriptor);
IMGUI_IMPL_API void ImGui_ImplMetal_RenderDrawData(ImDrawData* drawData,
                                                    id<MTLCommandBuffer> commandBuffer,
                                                    id<MTLRenderCommandEncoder> commandEncoder);
#endif

#endif
