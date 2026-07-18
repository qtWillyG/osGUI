#include "osgui_impl_dx11.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <limits>
#include <map>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

struct OG_DX11VertexConstantBuffer { float mvp[4][4]; };

static ID3D11Device* g_Device = 0;
static ID3D11DeviceContext* g_Context = 0;
static ID3D11Buffer* g_VertexBuffer = 0;
static ID3D11Buffer* g_IndexBuffer = 0;
static int g_VertexBufferSize = 5000;
static int g_IndexBufferSize = 10000;
static ID3D11VertexShader* g_VertexShader = 0;
static ID3D11PixelShader* g_PixelShader = 0;
static ID3D11InputLayout* g_InputLayout = 0;
static ID3D11Buffer* g_ConstantBuffer = 0;
static ID3D11SamplerState* g_Sampler = 0;
static ID3D11BlendState* g_BlendState = 0;
static ID3D11RasterizerState* g_RasterizerState = 0;
static ID3D11DepthStencilState* g_DepthStencilState = 0;
static ID3D11ShaderResourceView* g_FontView = 0;
static ID3D11Texture2D* g_FontTexture = 0;
static std::map<og::TextureID, ID3D11ShaderResourceView*> g_Textures;
static og::TextureID g_NextTexture = 2;
static const og::TextureID g_FontTextureID = 1;

og::TextureID OG_ImplDX11_RegisterTexture(ID3D11ShaderResourceView* texture) {
    if (!texture || !g_Device) return 0;
    while (g_NextTexture < 2 || g_Textures.find(g_NextTexture) != g_Textures.end()) ++g_NextTexture;
    const og::TextureID id = g_NextTexture++;
    texture->AddRef();
    g_Textures[id] = texture;
    return id;
}

void OG_ImplDX11_UnregisterTexture(og::TextureID texture_id) {
    std::map<og::TextureID, ID3D11ShaderResourceView*>::iterator found = g_Textures.find(texture_id);
    if (found == g_Textures.end()) return;
    found->second->Release();
    g_Textures.erase(found);
}

static ID3D11ShaderResourceView* OG_ImplDX11_FindTexture(og::TextureID texture_id) {
    if (texture_id == g_FontTextureID) return g_FontView;
    std::map<og::TextureID, ID3D11ShaderResourceView*>::iterator found = g_Textures.find(texture_id);
    return found != g_Textures.end() ? found->second : 0;
}

static void OG_ImplDX11_DestroyFontsTexture() {
    if (g_FontView) { g_FontView->Release(); g_FontView = 0; }
    if (g_FontTexture) { g_FontTexture->Release(); g_FontTexture = 0; }
    if (og::GetCurrentContext() && og::GetFontAtlas().tex_id == g_FontTextureID)
        og::GetFontAtlas().tex_id = 0;
}

static bool OG_ImplDX11_CreateFontsTexture() {
    if (!g_Device || !og::GetCurrentContext()) return false;
    og::FontAtlas& atlas = og::GetFontAtlas();
    if (!atlas.pixels || atlas.width <= 0 || atlas.height <= 0 ||
        atlas.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        atlas.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) return false;
    const size_t max_size = (std::numeric_limits<size_t>::max)();
    if ((size_t)atlas.width > max_size / (size_t)atlas.height ||
        (size_t)atlas.width * (size_t)atlas.height > max_size / 4u) return false;
    const size_t pixel_count = (size_t)atlas.width * (size_t)atlas.height;
    unsigned char* rgba = (unsigned char*)malloc(pixel_count * 4u);
    if (!rgba) return false;
    for (size_t i = 0; i < pixel_count; ++i) {
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = atlas.pixels[i];
    }

    D3D11_TEXTURE2D_DESC texture_desc;
    memset(&texture_desc, 0, sizeof(texture_desc));
    texture_desc.Width = (UINT)atlas.width;
    texture_desc.Height = (UINT)atlas.height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial_data = { rgba, (UINT)atlas.width * 4u, 0 };

    ID3D11Texture2D* new_texture = 0;
    HRESULT result = g_Device->CreateTexture2D(&texture_desc, &initial_data, &new_texture);
    free(rgba);
    if (FAILED(result) || !new_texture) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc;
    memset(&view_desc, 0, sizeof(view_desc));
    view_desc.Format = texture_desc.Format;
    view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view_desc.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* new_view = 0;
    result = g_Device->CreateShaderResourceView(new_texture, &view_desc, &new_view);
    if (FAILED(result) || !new_view) { new_texture->Release(); return false; }

    OG_ImplDX11_DestroyFontsTexture();
    g_FontTexture = new_texture;
    g_FontView = new_view;
    atlas.tex_id = g_FontTextureID;
    return true;
}

