// OSGui OpenGL 2-compatible renderer backend.
//
// Uses the fixed-function pipeline (OpenGL 1.x) that ships with opengl32.lib,
// so the demo builds with zero external loaders (no GLAD/GLEW/GLFW).
#include "osgui_impl_opengl2.h"
#include <windows.h>
#include <GL/gl.h>
#include <limits>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static GLuint g_FontTexture = 0;
static GLuint g_BlurTexture = 0;
static int g_BlurWidth = 0, g_BlurHeight = 0;
static GLuint g_BlurProgram = 0;
static bool g_Initialized = false;

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE 0x84E1
#define GL_TEXTURE0 0x84C0
#endif

typedef GLuint (APIENTRY *PFN_CreateShader)(GLenum);
typedef void (APIENTRY *PFN_ShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (APIENTRY *PFN_CompileShader)(GLuint);
typedef void (APIENTRY *PFN_GetShaderiv)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *PFN_DeleteShader)(GLuint);
typedef GLuint (APIENTRY *PFN_CreateProgram)();
typedef void (APIENTRY *PFN_AttachShader)(GLuint, GLuint);
typedef void (APIENTRY *PFN_LinkProgram)(GLuint);
typedef void (APIENTRY *PFN_GetProgramiv)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *PFN_UseProgram)(GLuint);
typedef void (APIENTRY *PFN_DeleteProgram)(GLuint);
typedef GLint (APIENTRY *PFN_GetUniformLocation)(GLuint, const char*);
typedef void (APIENTRY *PFN_Uniform1i)(GLint, GLint);
typedef void (APIENTRY *PFN_Uniform1f)(GLint, GLfloat);
typedef void (APIENTRY *PFN_Uniform2f)(GLint, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_BindBuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_BindVertexArray)(GLuint);
typedef void (APIENTRY *PFN_ActiveTexture)(GLenum);
typedef void (APIENTRY *PFN_ClientActiveTexture)(GLenum);

static PFN_CreateShader pCreateShader = 0;
static PFN_ShaderSource pShaderSource = 0;
static PFN_CompileShader pCompileShader = 0;
static PFN_GetShaderiv pGetShaderiv = 0;
static PFN_DeleteShader pDeleteShader = 0;
static PFN_CreateProgram pCreateProgram = 0;
static PFN_AttachShader pAttachShader = 0;
static PFN_LinkProgram pLinkProgram = 0;
static PFN_GetProgramiv pGetProgramiv = 0;
static PFN_UseProgram pUseProgram = 0;
static PFN_DeleteProgram pDeleteProgram = 0;
static PFN_GetUniformLocation pGetUniformLocation = 0;
static PFN_Uniform1i pUniform1i = 0;
static PFN_Uniform1f pUniform1f = 0;
static PFN_Uniform2f pUniform2f = 0;
static PFN_BindBuffer pBindBuffer = 0;
static PFN_BindVertexArray pBindVertexArray = 0;
static PFN_ActiveTexture pActiveTexture = 0;
static PFN_ClientActiveTexture pClientActiveTexture = 0;

static void* GetGLProc(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (p == 0 || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) return 0;
    return p;
}

