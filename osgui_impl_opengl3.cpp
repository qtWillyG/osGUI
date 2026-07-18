#include "osgui_impl_opengl3.h"
#include <limits>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef ptrdiff_t GLsizeiptr;
typedef float GLfloat;
typedef char GLchar;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_TRIANGLES 0x0004
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_SAMPLER_BINDING 0x8919
#define GL_RGBA 0x1908
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_STREAM_DRAW 0x88E0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_VIEWPORT 0x0BA2
#define GL_SCISSOR_BOX 0x0C10
#define GL_BLEND_SRC_RGB 0x80C9
#define GL_BLEND_DST_RGB 0x80C8
#define GL_BLEND_SRC_ALPHA 0x80CB
#define GL_BLEND_DST_ALPHA 0x80CA
#define GL_BLEND_EQUATION_RGB 0x8009
#define GL_BLEND_EQUATION_ALPHA 0x883D
#define GL_FUNC_ADD 0x8006
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_NO_ERROR 0

#define GLPROC(ret, name, args) typedef ret (*PFN_##name) args; static PFN_##name p##name = 0
GLPROC(void, ActiveTexture, (GLenum));
GLPROC(void, AttachShader, (GLuint, GLuint));
GLPROC(void, BindBuffer, (GLenum, GLuint));
GLPROC(void, BindSampler, (GLuint, GLuint));
GLPROC(void, BindTexture, (GLenum, GLuint));
GLPROC(void, BindVertexArray, (GLuint));
GLPROC(void, BlendEquationSeparate, (GLenum, GLenum));
GLPROC(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum));
GLPROC(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum));
GLPROC(void, CompileShader, (GLuint));
GLPROC(GLuint, CreateProgram, (void));
GLPROC(GLuint, CreateShader, (GLenum));
GLPROC(void, DeleteBuffers, (GLsizei, const GLuint*));
GLPROC(void, DeleteProgram, (GLuint));
GLPROC(void, DeleteShader, (GLuint));
GLPROC(void, DeleteTextures, (GLsizei, const GLuint*));
GLPROC(void, DeleteVertexArrays, (GLsizei, const GLuint*));
GLPROC(void, Disable, (GLenum));
GLPROC(void, DrawElements, (GLenum, GLsizei, GLenum, const void*));
GLPROC(void, Enable, (GLenum));
GLPROC(void, EnableVertexAttribArray, (GLuint));
GLPROC(void, GenBuffers, (GLsizei, GLuint*));
GLPROC(void, GenTextures, (GLsizei, GLuint*));
GLPROC(void, GenVertexArrays, (GLsizei, GLuint*));
GLPROC(void, GetIntegerv, (GLenum, GLint*));
GLPROC(GLenum, GetError, (void));
GLPROC(void, GetProgramiv, (GLuint, GLenum, GLint*));
GLPROC(void, GetShaderiv, (GLuint, GLenum, GLint*));
GLPROC(GLint, GetUniformLocation, (GLuint, const GLchar*));
GLPROC(GLboolean, IsEnabled, (GLenum));
GLPROC(void, LinkProgram, (GLuint));
GLPROC(void, PixelStorei, (GLenum, GLint));
GLPROC(void, Scissor, (GLint, GLint, GLsizei, GLsizei));
GLPROC(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*));
GLPROC(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*));
GLPROC(void, TexParameteri, (GLenum, GLenum, GLint));
GLPROC(void, Uniform1i, (GLint, GLint));
GLPROC(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*));
GLPROC(void, UseProgram, (GLuint));
GLPROC(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*));
GLPROC(void, Viewport, (GLint, GLint, GLsizei, GLsizei));
#undef GLPROC

static GLuint g_Shader = 0;
static GLuint g_Vbo = 0;
static GLuint g_Ebo = 0;
static GLuint g_Vao = 0;
static GLuint g_Font = 0;
static GLint g_Proj = -1;
static GLint g_Texture = -1;
static bool g_Initialized = false;

