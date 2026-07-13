#include "osgui_impl_opengl3.h"
#include <stddef.h>
#include <stdlib.h>

typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei;
typedef unsigned char GLboolean; typedef ptrdiff_t GLsizeiptr; typedef float GLfloat; typedef char GLchar;
#define GL_FALSE 0
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_TRIANGLES 0x0004
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_RGBA 0x1908
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303

#define GLPROC(ret,name,args) typedef ret (*PFN_##name) args; static PFN_##name p##name=0
GLPROC(void,ActiveTexture,(GLenum)); GLPROC(void,AttachShader,(GLuint,GLuint)); GLPROC(void,BindBuffer,(GLenum,GLuint));
GLPROC(void,BindTexture,(GLenum,GLuint)); GLPROC(void,BindVertexArray,(GLuint)); GLPROC(void,BlendFunc,(GLenum,GLenum));
GLPROC(void,BufferData,(GLenum,GLsizeiptr,const void*,GLenum)); GLPROC(void,CompileShader,(GLuint)); GLPROC(GLuint,CreateProgram,(void));
GLPROC(GLuint,CreateShader,(GLenum)); GLPROC(void,DeleteBuffers,(GLsizei,const GLuint*)); GLPROC(void,DeleteProgram,(GLuint));
GLPROC(void,DeleteShader,(GLuint)); GLPROC(void,DeleteTextures,(GLsizei,const GLuint*)); GLPROC(void,DeleteVertexArrays,(GLsizei,const GLuint*));
GLPROC(void,Disable,(GLenum)); GLPROC(void,DrawElements,(GLenum,GLsizei,GLenum,const void*)); GLPROC(void,Enable,(GLenum));
GLPROC(void,EnableVertexAttribArray,(GLuint)); GLPROC(void,GenBuffers,(GLsizei,GLuint*)); GLPROC(void,GenTextures,(GLsizei,GLuint*));
GLPROC(void,GenVertexArrays,(GLsizei,GLuint*)); GLPROC(void,GetProgramiv,(GLuint,GLenum,GLint*)); GLPROC(void,GetShaderiv,(GLuint,GLenum,GLint*));
GLPROC(GLint,GetUniformLocation,(GLuint,const GLchar*)); GLPROC(void,LinkProgram,(GLuint)); GLPROC(void,PixelStorei,(GLenum,GLint));
GLPROC(void,Scissor,(GLint,GLint,GLsizei,GLsizei)); GLPROC(void,ShaderSource,(GLuint,GLsizei,const GLchar* const*,const GLint*));
GLPROC(void,TexImage2D,(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*)); GLPROC(void,TexParameteri,(GLenum,GLenum,GLint));
GLPROC(void,Uniform1i,(GLint,GLint)); GLPROC(void,UniformMatrix4fv,(GLint,GLsizei,GLboolean,const GLfloat*)); GLPROC(void,UseProgram,(GLuint));
GLPROC(void,VertexAttribPointer,(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*)); GLPROC(void,Viewport,(GLint,GLint,GLsizei,GLsizei));

static GLuint g_Shader=0,g_Vbo=0,g_Ebo=0,g_Vao=0,g_Font=0; static GLint g_Proj=-1,g_Texture=-1;
static GLuint Compile(GLenum type,const char* source){GLuint s=pCreateShader(type);pShaderSource(s,1,&source,0);pCompileShader(s);GLint ok=0;pGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){pDeleteShader(s);return 0;}return s;}