bool OG_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!device || !context || g_Device || !og::GetCurrentContext()) return false;
    g_Device = device;
    g_Context = context;
    g_Device->AddRef();
    g_Context->AddRef();
    og::IO& io = og::GetIO();
    io.backend_renderer_name = "osgui_impl_dx11";
    io.backend_flags |= og::BackendFlags_RendererHasTextures | og::BackendFlags_RendererHasVtxOffset;
    return true;
}

bool OG_ImplDX11_CreateDeviceObjects() {
    if (!g_Device || !g_Context || !og::GetCurrentContext()) return false;
    if (g_VertexShader) return true;

    const char* shader_source =
        "cbuffer vertexBuffer:register(b0){float4x4 ProjectionMatrix;};"
        "struct VS_IN{float2 pos:POSITION;float2 uv:TEXCOORD0;float4 col:COLOR0;};"
        "struct PS_IN{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;float4 col:COLOR0;};"
        "PS_IN VSMain(VS_IN i){PS_IN o;o.pos=mul(ProjectionMatrix,float4(i.pos.xy,0,1));o.uv=i.uv;o.col=i.col;return o;}"
        "Texture2D texture0:register(t0);SamplerState sampler0:register(s0);"
        "float4 PSMain(PS_IN i):SV_Target{return i.col*texture0.Sample(sampler0,i.uv);}";

    ID3DBlob* vertex_blob = 0;
    ID3DBlob* pixel_blob = 0;
    ID3DBlob* errors = 0;
    HRESULT result = D3DCompile(shader_source, strlen(shader_source), 0, 0, 0,
                                "VSMain", "vs_4_0", 0, 0, &vertex_blob, &errors);
    if (errors) { errors->Release(); errors = 0; }
    if (FAILED(result) || !vertex_blob) return false;
    result = D3DCompile(shader_source, strlen(shader_source), 0, 0, 0,
                        "PSMain", "ps_4_0", 0, 0, &pixel_blob, &errors);
    if (errors) { errors->Release(); errors = 0; }
    if (FAILED(result) || !pixel_blob) { vertex_blob->Release(); return false; }

    result = g_Device->CreateVertexShader(vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), 0, &g_VertexShader);
    if (SUCCEEDED(result))
        result = g_Device->CreatePixelShader(pixel_blob->GetBufferPointer(), pixel_blob->GetBufferSize(), 0, &g_PixelShader);
    D3D11_INPUT_ELEMENT_DESC input_elements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(og::DrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(og::DrawVert, uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(og::DrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    if (SUCCEEDED(result))
        result = g_Device->CreateInputLayout(input_elements, 3, vertex_blob->GetBufferPointer(),
                                              vertex_blob->GetBufferSize(), &g_InputLayout);
    vertex_blob->Release();
    pixel_blob->Release();
    if (FAILED(result)) { OG_ImplDX11_InvalidateDeviceObjects(); return false; }

    D3D11_BUFFER_DESC constant_desc;
    memset(&constant_desc, 0, sizeof(constant_desc));
    constant_desc.ByteWidth = sizeof(OG_DX11VertexConstantBuffer);
    constant_desc.Usage = D3D11_USAGE_DYNAMIC;
    constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constant_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_Device->CreateBuffer(&constant_desc, 0, &g_ConstantBuffer))) {
        OG_ImplDX11_InvalidateDeviceObjects(); return false;
    }

    D3D11_SAMPLER_DESC sampler_desc;
    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(g_Device->CreateSamplerState(&sampler_desc, &g_Sampler))) {
        OG_ImplDX11_InvalidateDeviceObjects(); return false;
    }

    D3D11_BLEND_DESC blend_desc;
    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(g_Device->CreateBlendState(&blend_desc, &g_BlendState))) {
        OG_ImplDX11_InvalidateDeviceObjects(); return false;
    }

    D3D11_RASTERIZER_DESC raster_desc;
    memset(&raster_desc, 0, sizeof(raster_desc));
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_NONE;
    raster_desc.ScissorEnable = TRUE;
    raster_desc.DepthClipEnable = TRUE;
    if (FAILED(g_Device->CreateRasterizerState(&raster_desc, &g_RasterizerState))) {
        OG_ImplDX11_InvalidateDeviceObjects(); return false;
    }

    D3D11_DEPTH_STENCIL_DESC depth_desc;
    memset(&depth_desc, 0, sizeof(depth_desc));
    depth_desc.DepthEnable = FALSE;
    depth_desc.StencilEnable = FALSE;
    if (FAILED(g_Device->CreateDepthStencilState(&depth_desc, &g_DepthStencilState)) ||
        !OG_ImplDX11_CreateFontsTexture()) {
        OG_ImplDX11_InvalidateDeviceObjects(); return false;
    }
    return true;
}

