// OSGui OpenGL 2-compatible renderer backend.
//
// Uses the fixed-function pipeline (OpenGL 1.x) that ships with opengl32.lib,
// so the demo builds with zero external loaders (no GLAD/GLEW/GLFW).
#include "osgui_impl_opengl2.h"
#include <windows.h>
#include <GL/gl.h>

static GLuint g_FontTexture = 0;
static GLuint g_BlurTexture = 0;
static int g_BlurWidth = 0, g_BlurHeight = 0;
static GLuint g_BlurProgram = 0;

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
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
    if (!pCreateShader || !pShaderSource || !pCompileShader || !pGetShaderiv || !pCreateProgram ||
        !pAttachShader || !pLinkProgram || !pGetProgramiv || !pUseProgram || !pGetUniformLocation ||
        !pUniform1i || !pUniform1f || !pUniform2f) return false;

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
    pShaderSource(vs, 1, &vs_source, 0); pCompileShader(vs);
    pShaderSource(fs, 1, &fs_source, 0); pCompileShader(fs);
    GLint ok_vs = 0, ok_fs = 0;
    pGetShaderiv(vs, GL_COMPILE_STATUS, &ok_vs); pGetShaderiv(fs, GL_COMPILE_STATUS, &ok_fs);
    if (!ok_vs || !ok_fs) { if (pDeleteShader) { pDeleteShader(vs); pDeleteShader(fs); } return false; }
    g_BlurProgram = pCreateProgram();
    pAttachShader(g_BlurProgram, vs); pAttachShader(g_BlurProgram, fs); pLinkProgram(g_BlurProgram);
    GLint linked = 0; pGetProgramiv(g_BlurProgram, GL_LINK_STATUS, &linked);
    if (pDeleteShader) { pDeleteShader(vs); pDeleteShader(fs); }
    if (!linked) { if (pDeleteProgram) pDeleteProgram(g_BlurProgram); g_BlurProgram = 0; return false; }
    return true;
}

static void EnsureBlurTexture(int width, int height) {
    if (!g_BlurTexture) glGenTextures(1, &g_BlurTexture);
    glBindTexture(GL_TEXTURE_2D, g_BlurTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    if (width != g_BlurWidth || height != g_BlurHeight) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        g_BlurWidth = width; g_BlurHeight = height;
    }
}

bool OG_ImplOpenGL2_CreateFontsTexture() {
    og::FontAtlas& atlas = og::GetFontAtlas();
    if (!atlas.pixels) return false;

    // expand R8 coverage into RGBA (white with per-texel alpha) so a single
    // texture serves both solid fills (white texel) and text glyphs.
    int w = atlas.width, h = atlas.height;
    unsigned char* rgba = (unsigned char*)malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        unsigned char a = atlas.pixels[i];
        rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = a;
    }

    if (g_FontTexture) glDeleteTextures(1, &g_FontTexture);
    glGenTextures(1, &g_FontTexture);
    glBindTexture(GL_TEXTURE_2D, g_FontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);

    atlas.tex_id = (unsigned int)g_FontTexture;
    return true;
}

bool OG_ImplOpenGL2_Init() { CreateBlurShader(); return true; }
bool OG_ImplOpenGL2_HasShaderEffects() { return g_BlurProgram != 0; }

void OG_ImplOpenGL2_Shutdown() {
    if (g_FontTexture) { glDeleteTextures(1, &g_FontTexture); g_FontTexture = 0; }
    if (g_BlurTexture) { glDeleteTextures(1, &g_BlurTexture); g_BlurTexture = 0; }
    if (g_BlurProgram && pDeleteProgram) { pDeleteProgram(g_BlurProgram); g_BlurProgram = 0; }
    g_BlurWidth = g_BlurHeight = 0;
}

void OG_ImplOpenGL2_NewFrame() {
    if (!g_FontTexture || og::GetFontAtlas().tex_id == 0) OG_ImplOpenGL2_CreateFontsTexture();
}

void OG_ImplOpenGL2_RenderDrawData(og::DrawData* dd) {
    int fb_w = (int)dd->display_size.x;
    int fb_h = (int)dd->display_size.y;
    if (fb_w <= 0 || fb_h <= 0) return;

    // --- set up render state ---
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT | GL_VIEWPORT_BIT | GL_SCISSOR_BIT);
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

    glViewport(0, 0, fb_w, fb_h);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, fb_w, fb_h, 0.0, -1.0, 1.0);  // y-down, origin top-left
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    for (size_t n = 0; n < dd->lists.size(); n++) {
        og::DrawList* dl = dd->lists[n];
        const og::DrawVert* vtx = dl->vtx.data();
        const og::DrawIdx*  idx = dl->idx.data();

        glVertexPointer(2, GL_FLOAT, sizeof(og::DrawVert), (const char*)vtx + offsetof(og::DrawVert, pos));
        glTexCoordPointer(2, GL_FLOAT, sizeof(og::DrawVert), (const char*)vtx + offsetof(og::DrawVert, uv));
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(og::DrawVert), (const char*)vtx + offsetof(og::DrawVert, col));

        for (size_t c = 0; c < dl->cmds.size(); c++) {
            const og::DrawCmd& cmd = dl->cmds[c];
            if (cmd.elem_count == 0) continue;
            // scissor (flip Y for GL)
            int x0 = (int)cmd.clip_rect.x, y0 = (int)cmd.clip_rect.y;
            int x1 = (int)cmd.clip_rect.z, y1 = (int)cmd.clip_rect.w;
            if (x1 <= x0 || y1 <= y0) continue;
            glScissor(x0, fb_h - y1, x1 - x0, y1 - y0);
            bool blur = cmd.effect == og::DrawEffect_BackdropBlur && g_BlurProgram != 0;
            if (blur) {
                EnsureBlurTexture(fb_w, fb_h);
                glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, fb_w, fb_h);
                pUseProgram(g_BlurProgram);
                pUniform1i(pGetUniformLocation(g_BlurProgram, "uScene"), 0);
                pUniform2f(pGetUniformLocation(g_BlurProgram, "uResolution"), (float)fb_w, (float)fb_h);
                pUniform1f(pGetUniformLocation(g_BlurProgram, "uRadius"), cmd.effect_amount > 0.0f ? cmd.effect_amount : 6.0f);
                glBindTexture(GL_TEXTURE_2D, g_BlurTexture);
            } else {
                if (pUseProgram) pUseProgram(0);
                glBindTexture(GL_TEXTURE_2D, (GLuint)cmd.tex_id);
            }
            glDrawElements(GL_TRIANGLES, (GLsizei)cmd.elem_count, GL_UNSIGNED_INT, idx + cmd.idx_offset);
            if (blur) pUseProgram(0);
        }
    }

    // --- restore ---
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glMatrixMode(GL_MODELVIEW);   glPopMatrix();
    glMatrixMode(GL_PROJECTION);  glPopMatrix();
    glPopAttrib();
}