static bool CreateBlurShader() {
    pCreateShader = (PFN_CreateShader)GetGLProc("glCreateShader");
    pShaderSource = (PFN_ShaderSource)GetGLProc("glShaderSource");
    pCompileShader = (PFN_CompileShader)GetGLProc("glCompileShader");
    pGetShaderiv = (PFN_GetShaderiv)GetGLProc("glGetShaderiv");
    pDeleteShader = (PFN_DeleteShader)GetGLProc("glDeleteShader");
    pCreateProgram = (PFN_CreateProgram)GetGLProc("glCreateProgram");
    pAttachShader = (PFN_AttachShader)GetGLProc("glAttachShader");
    pLinkProgram = (PFN_LinkProgram)GetGLProc("glLinkProgram");
    pGetProgramiv = (PFN_GetProgramiv)GetGLProc("glGetProgramiv");
    pUseProgram = (PFN_UseProgram)GetGLProc("glUseProgram");
    pDeleteProgram = (PFN_DeleteProgram)GetGLProc("glDeleteProgram");
    pGetUniformLocation = (PFN_GetUniformLocation)GetGLProc("glGetUniformLocation");
    pUniform1i = (PFN_Uniform1i)GetGLProc("glUniform1i");
    pUniform1f = (PFN_Uniform1f)GetGLProc("glUniform1f");
    pUniform2f = (PFN_Uniform2f)GetGLProc("glUniform2f");
    pBindBuffer = (PFN_BindBuffer)GetGLProc("glBindBuffer");
    pBindVertexArray = (PFN_BindVertexArray)GetGLProc("glBindVertexArray");
    pActiveTexture = (PFN_ActiveTexture)GetGLProc("glActiveTexture");
    pClientActiveTexture = (PFN_ClientActiveTexture)GetGLProc("glClientActiveTexture");
    if (!pCreateShader || !pShaderSource || !pCompileShader || !pGetShaderiv || !pDeleteShader ||
        !pCreateProgram || !pAttachShader || !pLinkProgram || !pGetProgramiv || !pUseProgram ||
        !pDeleteProgram || !pGetUniformLocation || !pUniform1i || !pUniform1f || !pUniform2f) return false;

    const char* vs_source =
        "#version 120\n"
        "varying vec4 vColor;\n"
        "void main(){ gl_Position=ftransform(); vColor=gl_Color; }\n";
    const char* fs_source =
        "#version 120\n"
        "uniform sampler2D uScene; uniform vec2 uResolution; uniform float uRadius;\n"
        "varying vec4 vColor;\n"
        "void main(){\n"
        " vec2 uv=gl_FragCoord.xy/uResolution; vec2 d=vec2(uRadius)/uResolution;\n"
        " vec4 c=texture2D(uScene,uv)*0.24;\n"
        " c+=texture2D(uScene,uv+vec2(d.x,0.0))*0.12; c+=texture2D(uScene,uv-vec2(d.x,0.0))*0.12;\n"
        " c+=texture2D(uScene,uv+vec2(0.0,d.y))*0.12; c+=texture2D(uScene,uv-vec2(0.0,d.y))*0.12;\n"
        " c+=texture2D(uScene,uv+d)*0.07; c+=texture2D(uScene,uv-d)*0.07;\n"
        " c+=texture2D(uScene,uv+vec2(d.x,-d.y))*0.07; c+=texture2D(uScene,uv+vec2(-d.x,d.y))*0.07;\n"
        " gl_FragColor=vec4(mix(c.rgb,vColor.rgb,0.38),vColor.a);\n"
        "}\n";
    GLuint vs = pCreateShader(GL_VERTEX_SHADER), fs = pCreateShader(GL_FRAGMENT_SHADER);
    if (!vs || !fs) {
        if (vs) pDeleteShader(vs);
        if (fs) pDeleteShader(fs);
        return false;
    }
    pShaderSource(vs, 1, &vs_source, 0); pCompileShader(vs);
    pShaderSource(fs, 1, &fs_source, 0); pCompileShader(fs);
    GLint ok_vs = 0, ok_fs = 0;
    pGetShaderiv(vs, GL_COMPILE_STATUS, &ok_vs); pGetShaderiv(fs, GL_COMPILE_STATUS, &ok_fs);
    if (!ok_vs || !ok_fs) { pDeleteShader(vs); pDeleteShader(fs); return false; }
    g_BlurProgram = pCreateProgram();
    if (!g_BlurProgram) { pDeleteShader(vs); pDeleteShader(fs); return false; }
    pAttachShader(g_BlurProgram, vs); pAttachShader(g_BlurProgram, fs); pLinkProgram(g_BlurProgram);
    GLint linked = 0; pGetProgramiv(g_BlurProgram, GL_LINK_STATUS, &linked);
    pDeleteShader(vs); pDeleteShader(fs);
    if (!linked) { pDeleteProgram(g_BlurProgram); g_BlurProgram = 0; return false; }
    return true;
}

static bool EnsureBlurTexture(int width, int height) {
    GLint maximum_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
    if (width <= 0 || height <= 0 || maximum_texture_size <= 0 ||
        width > maximum_texture_size || height > maximum_texture_size) return false;
    if (!g_BlurTexture) glGenTextures(1, &g_BlurTexture);
    if (!g_BlurTexture) return false;
    glBindTexture(GL_TEXTURE_2D, g_BlurTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    if (width != g_BlurWidth || height != g_BlurHeight) {
        while (glGetError() != GL_NO_ERROR) {}
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &g_BlurTexture); g_BlurTexture = 0;
            g_BlurWidth = g_BlurHeight = 0; return false;
        }
        g_BlurWidth = width; g_BlurHeight = height;
    }
    return true;
}

