#include "osgui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void InstallTestFont() {
    og::FontAtlas& a=og::GetFontAtlas();a.width=2;a.height=2;a.line_height=16;a.ascent=12;a.tex_id=1;a.white_uv=og::Vec2(0.25f,0.25f);
    static unsigned char pixels[4]={255,255,255,255};a.pixels=pixels;
    for(int i=0;i<128;++i){a.glyph_valid[i]=i>=32;a.glyphs[i].advance=8;a.glyphs[i].x0=0;a.glyphs[i].y0=0;a.glyphs[i].x1=8;a.glyphs[i].y1=16;a.glyphs[i].u0=0;a.glyphs[i].v0=0;a.glyphs[i].u1=1;a.glyphs[i].v1=1;if(i>=32)a.glyph_map[(unsigned)i]=a.glyphs[i];}
}
static void Frame(char* text) {
    og::NewFrame();og::SetNextWindowPos(og::Vec2(10,10));og::SetNextWindowSize(og::Vec2(520,600));
    if(og::Begin("Tests")){og::InputText("Name",text,64);static int choice=0;const char* values[]={"One","Two","Three"};og::Combo("Mode",&choice,values,3);static float color[4]={1,0,0,1};og::ColorEdit4("Tint",color);float series[]={2,3,1,4};if(og::BeginChart("Data")){og::ChartArea("Area",series,4);og::EndChart();}if(og::BeginTable("Rows",2,og::TableFlags_Sortable)){og::TableHeader("Name");og::TableHeader("Value");og::TableSelectable("Alpha");og::TableSelectable("42");og::EndTable();}}og::End();og::Render();
}
int main(){
    og::CreateContext();InstallTestFont();og::IO& io=og::GetIO();io.display_size=og::Vec2(800,700);io.delta_time=1.0f/60.0f;char text[64]="";
    Frame(text);assert(og::GetDrawData()->total_vtx>0);assert(og::CalcTextSize("hello").x==40);
    io.key_down[13]=true;Frame(text);io.key_down[13]=false;Frame(text);
    io.input_chars[0]=0x03A9;io.input_char_count=1;Frame(text);assert(strcmp(text,"\xCE\xA9")==0);
    io.key_down[8]=true;Frame(text);io.key_down[8]=false;Frame(text);assert(text[0]==0);
    assert(og::SaveStateJSON("osgui-test-state.json"));assert(og::LoadStateJSON("osgui-test-state.json"));remove("osgui-test-state.json");
    og::GetFontAtlas().pixels=0;og::DestroyContext();puts("OSGui core tests passed");return 0;
}
