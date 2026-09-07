#include "imgui.h"
#include "imgui_impl_metal.h"

#ifndef IMGUI_DISABLE

#import <Metal/Metal.h>

#include <cstdint>
#include <cstring>

namespace
{
    id<MTLDevice> device = nil;
    id<MTLRenderPipelineState> pipelineState = nil;
    id<MTLSamplerState> samplerState = nil;
    MTLPixelFormat renderTargetPixelFormat = MTLPixelFormatInvalid;
    ImTextureData* fontTextureData = nullptr;

    static const char* shaderSource = R"metal(
        #include <metal_stdlib>
        using namespace metal;

        struct VertexIn {
            float2 position [[attribute(0)]];
            float2 uv [[attribute(1)]];
            uchar4 color [[attribute(2)]];
        };

        struct VertexOut {
            float4 position [[position]];
            float2 uv;
            float4 color;
        };

        struct Uniforms {
            float4x4 projection;
        };

        vertex VertexOut imgui_vertex(VertexIn in [[stage_in]],
                                      constant Uniforms& uniforms [[buffer(1)]]) {
            VertexOut out;
            out.position = uniforms.projection * float4(in.position, 0.0, 1.0);
            out.uv = in.uv;
            out.color = float4(in.color) / 255.0;
            return out;
        }

        fragment half4 imgui_fragment(VertexOut in [[stage_in]],
                                      texture2d<half> texture [[texture(0)]],
                                      sampler textureSampler [[sampler(0)]]) {
            return half4(in.color) * texture.sample(textureSampler, in.uv);
        }
    )metal";

    void destroyTexture(ImTextureData* textureData)
    {
        if (textureData == nullptr || textureData->TexID == ImTextureID_Invalid)
            return;

        id<MTLTexture> texture = (__bridge id<MTLTexture>)(void*)(intptr_t)textureData->TexID;
        (void)texture;
        CFBridgingRelease((void*)(intptr_t)textureData->TexID);
        textureData->SetTexID(ImTextureID_Invalid);
        textureData->SetStatus(ImTextureStatus_Destroyed);
    }

    void updateTexture(ImTextureData* textureData)
    {
        if (textureData->Status == ImTextureStatus_WantDestroy)
        {
            destroyTexture(textureData);
            return;
        }

        if (textureData->Status != ImTextureStatus_WantCreate &&
            textureData->Status != ImTextureStatus_WantUpdates)
            return;

        if (textureData->Status == ImTextureStatus_WantCreate)
        {
            if (textureData->Format != ImTextureFormat_RGBA32)
                return;

            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:(NSUInteger)textureData->Width
                height:(NSUInteger)textureData->Height
                mipmapped:NO];
            descriptor.usage = MTLTextureUsageShaderRead;
            id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
            [texture replaceRegion:MTLRegionMake2D(0, 0,
                                                   (NSUInteger)textureData->Width,
                                                   (NSUInteger)textureData->Height)
                       mipmapLevel:0
                         withBytes:textureData->Pixels
                       bytesPerRow:(NSUInteger)textureData->Width * 4];

            textureData->SetTexID((ImTextureID)(intptr_t)CFBridgingRetain(texture));
        }
        else
        {
            id<MTLTexture> texture = (__bridge id<MTLTexture>)(void*)(intptr_t)textureData->TexID;
            for (const ImTextureRect& update : textureData->Updates)
            {
                [texture replaceRegion:MTLRegionMake2D(update.x, update.y, update.w, update.h)
                           mipmapLevel:0
                             withBytes:textureData->GetPixelsAt(update.x, update.y)
                           bytesPerRow:(NSUInteger)textureData->Width * 4];
            }
        }

        textureData->SetStatus(ImTextureStatus_OK);
    }

    bool createPipeline(MTLPixelFormat pixelFormat)
    {
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:shaderSource]
                                                        options:nil
                                                          error:&error];
        if (library == nil)
        {
            NSLog(@"ImGui Metal: failed to compile shaders: %@", error);
            return false;
        }

        MTLVertexDescriptor* vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
        vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
        vertexDescriptor.attributes[0].offset = offsetof(ImDrawVert, pos);
        vertexDescriptor.attributes[0].bufferIndex = 0;
        vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
        vertexDescriptor.attributes[1].offset = offsetof(ImDrawVert, uv);
        vertexDescriptor.attributes[1].bufferIndex = 0;
        vertexDescriptor.attributes[2].format = MTLVertexFormatUChar4;
        vertexDescriptor.attributes[2].offset = offsetof(ImDrawVert, col);
        vertexDescriptor.attributes[2].bufferIndex = 0;
        vertexDescriptor.layouts[0].stride = sizeof(ImDrawVert);
        vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"imgui_vertex"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"imgui_fragment"];
        descriptor.vertexDescriptor = vertexDescriptor;
        descriptor.colorAttachments[0].pixelFormat = pixelFormat;
        descriptor.colorAttachments[0].blendingEnabled = YES;
        descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

        pipelineState = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (pipelineState == nil)
            NSLog(@"ImGui Metal: failed to create pipeline: %@", error);
        return pipelineState != nil;
    }
}