bool OG_ImplOpenGL2_CreateFontsTexture() {
    if (!og::GetCurrentContext()) return false;
    og::FontAtlas& atlas = og::GetFontAtlas();
    if (!atlas.pixels || atlas.width <= 0 || atlas.height <= 0) return false;

    // expand R8 coverage into RGBA (white with per-texel alpha) so a single
    // texture serves both solid fills (white texel) and text glyphs.
    int w = atlas.width, h = atlas.height;
    GLint maximum_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
    if (maximum_texture_size <= 0 || w > maximum_texture_size || h > maximum_texture_size) return false;
    const size_t max_size = (std::numeric_limits<size_t>::max)();
    if ((size_t)w > max_size / (size_t)h || (size_t)w * (size_t)h > max_size / 4u) return false;
    const size_t pixel_count = (size_t)w * (size_t)h;
    unsigned char* rgba = (unsigned char*)malloc(pixel_count * 4u);
    if (!rgba) return false;
    for (size_t i = 0; i < pixel_count; i++) {
        unsigned char a = atlas.pixels[i];
        rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = a;
    }

    GLint previous_texture = 0, previous_unpack = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_unpack);
    GLuint new_texture = 0;
    glGenTextures(1, &new_texture);
    if (!new_texture) { free(rgba); return false; }
    glBindTexture(GL_TEXTURE_2D, new_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    while (glGetError() != GL_NO_ERROR) {}
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    const GLenum upload_error = glGetError();
    glPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture);
    if (upload_error != GL_NO_ERROR) { glDeleteTextures(1, &new_texture); return false; }

    if (g_FontTexture) glDeleteTextures(1, &g_FontTexture);
    g_FontTexture = new_texture;
    atlas.tex_id = (og::TextureID)g_FontTexture;
    return true;
}

bool OG_ImplOpenGL2_Init() {
    if (!og::GetCurrentContext() || g_Initialized) return false;
    if (!glGetString(GL_VERSION)) return false;
    const bool effects = CreateBlurShader();
    og::IO& io = og::GetIO();
    io.backend_renderer_name = "osgui_impl_opengl2";
    io.backend_flags |= og::BackendFlags_RendererHasTextures | og::BackendFlags_RendererHasVtxOffset;
    if (effects) io.backend_flags |= og::BackendFlags_RendererHasEffects;
    g_Initialized = true;
    return true;
}
bool OG_ImplOpenGL2_HasShaderEffects() { return g_BlurProgram != 0; }

void OG_ImplOpenGL2_Shutdown() {
    const GLuint old_font = g_FontTexture;
    if (g_FontTexture) { glDeleteTextures(1, &g_FontTexture); g_FontTexture = 0; }
    if (g_BlurTexture) { glDeleteTextures(1, &g_BlurTexture); g_BlurTexture = 0; }
    if (g_BlurProgram && pDeleteProgram) { pDeleteProgram(g_BlurProgram); g_BlurProgram = 0; }
    g_BlurWidth = g_BlurHeight = 0;
    g_Initialized = false;
    if (og::GetCurrentContext()) {
        og::IO& io = og::GetIO();
        if (io.backend_renderer_name && strcmp(io.backend_renderer_name, "osgui_impl_opengl2") == 0)
            io.backend_renderer_name = 0;
        io.backend_flags &= ~(og::BackendFlags_RendererHasTextures |
                              og::BackendFlags_RendererHasEffects |
                              og::BackendFlags_RendererHasVtxOffset);
        if (og::GetFontAtlas().tex_id == (og::TextureID)old_font) og::GetFontAtlas().tex_id = 0;
    }
}

void OG_ImplOpenGL2_NewFrame() {
    if (!og::GetCurrentContext()) return;
    if (!g_FontTexture || og::GetFontAtlas().tex_id != (og::TextureID)g_FontTexture)
        OG_ImplOpenGL2_CreateFontsTexture();
}