static GLuint OG_ImplOpenGL3_Compile(GLenum type, const char* source) {
    GLuint shader = pCreateShader(type);
    if (!shader) return 0;
    pShaderSource(shader, 1, &source, 0);
    pCompileShader(shader);
    GLint compiled = 0;
    pGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) { pDeleteShader(shader); return 0; }
    return shader;
}

static void OG_ImplOpenGL3_ClearRendererFlags() {
    if (!og::GetCurrentContext()) return;
    og::IO& io = og::GetIO();
    if (io.backend_renderer_name && strcmp(io.backend_renderer_name, "osgui_impl_opengl3") == 0)
        io.backend_renderer_name = 0;
    io.backend_flags &= ~(og::BackendFlags_RendererHasTextures |
                          og::BackendFlags_RendererHasVtxOffset);
}

bool OG_ImplOpenGL3_Init(OG_GLGetProcAddress loader) {
    if (!loader || g_Initialized || !og::GetCurrentContext()) return false;
#define LOAD(name) p##name = (PFN_##name)loader("gl" #name); if (!p##name) return false
    LOAD(ActiveTexture); LOAD(AttachShader); LOAD(BindBuffer); LOAD(BindSampler);
    LOAD(BindTexture); LOAD(BindVertexArray); LOAD(BlendEquationSeparate); LOAD(BlendFuncSeparate);
    LOAD(BufferData); LOAD(CompileShader); LOAD(CreateProgram); LOAD(CreateShader);
    LOAD(DeleteBuffers); LOAD(DeleteProgram); LOAD(DeleteShader); LOAD(DeleteTextures);
    LOAD(DeleteVertexArrays); LOAD(Disable); LOAD(DrawElements); LOAD(Enable);
    LOAD(EnableVertexAttribArray); LOAD(GenBuffers); LOAD(GenTextures); LOAD(GenVertexArrays);
    LOAD(GetIntegerv); LOAD(GetError); LOAD(GetProgramiv); LOAD(GetShaderiv); LOAD(GetUniformLocation);
    LOAD(IsEnabled); LOAD(LinkProgram); LOAD(PixelStorei); LOAD(Scissor); LOAD(ShaderSource);
    LOAD(TexImage2D); LOAD(TexParameteri); LOAD(Uniform1i); LOAD(UniformMatrix4fv);
    LOAD(UseProgram); LOAD(VertexAttribPointer); LOAD(Viewport);
#undef LOAD

    const char* vertex_source =
        "#version 330 core\n"
        "layout(location=0)in vec2 Position;layout(location=1)in vec2 UV;layout(location=2)in vec4 Color;"
        "uniform mat4 ProjMtx;out vec2 FragUV;out vec4 FragColor;"
        "void main(){FragUV=UV;FragColor=Color;gl_Position=ProjMtx*vec4(Position,0,1);}";
    const char* fragment_source =
        "#version 330 core\n"
        "in vec2 FragUV;in vec4 FragColor;uniform sampler2D Texture;out vec4 OutColor;"
        "void main(){OutColor=FragColor*texture(Texture,FragUV);}";

    GLuint vertex_shader = OG_ImplOpenGL3_Compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment_shader = OG_ImplOpenGL3_Compile(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex_shader || !fragment_shader) {
        if (vertex_shader) pDeleteShader(vertex_shader);
        if (fragment_shader) pDeleteShader(fragment_shader);
        return false;
    }
    GLuint program = pCreateProgram();
    if (!program) { pDeleteShader(vertex_shader); pDeleteShader(fragment_shader); return false; }
    pAttachShader(program, vertex_shader);
    pAttachShader(program, fragment_shader);
    pLinkProgram(program);
    pDeleteShader(vertex_shader);
    pDeleteShader(fragment_shader);
    GLint linked = 0;
    pGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) { pDeleteProgram(program); return false; }

    g_Shader = program;
    g_Proj = pGetUniformLocation(g_Shader, "ProjMtx");
    g_Texture = pGetUniformLocation(g_Shader, "Texture");
    pGenBuffers(1, &g_Vbo);
    pGenBuffers(1, &g_Ebo);
    pGenVertexArrays(1, &g_Vao);
    if (!g_Vbo || !g_Ebo || !g_Vao || g_Proj < 0 || g_Texture < 0) {
        if (g_Vbo) pDeleteBuffers(1, &g_Vbo);
        if (g_Ebo) pDeleteBuffers(1, &g_Ebo);
        if (g_Vao) pDeleteVertexArrays(1, &g_Vao);
        pDeleteProgram(g_Shader);
        g_Vbo = g_Ebo = g_Vao = g_Shader = 0;
        return false;
    }

    og::IO& io = og::GetIO();
    io.backend_renderer_name = "osgui_impl_opengl3";
    io.backend_flags |= og::BackendFlags_RendererHasTextures | og::BackendFlags_RendererHasVtxOffset;
    g_Initialized = true;
    return true;
}