void OG_ImplDX11_InvalidateDeviceObjects() {
    OG_ImplDX11_DestroyFontsTexture();
    if (g_VertexBuffer) { g_VertexBuffer->Release(); g_VertexBuffer = 0; }
    if (g_IndexBuffer) { g_IndexBuffer->Release(); g_IndexBuffer = 0; }
    if (g_VertexShader) { g_VertexShader->Release(); g_VertexShader = 0; }
    if (g_PixelShader) { g_PixelShader->Release(); g_PixelShader = 0; }
    if (g_InputLayout) { g_InputLayout->Release(); g_InputLayout = 0; }
    if (g_ConstantBuffer) { g_ConstantBuffer->Release(); g_ConstantBuffer = 0; }
    if (g_Sampler) { g_Sampler->Release(); g_Sampler = 0; }
    if (g_BlendState) { g_BlendState->Release(); g_BlendState = 0; }
    if (g_RasterizerState) { g_RasterizerState->Release(); g_RasterizerState = 0; }
    if (g_DepthStencilState) { g_DepthStencilState->Release(); g_DepthStencilState = 0; }
    g_VertexBufferSize = 5000;
    g_IndexBufferSize = 10000;
}

void OG_ImplDX11_NewFrame() {
    if (!g_Device || !og::GetCurrentContext()) return;
    if (!g_VertexShader) OG_ImplDX11_CreateDeviceObjects();
    else if (og::GetFontAtlas().tex_id != g_FontTextureID) OG_ImplDX11_CreateFontsTexture();
}

struct OG_DX11StateBackup {
    ID3D11InputLayout* input_layout;
    ID3D11Buffer* vertex_buffer;
    UINT vertex_stride, vertex_offset;
    ID3D11Buffer* index_buffer;
    DXGI_FORMAT index_format;
    UINT index_offset;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    ID3D11VertexShader* vertex_shader;
    ID3D11PixelShader* pixel_shader;
    ID3D11GeometryShader* geometry_shader;
    ID3D11Buffer* vertex_constant_buffer;
    ID3D11ShaderResourceView* pixel_resource;
    ID3D11SamplerState* pixel_sampler;
    ID3D11RasterizerState* rasterizer_state;
    UINT viewport_count, scissor_count;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11BlendState* blend_state;
    FLOAT blend_factor[4];
    UINT sample_mask;
    ID3D11DepthStencilState* depth_state;
    UINT stencil_ref;
    ID3D11RenderTargetView* render_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ID3D11DepthStencilView* depth_view;

    OG_DX11StateBackup() { memset(this, 0, sizeof(*this)); }
};