bool OG_ImplOpenGL3_Init(OG_GLGetProcAddress loader){
    if(!loader)return false;
#define LOAD(name) p##name=(PFN_##name)loader("gl" #name);if(!p##name)return false
    LOAD(ActiveTexture);LOAD(AttachShader);LOAD(BindBuffer);LOAD(BindTexture);LOAD(BindVertexArray);LOAD(BlendFunc);LOAD(BufferData);LOAD(CompileShader);LOAD(CreateProgram);LOAD(CreateShader);LOAD(DeleteBuffers);LOAD(DeleteProgram);LOAD(DeleteShader);LOAD(DeleteTextures);LOAD(DeleteVertexArrays);LOAD(Disable);LOAD(DrawElements);LOAD(Enable);LOAD(EnableVertexAttribArray);LOAD(GenBuffers);LOAD(GenTextures);LOAD(GenVertexArrays);LOAD(GetProgramiv);LOAD(GetShaderiv);LOAD(GetUniformLocation);LOAD(LinkProgram);LOAD(PixelStorei);LOAD(Scissor);LOAD(ShaderSource);LOAD(TexImage2D);LOAD(TexParameteri);LOAD(Uniform1i);LOAD(UniformMatrix4fv);LOAD(UseProgram);LOAD(VertexAttribPointer);LOAD(Viewport);
#undef LOAD
    const char* vs="#version 330 core\nlayout(location=0)in vec2 Position;layout(location=1)in vec2 UV;layout(location=2)in vec4 Color;uniform mat4 ProjMtx;out vec2 FragUV;out vec4 FragColor;void main(){FragUV=UV;FragColor=Color;gl_Position=ProjMtx*vec4(Position,0,1);}";
    const char* fs="#version 330 core\nin vec2 FragUV;in vec4 FragColor;uniform sampler2D Texture;out vec4 OutColor;void main(){OutColor=FragColor*texture(Texture,FragUV);}";
    GLuint v=Compile(GL_VERTEX_SHADER,vs),f=Compile(GL_FRAGMENT_SHADER,fs);if(!v||!f)return false;g_Shader=pCreateProgram();pAttachShader(g_Shader,v);pAttachShader(g_Shader,f);pLinkProgram(g_Shader);pDeleteShader(v);pDeleteShader(f);GLint linked=0;pGetProgramiv(g_Shader,GL_LINK_STATUS,&linked);if(!linked)return false;
    g_Proj=pGetUniformLocation(g_Shader,"ProjMtx");g_Texture=pGetUniformLocation(g_Shader,"Texture");pGenBuffers(1,&g_Vbo);pGenBuffers(1,&g_Ebo);pGenVertexArrays(1,&g_Vao);return true;
}
void OG_ImplOpenGL3_DestroyFontsTexture(){if(g_Font){pDeleteTextures(1,&g_Font);g_Font=0;}if(og::GetFontAtlas().tex_id)og::GetFontAtlas().tex_id=0;}
bool OG_ImplOpenGL3_CreateFontsTexture(){og::FontAtlas& a=og::GetFontAtlas();if(!a.pixels)return false;unsigned char* rgba=(unsigned char*)malloc((size_t)a.width*a.height*4);if(!rgba)return false;for(int i=0;i<a.width*a.height;++i){rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=255;rgba[i*4+3]=a.pixels[i];}pGenTextures(1,&g_Font);pBindTexture(GL_TEXTURE_2D,g_Font);pTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);pTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);pTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);pTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);pPixelStorei(GL_UNPACK_ALIGNMENT,1);pTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,a.width,a.height,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);free(rgba);a.tex_id=g_Font;return true;}
void OG_ImplOpenGL3_NewFrame(){if(!og::GetFontAtlas().tex_id){if(g_Font)OG_ImplOpenGL3_DestroyFontsTexture();OG_ImplOpenGL3_CreateFontsTexture();}}
void OG_ImplOpenGL3_Shutdown(){OG_ImplOpenGL3_DestroyFontsTexture();if(g_Vbo)pDeleteBuffers(1,&g_Vbo);if(g_Ebo)pDeleteBuffers(1,&g_Ebo);if(g_Vao)pDeleteVertexArrays(1,&g_Vao);if(g_Shader)pDeleteProgram(g_Shader);g_Vbo=g_Ebo=g_Vao=g_Shader=0;}
void OG_ImplOpenGL3_RenderDrawData(og::DrawData* dd){if(!dd)return;int w=(int)(dd->display_size.x*og::GetIO().framebuffer_scale.x),h=(int)(dd->display_size.y*og::GetIO().framebuffer_scale.y);if(w<=0||h<=0)return;pEnable(GL_BLEND);pBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);pDisable(GL_CULL_FACE);pDisable(GL_DEPTH_TEST);pEnable(GL_SCISSOR_TEST);pViewport(0,0,w,h);pUseProgram(g_Shader);pUniform1i(g_Texture,0);float L=dd->display_pos.x,R=L+dd->display_size.x,T=dd->display_pos.y,B=T+dd->display_size.y;const float m[16]={2/(R-L),0,0,0,0,2/(T-B),0,0,0,0,-1,0,(R+L)/(L-R),(T+B)/(B-T),0,1};pUniformMatrix4fv(g_Proj,1,GL_FALSE,m);pBindVertexArray(g_Vao);pBindBuffer(GL_ARRAY_BUFFER,g_Vbo);pBindBuffer(GL_ELEMENT_ARRAY_BUFFER,g_Ebo);pEnableVertexAttribArray(0);pEnableVertexAttribArray(1);pEnableVertexAttribArray(2);pVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(og::DrawVert),(void*)offsetof(og::DrawVert,pos));pVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(og::DrawVert),(void*)offsetof(og::DrawVert,uv));pVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,1,sizeof(og::DrawVert),(void*)offsetof(og::DrawVert,col));
    for(size_t n=0;n<dd->lists.size();++n){og::DrawList* dl=dd->lists[n];pBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(dl->vtx.size()*sizeof(og::DrawVert)),dl->vtx.data(),GL_STREAM_DRAW);pBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(dl->idx.size()*sizeof(og::DrawIdx)),dl->idx.data(),GL_STREAM_DRAW);for(size_t c=0;c<dl->cmds.size();++c){const og::DrawCmd& cmd=dl->cmds[c];if(!cmd.elem_count)continue;pBindTexture(GL_TEXTURE_2D,cmd.tex_id);int x=(int)((cmd.clip_rect.x-dd->display_pos.x)*og::GetIO().framebuffer_scale.x),y=(int)((cmd.clip_rect.y-dd->display_pos.y)*og::GetIO().framebuffer_scale.y),x2=(int)((cmd.clip_rect.z-dd->display_pos.x)*og::GetIO().framebuffer_scale.x),y2=(int)((cmd.clip_rect.w-dd->display_pos.y)*og::GetIO().framebuffer_scale.y);pScissor(x,h-y2,x2-x,y2-y);pDrawElements(GL_TRIANGLES,(GLsizei)cmd.elem_count,GL_UNSIGNED_INT,(void*)((size_t)cmd.idx_offset*sizeof(og::DrawIdx)));}}
    pBindVertexArray(0);pUseProgram(0);pDisable(GL_SCISSOR_TEST);
}