void OG_ImplOpenGL3_DestroyFontsTexture() {
    const GLuint old_font = g_Font;
    if (g_Font && pDeleteTextures) { pDeleteTextures(1, &g_Font); g_Font = 0; }
    if (og::GetCurrentContext() && og::GetFontAtlas().tex_id == (og::TextureID)old_font)
        og::GetFontAtlas().tex_id = 0;
}

bool OG_ImplOpenGL3_CreateFontsTexture() {
    if (!g_Initialized || !og::GetCurrentContext()) return false;
    og::FontAtlas& atlas = og::GetFontAtlas();
    if (!atlas.pixels || atlas.width <= 0 || atlas.height <= 0) return false;

    GLint maximum_texture_size = 0;
    pGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
    if (maximum_texture_size <= 0 || atlas.width > maximum_texture_size || atlas.height > maximum_texture_size)
        return false;
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

    GLint previous_active_texture = 0, previous_texture = 0, previous_unpack = 0;
    pGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
    pActiveTexture(GL_TEXTURE0);
    pGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    pGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_unpack);

    GLuint new_font = 0;
    pGenTextures(1, &new_font);
    if (!new_font) { pActiveTexture((GLenum)previous_active_texture); free(rgba); return false; }
    pBindTexture(GL_TEXTURE_2D, new_font);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    pPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    while (pGetError() != GL_NO_ERROR) {}
    pTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.width, atlas.height, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    const GLenum upload_error = pGetError();
    free(rgba);

    pPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack);
    pBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture);
    pActiveTexture((GLenum)previous_active_texture);
    if (upload_error != GL_NO_ERROR) { pDeleteTextures(1, &new_font); return false; }
    if (g_Font) pDeleteTextures(1, &g_Font);
    g_Font = new_font;
    atlas.tex_id = (og::TextureID)g_Font;
    return true;
}

void OG_ImplOpenGL3_NewFrame() {
    if (!g_Initialized || !og::GetCurrentContext()) return;
    if (!g_Font || og::GetFontAtlas().tex_id != (og::TextureID)g_Font) {
        if (g_Font) OG_ImplOpenGL3_DestroyFontsTexture();
        OG_ImplOpenGL3_CreateFontsTexture();
    }
}

struct OG_OpenGL3State {
    GLint active_texture, program, texture, sampler, array_buffer, element_buffer, vertex_array;
    GLint viewport[4], scissor[4];
    GLint blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;
    GLint blend_equation_rgb, blend_equation_alpha;
    GLboolean blend, cull, depth, scissor_test;
};

