/*
The MIT License

Copyright (C) 2009-2010 DeSmuME team

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <map>

#include "GPU.h"
#include "NDSSystem.h"

#include "aggdraw.h"

#include "agg_renderer_base.h"
#include "agg_renderer_primitives.h"
#include "agg_renderer_scanline.h"
#include "agg_bounding_rect.h"
#include "agg_trans_affine.h"
#include "agg_path_storage.h"
#include "agg_color_rgba.h"

#include "agg_rasterizer_scanline_aa.h"
#include "agg_scanline_u.h"
#include "agg_renderer_scanline.h"
#include "agg_scanline_p.h"

//raster text
#include "agg_glyph_raster_bin.h"
#include "agg_embedded_raster_fonts.h"
#include "agg_renderer_raster_text.h"

#include "ctrl/agg_bezier_ctrl.h"
#include "platform/agg_platform_support.h"
#include "agg_pattern_filters_rgba.h"
#include "agg_renderer_outline_image.h"
#include "agg_rasterizer_outline_aa.h"

#include "agg_image_accessors.h"
#include "agg_span_interpolator_linear.h"
#include "agg_span_image_filter_rgb.h"
#include "agg_span_image_filter_rgba.h"
#include "agg_span_image_filter_gray.h"

#include "agg_span_allocator.h"

typedef std::map<std::string, const agg::int8u*> TAgg_Font_Table;
static TAgg_Font_Table font_table;

font_type fonts_list[] =
{
	{ agg::gse4x6, "gse4x6" },
	{ agg::gse4x8, "gse4x8" },
	{ agg::gse5x7, "gse5x7" },
	{ agg::gse5x9, "gse5x9" },
	{ agg::gse6x9, "gse6x9" },
	{ agg::gse6x12, "gse6x12" },
	{ agg::gse7x11, "gse7x11" },
	{ agg::gse7x11_bold, "gse7x11_bold" },
	{ agg::gse7x15, "gse7x15" },
	{ agg::gse7x15_bold, "gse7x15_bold" },
	{ agg::gse8x16, "gse8x16" },
	{ agg::gse8x16_bold, "gse8x16_bold" },
	{ agg::mcs11_prop, "mcs11_prop" },
	{ agg::mcs11_prop_condensed, "mcs11_prop_condensed" },
	{ agg::mcs12_prop, "mcs12_prop" },
	{ agg::mcs13_prop, "mcs13_prop" },
	{ agg::mcs5x10_mono, "mcs5x10_mono" },
	{ agg::mcs5x11_mono, "mcs5x11_mono" },
	{ agg::mcs6x10_mono, "mcs6x10_mono" },
	{ agg::mcs6x11_mono, "mcs6x11_mono" },
	{ agg::mcs7x12_mono_high, "mcs7x12_mono_high" },
	{ agg::mcs7x12_mono_low, "mcs7x12_mono_low" },
	{ agg::verdana12, "verdana12" },
	{ agg::verdana12_bold, "verdana12_bold" },
	{ agg::verdana13, "verdana13" },
	{ agg::verdana13_bold, "verdana13_bold" },
	{ agg::verdana14, "verdana14" },
	{ agg::verdana14_bold, "verdana14_bold" },
	{ agg::verdana16, "verdana16" },
	{ agg::verdana16_bold, "verdana16_bold" },
	{ agg::verdana17, "verdana17" },
	{ agg::verdana17_bold, "verdana17_bold" },
	{ agg::verdana18, "verdana18" },
	{ agg::verdana18_bold, "verdana18_bold" },
};
int font_Nums = ARRAY_SIZE(fonts_list);

const agg::int8u* AggDrawTarget::lookupFont(const std::string& name)
{ 
	TAgg_Font_Table::iterator it(font_table.find(name));
	if(it == font_table.end()) return NULL;
	else return it->second;
}

static void Agg_init_fonts()
{
	for(u32 i=0;i<font_Nums;i++)
		font_table[fonts_list[i].name] = fonts_list[i].font;
}

AggDraw_Desmume aggDraw;

#if defined(WIN32) || defined(HOST_LINUX)
T_AGG_RGBA agg_targetScreen(0, 256, 384, 1024);
#else
T_AGG_RGB555 agg_targetScreen(0, 256, 384, 1512);
#endif

static u32 luaBuffer[256*192*2];
T_AGG_RGBA agg_targetLua((u8*)luaBuffer, 256, 384, 1024);

static u32 hudBuffer[256*192*2];
T_AGG_RGBA agg_targetHud((u8*)hudBuffer, 256, 384, 1024);

static AggDrawTarget* targets[] = {
	&agg_targetScreen,
	&agg_targetHud,
	&agg_targetLua,
};

void Agg_init()
{
	Agg_init_fonts();
	aggDraw.screen = targets[0];
	aggDraw.hud = targets[1];
	aggDraw.lua = targets[2];

	aggDraw.target = targets[0];

	//if we're single core, we don't want to waste time compositing
	//and the more clever compositing isnt supported in non-windows
	#ifdef WIN32
	if(CommonSettings.single_core())
		aggDraw.hud = &agg_targetScreen;
	#else
	aggDraw.hud = &agg_targetScreen;
	#endif

	aggDraw.hud->setFont("verdana18_bold");
	aggDraw.hud->setVectorFont("s8514fix", 14, true);
}

void AggDraw_Desmume::setTarget(AggTarget newTarget)
{
	target = targets[newTarget];
}