static void OG_ImplOpenGL2_SetupRenderState(const og::DrawData* data, int framebuffer_width,
                                             int framebuffer_height) {
    if (pUseProgram) pUseProgram(0);
    if (pActiveTexture) pActiveTexture(GL_TEXTURE0);
    if (pClientActiveTexture) pClientActiveTexture(GL_TEXTURE0);
    if (pBindVertexArray) pBindVertexArray(0);
    if (pBindBuffer) { pBindBuffer(GL_ARRAY_BUFFER, 0); pBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glViewport(0, 0, framebuffer_width, framebuffer_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double left = data->display_pos.x;
    const double right = data->display_pos.x + data->display_size.x;
    const double top = data->display_pos.y;
    const double bottom = data->display_pos.y + data->display_size.y;
    glOrtho(left, right, bottom, top, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void OG_ImplOpenGL2_RenderDrawData(og::DrawData* dd) {
    if (!dd || !og::GetCurrentContext()) return;
    const og::Vec2 scale = og::GetIO().framebuffer_scale;
    int fb_w = (int)(dd->display_size.x * scale.x);
    int fb_h = (int)(dd->display_size.y * scale.y);
    if (fb_w <= 0 || fb_h <= 0) return;

    // --- save host state and set up renderer state ---
    GLint previous_matrix_mode = GL_MODELVIEW;
    GLint previous_program = 0;
    GLint previous_active_texture = GL_TEXTURE0, previous_client_active_texture = GL_TEXTURE0;
    GLint previous_array_buffer = 0, previous_element_buffer = 0, previous_vertex_array = 0;
    glGetIntegerv(GL_MATRIX_MODE, &previous_matrix_mode);
    if (pUseProgram) glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    if (pActiveTexture) glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
    if (pClientActiveTexture) glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &previous_client_active_texture);
    if (pBindVertexArray) glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vertex_array);
    if (pBindBuffer) {
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array_buffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previous_element_buffer);
    }
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT | GL_VIEWPORT_BIT |
                 GL_SCISSOR_BIT | GL_TEXTURE_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    OG_ImplOpenGL2_SetupRenderState(dd, fb_w, fb_h);

    for (size_t n = 0; n < dd->lists.size(); n++) {
        og::DrawList* dl = dd->lists[n];
        if (!dl) continue;
        const og::DrawVert* vtx = dl->vtx.data();
        const og::DrawIdx*  idx = dl->idx.data();

        for (size_t c = 0; c < dl->cmds.size(); c++) {
            const og::DrawCmd& cmd = dl->cmds[c];
            if (cmd.callback) {
                cmd.callback(dl, &cmd);
                OG_ImplOpenGL2_SetupRenderState(dd, fb_w, fb_h);
                continue;
            }
            if (cmd.elem_count == 0 ||
                cmd.elem_count > (unsigned int)(std::numeric_limits<GLsizei>::max)()) continue;
            if ((size_t)cmd.vtx_offset >= dl->vtx.size() ||
                (size_t)cmd.idx_offset + (size_t)cmd.elem_count > dl->idx.size()) continue;
            const og::DrawVert* command_vtx = vtx + cmd.vtx_offset;
            glVertexPointer(2, GL_FLOAT, sizeof(og::DrawVert), (const char*)command_vtx + offsetof(og::DrawVert, pos));
            glTexCoordPointer(2, GL_FLOAT, sizeof(og::DrawVert), (const char*)command_vtx + offsetof(og::DrawVert, uv));
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(og::DrawVert), (const char*)command_vtx + offsetof(og::DrawVert, col));
            // scissor (flip Y for GL)
            int x0 = (int)((cmd.clip_rect.x - dd->display_pos.x) * scale.x);
            int y0 = (int)((cmd.clip_rect.y - dd->display_pos.y) * scale.y);
            int x1 = (int)((cmd.clip_rect.z - dd->display_pos.x) * scale.x);
            int y1 = (int)((cmd.clip_rect.w - dd->display_pos.y) * scale.y);
            if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
            if (x1 > fb_w) x1 = fb_w; if (y1 > fb_h) y1 = fb_h;
            if (x1 <= x0 || y1 <= y0) continue;
            glScissor(x0, fb_h - y1, x1 - x0, y1 - y0);
            bool blur = cmd.effect == og::DrawEffect_BackdropBlur && g_BlurProgram != 0;
            if (blur) {
                blur = EnsureBlurTexture(fb_w, fb_h);
            }
            if (blur) {
                glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, fb_w, fb_h);
                pUseProgram(g_BlurProgram);
                pUniform1i(pGetUniformLocation(g_BlurProgram, "uScene"), 0);
                pUniform2f(pGetUniformLocation(g_BlurProgram, "uResolution"), (float)fb_w, (float)fb_h);
                pUniform1f(pGetUniformLocation(g_BlurProgram, "uRadius"), cmd.effect_amount > 0.0f ? cmd.effect_amount : 6.0f);
                glBindTexture(GL_TEXTURE_2D, g_BlurTexture);
            } else {
                if (cmd.tex_id > (og::TextureID)(std::numeric_limits<GLuint>::max)()) continue;
                if (pUseProgram) pUseProgram(0);
                glBindTexture(GL_TEXTURE_2D, (GLuint)cmd.tex_id);
            }
            glDrawElements(GL_TRIANGLES, (GLsizei)cmd.elem_count, GL_UNSIGNED_INT, idx + cmd.idx_offset);
            if (blur) pUseProgram(0);
        }
    }

    // --- restore ---
    glMatrixMode(GL_MODELVIEW);   glPopMatrix();
    glMatrixMode(GL_PROJECTION);  glPopMatrix();
    glMatrixMode(previous_matrix_mode);
    glPopClientAttrib();
    if (pBindVertexArray) pBindVertexArray((GLuint)previous_vertex_array);
    if (pBindBuffer) {
        pBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous_array_buffer);
        if (!pBindVertexArray) pBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)previous_element_buffer);
    }
    if (pUseProgram) pUseProgram((GLuint)previous_program);
    glPopAttrib();
    if (pActiveTexture) pActiveTexture((GLenum)previous_active_texture);
    if (pClientActiveTexture) pClientActiveTexture((GLenum)previous_client_active_texture);
}
