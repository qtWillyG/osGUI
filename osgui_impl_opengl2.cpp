// OSGui OpenGL 2-compatible renderer backend.
//
// Uses the fixed-function pipeline (OpenGL 1.x) that ships with opengl32.lib,
// so the demo builds with zero external loaders (no GLAD/GLEW/GLFW).
#include "osgui_impl_opengl2.h"
#include <windows.h>
#include <GL/gl.h>

static GLuint g_FontTexture = 0;

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);

    atlas.tex_id = (unsigned int)g_FontTexture;
    return true;
}

bool OG_ImplOpenGL2_Init() { return true; }

void OG_ImplOpenGL2_Shutdown() {
    if (g_FontTexture) { glDeleteTextures(1, &g_FontTexture); g_FontTexture = 0; }
}

void OG_ImplOpenGL2_NewFrame() {
    if (!g_FontTexture) OG_ImplOpenGL2_CreateFontsTexture();
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
            glBindTexture(GL_TEXTURE_2D, (GLuint)cmd.tex_id);
            glDrawElements(GL_TRIANGLES, (GLsizei)cmd.elem_count, GL_UNSIGNED_INT, idx + cmd.idx_offset);
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