static void OG_ImplOpenGL3_SetupRenderState(const og::DrawData* data, int width, int height) {
    pEnable(GL_BLEND);
    pBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    pBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    pDisable(GL_CULL_FACE);
    pDisable(GL_DEPTH_TEST);
    pEnable(GL_SCISSOR_TEST);
    pViewport(0, 0, width, height);
    pActiveTexture(GL_TEXTURE0);
    pBindSampler(0, 0);
    pUseProgram(g_Shader);
    pUniform1i(g_Texture, 0);

    float left = data->display_pos.x;
    float right = left + data->display_size.x;
    float top = data->display_pos.y;
    float bottom = top + data->display_size.y;
    const float projection[16] = {
        2.0f / (right - left), 0, 0, 0,
        0, 2.0f / (top - bottom), 0, 0,
        0, 0, -1.0f, 0,
        (right + left) / (left - right), (top + bottom) / (bottom - top), 0, 1
    };
    pUniformMatrix4fv(g_Proj, 1, GL_FALSE, projection);
    pBindVertexArray(g_Vao);
    pBindBuffer(GL_ARRAY_BUFFER, g_Vbo);
    pBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_Ebo);
    pEnableVertexAttribArray(0);
    pEnableVertexAttribArray(1);
    pEnableVertexAttribArray(2);
    pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(og::DrawVert), (void*)offsetof(og::DrawVert, pos));
    pVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(og::DrawVert), (void*)offsetof(og::DrawVert, uv));
    pVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(og::DrawVert), (void*)offsetof(og::DrawVert, col));
}