bool ImGui_ImplMetal_Init(id<MTLDevice> metalDevice)
{
    if (metalDevice == nil)
        return false;

    device = metalDevice;
    MTLSamplerDescriptor* samplerDescriptor = [MTLSamplerDescriptor new];
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.mipFilter = MTLSamplerMipFilterLinear;
    samplerState = [device newSamplerStateWithDescriptor:samplerDescriptor];
    ImGui::GetIO().BackendRendererName = "imgui_impl_metal";
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    return true;
}

void ImGui_ImplMetal_Shutdown()
{
    if (fontTextureData != nullptr)
        destroyTexture(fontTextureData);

    fontTextureData = nullptr;
    pipelineState = nil;
    samplerState = nil;
    renderTargetPixelFormat = MTLPixelFormatInvalid;
    device = nil;
    ImGui::GetIO().BackendRendererName = nullptr;
    ImGui::GetIO().BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset |
                                     ImGuiBackendFlags_RendererHasTextures);
}

void ImGui_ImplMetal_NewFrame(MTLRenderPassDescriptor* renderPassDescriptor)
{
    renderTargetPixelFormat = renderPassDescriptor.colorAttachments[0].texture.pixelFormat;
}

void ImGui_ImplMetal_RenderDrawData(ImDrawData* drawData,
                                    id<MTLCommandBuffer> commandBuffer,
                                    id<MTLRenderCommandEncoder> commandEncoder)
{
    if (drawData == nullptr || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
        return;

    for (ImTextureData* textureData : ImGui::GetPlatformIO().Textures)
    {
        updateTexture(textureData);
        if (textureData == ImGui::GetIO().Fonts->TexRef._TexData)
            fontTextureData = textureData;
    }

    if (fontTextureData == nullptr)
    {
        ImGui::GetIO().Fonts->Build();
        for (ImTextureData* textureData : ImGui::GetPlatformIO().Textures)
        {
            updateTexture(textureData);
            if (textureData == ImGui::GetIO().Fonts->TexRef._TexData)
                fontTextureData = textureData;
        }
    }

    if (pipelineState == nil && !createPipeline(renderTargetPixelFormat))
        return;

    const float framebufferWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    const float framebufferHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;
    const float left = drawData->DisplayPos.x;
    const float right = drawData->DisplayPos.x + drawData->DisplaySize.x;
    const float top = drawData->DisplayPos.y;
    const float bottom = drawData->DisplayPos.y + drawData->DisplaySize.y;
    const float projection[4][4] = {
        { 2.0f / (right - left), 0.0f, 0.0f, 0.0f },
        { 0.0f, 2.0f / (top - bottom), 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { (right + left) / (left - right), (top + bottom) / (bottom - top), 0.0f, 1.0f }
    };

    [commandEncoder setRenderPipelineState:pipelineState];
    [commandEncoder setVertexBytes:projection length:sizeof(projection) atIndex:1];
    [commandEncoder setCullMode:MTLCullModeNone];

    for (const ImDrawList* drawList : drawData->CmdLists)
    {
        id<MTLBuffer> vertexBuffer = [device newBufferWithBytes:drawList->VtxBuffer.Data
                                                           length:drawList->VtxBuffer.Size * sizeof(ImDrawVert)
                                                          options:MTLResourceStorageModeShared];
        id<MTLBuffer> indexBuffer = [device newBufferWithBytes:drawList->IdxBuffer.Data
                                                          length:drawList->IdxBuffer.Size * sizeof(ImDrawIdx)
                                                         options:MTLResourceStorageModeShared];
        [commandEncoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];

        for (const ImDrawCmd& command : drawList->CmdBuffer)
        {
            if (command.UserCallback != nullptr)
            {
                command.UserCallback(drawList, &command);
                continue;
            }

            ImTextureID textureID = command.GetTexID();
            id<MTLTexture> texture = (__bridge id<MTLTexture>)(void*)(intptr_t)textureID;
            [commandEncoder setFragmentTexture:texture atIndex:0];
            [commandEncoder setFragmentSamplerState:samplerState atIndex:0];

            ImVec2 clipMin((command.ClipRect.x - drawData->DisplayPos.x) * drawData->FramebufferScale.x,
                          (command.ClipRect.y - drawData->DisplayPos.y) * drawData->FramebufferScale.y);
            ImVec2 clipMax((command.ClipRect.z - drawData->DisplayPos.x) * drawData->FramebufferScale.x,
                          (command.ClipRect.w - drawData->DisplayPos.y) * drawData->FramebufferScale.y);
            if (clipMin.x < 0.0f) clipMin.x = 0.0f;
            if (clipMin.y < 0.0f) clipMin.y = 0.0f;
            if (clipMax.x > framebufferWidth) clipMax.x = framebufferWidth;
            if (clipMax.y > framebufferHeight) clipMax.y = framebufferHeight;
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y || command.ElemCount == 0)
                continue;

            MTLScissorRect scissor = {
                (NSUInteger)clipMin.x, (NSUInteger)clipMin.y,
                (NSUInteger)(clipMax.x - clipMin.x), (NSUInteger)(clipMax.y - clipMin.y)
            };
            [commandEncoder setScissorRect:scissor];
            [commandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                       indexCount:command.ElemCount
                                        indexType:sizeof(ImDrawIdx) == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
                                      indexBuffer:indexBuffer
                                indexBufferOffset:(command.IdxOffset * sizeof(ImDrawIdx))];
        }
    }

    (void)commandBuffer;
}

#endif