static void OG_ImplDX11_BackupState(OG_DX11StateBackup& state) {
    g_Context->IAGetInputLayout(&state.input_layout);
    g_Context->IAGetVertexBuffers(0, 1, &state.vertex_buffer, &state.vertex_stride, &state.vertex_offset);
    g_Context->IAGetIndexBuffer(&state.index_buffer, &state.index_format, &state.index_offset);
    g_Context->IAGetPrimitiveTopology(&state.topology);
    g_Context->VSGetShader(&state.vertex_shader, 0, 0);
    g_Context->PSGetShader(&state.pixel_shader, 0, 0);
    g_Context->GSGetShader(&state.geometry_shader, 0, 0);
    g_Context->VSGetConstantBuffers(0, 1, &state.vertex_constant_buffer);
    g_Context->PSGetShaderResources(0, 1, &state.pixel_resource);
    g_Context->PSGetSamplers(0, 1, &state.pixel_sampler);
    g_Context->RSGetState(&state.rasterizer_state);
    state.viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    state.scissor_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_Context->RSGetViewports(&state.viewport_count, state.viewports);
    g_Context->RSGetScissorRects(&state.scissor_count, state.scissors);
    g_Context->OMGetBlendState(&state.blend_state, state.blend_factor, &state.sample_mask);
    g_Context->OMGetDepthStencilState(&state.depth_state, &state.stencil_ref);
    g_Context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, state.render_targets, &state.depth_view);
}

static void OG_ImplDX11_ReleaseState(OG_DX11StateBackup& state) {
#define OG_RELEASE(item) do { if (item) { item->Release(); item = 0; } } while (0)
    OG_RELEASE(state.input_layout); OG_RELEASE(state.vertex_buffer); OG_RELEASE(state.index_buffer);
    OG_RELEASE(state.vertex_shader); OG_RELEASE(state.pixel_shader); OG_RELEASE(state.geometry_shader);
    OG_RELEASE(state.vertex_constant_buffer); OG_RELEASE(state.pixel_resource); OG_RELEASE(state.pixel_sampler);
    OG_RELEASE(state.rasterizer_state); OG_RELEASE(state.blend_state); OG_RELEASE(state.depth_state);
    for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) OG_RELEASE(state.render_targets[i]);
    OG_RELEASE(state.depth_view);
#undef OG_RELEASE
}

static void OG_ImplDX11_SetupRenderState(const og::DrawData* data, int framebuffer_width,
                                          int framebuffer_height, const OG_DX11StateBackup& host) {
    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (FLOAT)framebuffer_width, (FLOAT)framebuffer_height, 0.0f, 1.0f };
    g_Context->RSSetViewports(1, &viewport);
    UINT stride = sizeof(og::DrawVert), offset = 0;
    g_Context->IASetInputLayout(g_InputLayout);
    g_Context->IASetVertexBuffers(0, 1, &g_VertexBuffer, &stride, &offset);
    g_Context->IASetIndexBuffer(g_IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    g_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_Context->VSSetShader(g_VertexShader, 0, 0);
    g_Context->VSSetConstantBuffers(0, 1, &g_ConstantBuffer);
    g_Context->PSSetShader(g_PixelShader, 0, 0);
    g_Context->PSSetSamplers(0, 1, &g_Sampler);
    g_Context->GSSetShader(0, 0, 0);
    const float blend_factor[4] = { 0, 0, 0, 0 };
    g_Context->OMSetBlendState(g_BlendState, blend_factor, 0xffffffffu);
    g_Context->OMSetDepthStencilState(g_DepthStencilState, 0);
    g_Context->RSSetState(g_RasterizerState);
    g_Context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, host.render_targets, host.depth_view);
    (void)data;
}

static void OG_ImplDX11_RestoreState(OG_DX11StateBackup& state) {
    g_Context->IASetInputLayout(state.input_layout);
    g_Context->IASetVertexBuffers(0, 1, &state.vertex_buffer, &state.vertex_stride, &state.vertex_offset);
    g_Context->IASetIndexBuffer(state.index_buffer, state.index_format, state.index_offset);
    g_Context->IASetPrimitiveTopology(state.topology);
    g_Context->VSSetShader(state.vertex_shader, 0, 0);
    g_Context->PSSetShader(state.pixel_shader, 0, 0);
    g_Context->GSSetShader(state.geometry_shader, 0, 0);
    g_Context->VSSetConstantBuffers(0, 1, &state.vertex_constant_buffer);
    g_Context->PSSetShaderResources(0, 1, &state.pixel_resource);
    g_Context->PSSetSamplers(0, 1, &state.pixel_sampler);
    g_Context->RSSetState(state.rasterizer_state);
    g_Context->RSSetViewports(state.viewport_count, state.viewports);
    g_Context->RSSetScissorRects(state.scissor_count, state.scissors);
    g_Context->OMSetBlendState(state.blend_state, state.blend_factor, state.sample_mask);
    g_Context->OMSetDepthStencilState(state.depth_state, state.stencil_ref);
    g_Context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, state.render_targets, state.depth_view);
    OG_ImplDX11_ReleaseState(state);
}

