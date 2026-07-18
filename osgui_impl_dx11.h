#pragma once
#include "osgui.h"
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

bool OG_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* context);
void OG_ImplDX11_Shutdown();
void OG_ImplDX11_NewFrame();
void OG_ImplDX11_RenderDrawData(og::DrawData* draw_data);
bool OG_ImplDX11_CreateDeviceObjects();
void OG_ImplDX11_InvalidateDeviceObjects();
og::TextureID OG_ImplDX11_RegisterTexture(ID3D11ShaderResourceView* texture);
void OG_ImplDX11_UnregisterTexture(og::TextureID texture_id);