void OG_ImplOpenGL3_RenderDrawData(og::DrawData* data) {
    if (!g_Initialized || !data || !og::GetCurrentContext()) return;
    const og::Vec2 scale = og::GetIO().framebuffer_scale;
    int width = (int)(data->display_size.x * scale.x);
    int height = (int)(data->display_size.y * scale.y);
    if (width <= 0 || height <= 0) return;

    OG_OpenGL3State previous;
    pGetIntegerv(GL_ACTIVE_TEXTURE, &previous.active_texture);
    pActiveTexture(GL_TEXTURE0);
    pGetIntegerv(GL_CURRENT_PROGRAM, &previous.program);
    pGetIntegerv(GL_TEXTURE_BINDING_2D, &previous.texture);
    pGetIntegerv(GL_SAMPLER_BINDING, &previous.sampler);
    pGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous.array_buffer);
    pGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previous.element_buffer);
    pGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous.vertex_array);
    pGetIntegerv(GL_VIEWPORT, previous.viewport);
    pGetIntegerv(GL_SCISSOR_BOX, previous.scissor);
    pGetIntegerv(GL_BLEND_SRC_RGB, &previous.blend_src_rgb);
    pGetIntegerv(GL_BLEND_DST_RGB, &previous.blend_dst_rgb);
    pGetIntegerv(GL_BLEND_SRC_ALPHA, &previous.blend_src_alpha);
    pGetIntegerv(GL_BLEND_DST_ALPHA, &previous.blend_dst_alpha);
    pGetIntegerv(GL_BLEND_EQUATION_RGB, &previous.blend_equation_rgb);
    pGetIntegerv(GL_BLEND_EQUATION_ALPHA, &previous.blend_equation_alpha);
    previous.blend = pIsEnabled(GL_BLEND);
    previous.cull = pIsEnabled(GL_CULL_FACE);
    previous.depth = pIsEnabled(GL_DEPTH_TEST);
    previous.scissor_test = pIsEnabled(GL_SCISSOR_TEST);

    OG_ImplOpenGL3_SetupRenderState(data, width, height);
    for (size_t list_index = 0; list_index < data->lists.size(); ++list_index) {
        og::DrawList* list = data->lists[list_index];
        if (!list) continue;
        const size_t maximum_buffer_size = (size_t)(std::numeric_limits<GLsizeiptr>::max)();
        if (list->vtx.size() > maximum_buffer_size / sizeof(og::DrawVert) ||
            list->idx.size() > maximum_buffer_size / sizeof(og::DrawIdx)) continue;
        pBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(list->vtx.size() * sizeof(og::DrawVert)),
                    list->vtx.empty() ? 0 : &list->vtx[0], GL_STREAM_DRAW);
        pBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(list->idx.size() * sizeof(og::DrawIdx)),
                    list->idx.empty() ? 0 : &list->idx[0], GL_STREAM_DRAW);
        unsigned int current_vertex_offset = ~0u;

        for (size_t command_index = 0; command_index < list->cmds.size(); ++command_index) {
            const og::DrawCmd& command = list->cmds[command_index];
            if (command.callback) {
                command.callback(list, &command);
                OG_ImplOpenGL3_SetupRenderState(data, width, height);
                current_vertex_offset = ~0u;
                continue;
            }
            if (!command.elem_count ||
                command.elem_count > (unsigned int)(std::numeric_limits<GLsizei>::max)() ||
                (size_t)command.vtx_offset >= list->vtx.size() ||
                (size_t)command.idx_offset + (size_t)command.elem_count > list->idx.size()) continue;

            int clip_x = (int)((command.clip_rect.x - data->display_pos.x) * scale.x);
            int clip_y = (int)((command.clip_rect.y - data->display_pos.y) * scale.y);
            int clip_z = (int)((command.clip_rect.z - data->display_pos.x) * scale.x);
            int clip_w = (int)((command.clip_rect.w - data->display_pos.y) * scale.y);
            if (clip_x < 0) clip_x = 0;
            if (clip_y < 0) clip_y = 0;
            if (clip_z > width) clip_z = width;
            if (clip_w > height) clip_w = height;
            if (clip_z <= clip_x || clip_w <= clip_y) continue;

            if (current_vertex_offset != command.vtx_offset) {
                const size_t base = (size_t)command.vtx_offset * sizeof(og::DrawVert);
                pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(og::DrawVert), (void*)(base + offsetof(og::DrawVert, pos)));
                pVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(og::DrawVert), (void*)(base + offsetof(og::DrawVert, uv)));
                pVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(og::DrawVert), (void*)(base + offsetof(og::DrawVert, col)));
                current_vertex_offset = command.vtx_offset;
            }
            pScissor(clip_x, height - clip_w, clip_z - clip_x, clip_w - clip_y);
            if (command.tex_id > (og::TextureID)(std::numeric_limits<GLuint>::max)()) continue;
            pBindTexture(GL_TEXTURE_2D, (GLuint)command.tex_id);
            pDrawElements(GL_TRIANGLES, (GLsizei)command.elem_count, GL_UNSIGNED_INT,
                          (void*)((size_t)command.idx_offset * sizeof(og::DrawIdx)));
        }
    }

    pUseProgram((GLuint)previous.program);
    pBindTexture(GL_TEXTURE_2D, (GLuint)previous.texture);
    pBindSampler(0, (GLuint)previous.sampler);
    pBindVertexArray((GLuint)previous.vertex_array);
    pBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous.array_buffer);
    pBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)previous.element_buffer);
    pBlendEquationSeparate((GLenum)previous.blend_equation_rgb, (GLenum)previous.blend_equation_alpha);
    pBlendFuncSeparate((GLenum)previous.blend_src_rgb, (GLenum)previous.blend_dst_rgb,
                       (GLenum)previous.blend_src_alpha, (GLenum)previous.blend_dst_alpha);
    if (previous.blend) pEnable(GL_BLEND); else pDisable(GL_BLEND);
    if (previous.cull) pEnable(GL_CULL_FACE); else pDisable(GL_CULL_FACE);
    if (previous.depth) pEnable(GL_DEPTH_TEST); else pDisable(GL_DEPTH_TEST);
    if (previous.scissor_test) pEnable(GL_SCISSOR_TEST); else pDisable(GL_SCISSOR_TEST);
    pViewport(previous.viewport[0], previous.viewport[1], previous.viewport[2], previous.viewport[3]);
    pScissor(previous.scissor[0], previous.scissor[1], previous.scissor[2], previous.scissor[3]);
    pActiveTexture((GLenum)previous.active_texture);
}

void OG_ImplOpenGL3_Shutdown() {
    if (!g_Initialized) return;
    OG_ImplOpenGL3_DestroyFontsTexture();
    if (g_Vbo) pDeleteBuffers(1, &g_Vbo);
    if (g_Ebo) pDeleteBuffers(1, &g_Ebo);
    if (g_Vao) pDeleteVertexArrays(1, &g_Vao);
    if (g_Shader) pDeleteProgram(g_Shader);
    g_Vbo = g_Ebo = g_Vao = g_Shader = 0;
    g_Proj = g_Texture = -1;
    g_Initialized = false;
    OG_ImplOpenGL3_ClearRendererFlags();
}