void OG_ImplDX11_RenderDrawData(og::DrawData* data) {
    if (!data || !g_Context || !og::GetCurrentContext() ||
        data->display_size.x <= 0.0f || data->display_size.y <= 0.0f) return;
    if (!g_VertexShader && !OG_ImplDX11_CreateDeviceObjects()) return;

    size_t total_vertices = 0, total_indices = 0;
    for (size_t i = 0; i < data->lists.size(); ++i) {
        if (!data->lists[i]) continue;
        if (data->lists[i]->vtx.size() > (std::numeric_limits<size_t>::max)() - total_vertices ||
            data->lists[i]->idx.size() > (std::numeric_limits<size_t>::max)() - total_indices) return;
        total_vertices += data->lists[i]->vtx.size();
        total_indices += data->lists[i]->idx.size();
    }
    if (total_vertices > (size_t)(std::numeric_limits<int>::max)() - 5000u ||
        total_indices > (size_t)(std::numeric_limits<int>::max)() - 10000u ||
        total_vertices + 5000u > (size_t)(std::numeric_limits<UINT>::max)() / sizeof(og::DrawVert) ||
        total_indices + 10000u > (size_t)(std::numeric_limits<UINT>::max)() / sizeof(og::DrawIdx)) return;

    if (total_vertices > 0 && (!g_VertexBuffer || g_VertexBufferSize < (int)total_vertices)) {
        if (g_VertexBuffer) { g_VertexBuffer->Release(); g_VertexBuffer = 0; }
        g_VertexBufferSize = (int)total_vertices + 5000;
        D3D11_BUFFER_DESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = (UINT)((size_t)g_VertexBufferSize * sizeof(og::DrawVert));
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_Device->CreateBuffer(&desc, 0, &g_VertexBuffer))) return;
    }
    if (total_indices > 0 && (!g_IndexBuffer || g_IndexBufferSize < (int)total_indices)) {
        if (g_IndexBuffer) { g_IndexBuffer->Release(); g_IndexBuffer = 0; }
        g_IndexBufferSize = (int)total_indices + 10000;
        D3D11_BUFFER_DESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = (UINT)((size_t)g_IndexBufferSize * sizeof(og::DrawIdx));
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_Device->CreateBuffer(&desc, 0, &g_IndexBuffer))) return;
    }

    if (total_vertices > 0 || total_indices > 0) {
        D3D11_MAPPED_SUBRESOURCE vertex_map, index_map;
        memset(&vertex_map, 0, sizeof(vertex_map)); memset(&index_map, 0, sizeof(index_map));
        if (total_vertices > 0 && FAILED(g_Context->Map(g_VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &vertex_map))) return;
        if (total_indices > 0 && FAILED(g_Context->Map(g_IndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &index_map))) {
            if (total_vertices > 0) g_Context->Unmap(g_VertexBuffer, 0);
            return;
        }
        og::DrawVert* vertex_destination = (og::DrawVert*)vertex_map.pData;
        og::DrawIdx* index_destination = (og::DrawIdx*)index_map.pData;
        for (size_t i = 0; i < data->lists.size(); ++i) {
            og::DrawList* list = data->lists[i]; if (!list) continue;
            if (!list->vtx.empty()) { memcpy(vertex_destination, &list->vtx[0], list->vtx.size() * sizeof(*vertex_destination)); vertex_destination += list->vtx.size(); }
            if (!list->idx.empty()) { memcpy(index_destination, &list->idx[0], list->idx.size() * sizeof(*index_destination)); index_destination += list->idx.size(); }
        }
        if (total_vertices > 0) g_Context->Unmap(g_VertexBuffer, 0);
        if (total_indices > 0) g_Context->Unmap(g_IndexBuffer, 0);
    }

    D3D11_MAPPED_SUBRESOURCE constant_map;
    memset(&constant_map, 0, sizeof(constant_map));
    if (FAILED(g_Context->Map(g_ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constant_map))) return;
    float left = data->display_pos.x, right = left + data->display_size.x;
    float top = data->display_pos.y, bottom = top + data->display_size.y;
    float projection[4][4] = {
        { 2.0f / (right - left), 0, 0, 0 }, { 0, 2.0f / (top - bottom), 0, 0 },
        { 0, 0, 0.5f, 0 }, { (right + left) / (left - right), (top + bottom) / (bottom - top), 0.5f, 1 }
    };
    memcpy(constant_map.pData, projection, sizeof(projection));
    g_Context->Unmap(g_ConstantBuffer, 0);

    const og::Vec2 scale = og::GetIO().framebuffer_scale;
    int framebuffer_width = (int)(data->display_size.x * scale.x);
    int framebuffer_height = (int)(data->display_size.y * scale.y);
    if (framebuffer_width <= 0 || framebuffer_height <= 0) return;

    OG_DX11StateBackup host;
    OG_ImplDX11_BackupState(host);
    OG_ImplDX11_SetupRenderState(data, framebuffer_width, framebuffer_height, host);

    int global_vertex_offset = 0, global_index_offset = 0;
    for (size_t list_index = 0; list_index < data->lists.size(); ++list_index) {
        og::DrawList* list = data->lists[list_index];
        if (!list) continue;
        for (size_t command_index = 0; command_index < list->cmds.size(); ++command_index) {
            const og::DrawCmd& command = list->cmds[command_index];
            if (command.callback) {
                command.callback(list, &command);
                OG_ImplDX11_SetupRenderState(data, framebuffer_width, framebuffer_height, host);
                continue;
            }
            if (!command.elem_count || (size_t)command.vtx_offset >= list->vtx.size() ||
                (size_t)command.idx_offset + (size_t)command.elem_count > list->idx.size()) continue;
            LONG clip_left = (LONG)((command.clip_rect.x - data->display_pos.x) * scale.x);
            LONG clip_top = (LONG)((command.clip_rect.y - data->display_pos.y) * scale.y);
            LONG clip_right = (LONG)((command.clip_rect.z - data->display_pos.x) * scale.x);
            LONG clip_bottom = (LONG)((command.clip_rect.w - data->display_pos.y) * scale.y);
            if (clip_left < 0) clip_left = 0; if (clip_top < 0) clip_top = 0;
            if (clip_right > framebuffer_width) clip_right = framebuffer_width;
            if (clip_bottom > framebuffer_height) clip_bottom = framebuffer_height;
            if (clip_right <= clip_left || clip_bottom <= clip_top) continue;
            ID3D11ShaderResourceView* texture = OG_ImplDX11_FindTexture(command.tex_id);
            if (!texture) continue;
            D3D11_RECT scissor = { clip_left, clip_top, clip_right, clip_bottom };
            g_Context->RSSetScissorRects(1, &scissor);
            g_Context->PSSetShaderResources(0, 1, &texture);
            g_Context->DrawIndexed(command.elem_count, command.idx_offset + global_index_offset,
                                   global_vertex_offset + (INT)command.vtx_offset);
        }
        global_index_offset += (int)list->idx.size();
        global_vertex_offset += (int)list->vtx.size();
    }

    OG_ImplDX11_RestoreState(host);
}

void OG_ImplDX11_Shutdown() {
    for (std::map<og::TextureID, ID3D11ShaderResourceView*>::iterator it = g_Textures.begin();
         it != g_Textures.end(); ++it) it->second->Release();
    g_Textures.clear();
    g_NextTexture = 2;
    OG_ImplDX11_InvalidateDeviceObjects();
    if (g_Context) { g_Context->Release(); g_Context = 0; }
    if (g_Device) { g_Device->Release(); g_Device = 0; }
    if (og::GetCurrentContext()) {
        og::IO& io = og::GetIO();
        if (io.backend_renderer_name && strcmp(io.backend_renderer_name, "osgui_impl_dx11") == 0)
            io.backend_renderer_name = 0;
        io.backend_flags &= ~(og::BackendFlags_RendererHasTextures |
                              og::BackendFlags_RendererHasVtxOffset);
    }
}
