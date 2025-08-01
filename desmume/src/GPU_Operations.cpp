/*
	Copyright (C) 2021-2025 DeSmuME team

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "GPU_Operations.h"


static size_t _gpuLargestDstLineCount = 1;
static size_t _gpuVRAMBlockOffset = GPU_VRAM_BLOCK_LINES * GPU_FRAMEBUFFER_NATIVE_WIDTH;

static u16 *_gpuDstToSrcIndex = NULL; // Key: Destination pixel index / Value: Source pixel index
static u8 *_gpuDstToSrcSSSE3_u8_8e = NULL;
static u8 *_gpuDstToSrcSSSE3_u8_16e = NULL;
static u8 *_gpuDstToSrcSSSE3_u16_8e = NULL;
static u8 *_gpuDstToSrcSSSE3_u32_4e = NULL;

static CACHE_ALIGN u32 _gpuDstPitchCount[GPU_FRAMEBUFFER_NATIVE_WIDTH];	// Key: Source pixel index in x-dimension / Value: Number of x-dimension destination pixels for the source pixel
static CACHE_ALIGN u32 _gpuDstPitchIndex[GPU_FRAMEBUFFER_NATIVE_WIDTH];	// Key: Source pixel index in x-dimension / Value: First destination pixel that maps to the source pixel

u8 PixelOperation::BlendTable555[17][17][32][32];
Color5551 PixelOperation::BrightnessUpTable555[17][0x8000];
Color4u8 PixelOperation::BrightnessUpTable666[17][0x8000];
Color4u8 PixelOperation::BrightnessUpTable888[17][0x8000];
Color5551 PixelOperation::BrightnessDownTable555[17][0x8000];
Color4u8 PixelOperation::BrightnessDownTable666[17][0x8000];
Color4u8 PixelOperation::BrightnessDownTable888[17][0x8000];

static CACHE_ALIGN ColorOperation colorop;
static CACHE_ALIGN PixelOperation pixelop;

FORCEINLINE Color5551 ColorOperation::blend(const Color5551 colA, const Color5551 colB, const u16 blendEVA, const u16 blendEVB) const
{
	const u16 r = ( (colA.r * blendEVA) + (colB.r * blendEVB) ) / 16;
	const u16 g = ( (colA.g * blendEVA) + (colB.g * blendEVB) ) / 16;
	const u16 b = ( (colA.b * blendEVA) + (colB.b * blendEVB) ) / 16;
	
	Color5551 outColor;
	outColor.r = (r > 31) ? 31 : r;
	outColor.g = (g > 31) ? 31 : g;
	outColor.b = (b > 31) ? 31 : b;
	outColor.a = 0;
	
	return outColor;
}

FORCEINLINE Color5551 ColorOperation::blend(const Color5551 colA, const Color5551 colB, const TBlendTable *blendTable) const
{
	Color5551 outColor;
	outColor.r = (*blendTable)[colA.r][colB.r];
	outColor.g = (*blendTable)[colA.g][colB.g];
	outColor.b = (*blendTable)[colA.b][colB.b];
	outColor.a = 0;
	
	return outColor;
}

template <NDSColorFormat COLORFORMAT>
FORCEINLINE Color4u8 ColorOperation::blend(const Color4u8 colA, const Color4u8 colB, const u16 blendEVA, const u16 blendEVB) const
{
	Color4u8 outColor;
	const u16 r = ( ((u16)colA.r * blendEVA) + ((u16)colB.r * blendEVB) ) / 16;
	const u16 g = ( ((u16)colA.g * blendEVA) + ((u16)colB.g * blendEVB) ) / 16;
	const u16 b = ( ((u16)colA.b * blendEVA) + ((u16)colB.b * blendEVB) ) / 16;
	
	if (COLORFORMAT == NDSColorFormat_BGR666_Rev)
	{
		outColor.r = (u8)( (r > 63) ? 63 : r );
		outColor.g = (u8)( (g > 63) ? 63 : g );
		outColor.b = (u8)( (b > 63) ? 63 : b );
	}
	else if (COLORFORMAT == NDSColorFormat_BGR888_Rev)
	{
		outColor.r = (u8)( (r > 255) ? 255 : r );
		outColor.g = (u8)( (g > 255) ? 255 : g );
		outColor.b = (u8)( (b > 255) ? 255 : b );
	}
	
	outColor.a = 0;
	return outColor;
}

FORCEINLINE Color5551 ColorOperation::blend3D(const Color4u8 colA, const Color5551 colB) const
{
	const u16 alpha = colA.a + 1;
	const u16 minusAlpha = 32 - alpha;
	
	Color5551 outColor;
	outColor.r = (u8)( (((u16)colA.r * alpha) + ((colB.r << 1) * minusAlpha)) >> 6 );
	outColor.g = (u8)( (((u16)colA.g * alpha) + ((colB.g << 1) * minusAlpha)) >> 6 );
	outColor.b = (u8)( (((u16)colA.b * alpha) + ((colB.b << 1) * minusAlpha)) >> 6 );
	outColor.a = 0;
	
	return outColor;
}

template <NDSColorFormat COLORFORMAT>
FORCEINLINE Color4u8 ColorOperation::blend3D(const Color4u8 colA, const Color4u8 colB) const
{
	Color4u8 outColor;
	const u16 alpha = colA.a + 1;
	
	if (COLORFORMAT == NDSColorFormat_BGR666_Rev)
	{
		const u16 minusAlpha = 32 - alpha;
		outColor.r = (u8)( (((u16)colA.r * alpha) + ((u16)colB.r * minusAlpha)) >> 5 );
		outColor.g = (u8)( (((u16)colA.g * alpha) + ((u16)colB.g * minusAlpha)) >> 5 );
		outColor.b = (u8)( (((u16)colA.b * alpha) + ((u16)colB.b * minusAlpha)) >> 5 );
	}
	else if (COLORFORMAT == NDSColorFormat_BGR888_Rev)
	{
		const u16 minusAlpha = 256 - alpha;
		outColor.r = (u8)( (((u16)colA.r * alpha) + ((u16)colB.r * minusAlpha)) >> 8 );
		outColor.g = (u8)( (((u16)colA.g * alpha) + ((u16)colB.g * minusAlpha)) >> 8 );
		outColor.b = (u8)( (((u16)colA.b * alpha) + ((u16)colB.b * minusAlpha)) >> 8 );
	}
	
	outColor.a = 0;
	return outColor;
}

FORCEINLINE Color5551 ColorOperation::increase(const Color5551 col, const u16 blendEVY) const
{
	Color5551 outColor;
	outColor.r = (col.r + ((31 - col.r) * blendEVY / 16));
	outColor.g = (col.g + ((31 - col.g) * blendEVY / 16));
	outColor.b = (col.b + ((31 - col.b) * blendEVY / 16));
	outColor.a = 0;
	
	return outColor;
}

template <NDSColorFormat COLORFORMAT>
FORCEINLINE Color4u8 ColorOperation::increase(const Color4u8 col, const u16 blendEVY) const
{
	Color4u8 outColor;
	
	if (COLORFORMAT == NDSColorFormat_BGR666_Rev)
	{
		outColor.r = (u8)( (u16)col.r + ((63 - (u16)col.r) * blendEVY / 16) );
		outColor.g = (u8)( (u16)col.g + ((63 - (u16)col.g) * blendEVY / 16) );
		outColor.b = (u8)( (u16)col.b + ((63 - (u16)col.b) * blendEVY / 16) );
	}
	else if (COLORFORMAT == NDSColorFormat_BGR888_Rev)
	{
		outColor.r = (u8)( (u16)col.r + ((255 - (u16)col.r) * blendEVY / 16) );
		outColor.g = (u8)( (u16)col.g + ((255 - (u16)col.g) * blendEVY / 16) );
		outColor.b = (u8)( (u16)col.b + ((255 - (u16)col.b) * blendEVY / 16) );
	}
	
	outColor.a = 0;
	return outColor;
}

FORCEINLINE Color5551 ColorOperation::decrease(const Color5551 col, const u16 blendEVY) const
{
	Color5551 outColor;
	outColor.r = (col.r - (col.r * blendEVY / 16));
	outColor.g = (col.g - (col.g * blendEVY / 16));
	outColor.b = (col.b - (col.b * blendEVY / 16));
	outColor.a = 0;
	
	return outColor;
}

template <NDSColorFormat COLORFORMAT>
FORCEINLINE Color4u8 ColorOperation::decrease(const Color4u8 col, const u16 blendEVY) const
{
	Color4u8 outColor;
	outColor.r = (u8)( (u16)col.r - ((u16)col.r * blendEVY / 16) );
	outColor.g = (u8)( (u16)col.g - ((u16)col.g * blendEVY / 16) );
	outColor.b = (u8)( (u16)col.b - ((u16)col.b * blendEVY / 16) );
	outColor.a = 0;
	
	return outColor;
}

void PixelOperation::InitLUTs()
{
	static bool didInit = false;
	
	if (didInit)
	{
		return;
	}
	
	/*
	NOTE: gbatek (in the reference above) seems to expect 6bit values
	per component, but as desmume works with 5bit per component,
	we use 31 as top, instead of 63. Testing it on a few games,
	using 63 seems to give severe color wraping, and 31 works
	nicely, so for now we'll just that, until proven wrong.

	i have seen pics of pokemon ranger getting white with 31, with 63 it is nice.
	it could be pb of alpha or blending or...

	MightyMax> created a test NDS to check how the brightness values work,
	and 31 seems to be correct. FactorEx is a override for max brighten/darken
	See: http://mightymax.org/gfx_test_brightness.nds
	The Pokemon Problem could be a problem with 8/32 bit writes not recognized yet,
	i'll add that so you can check back.
	*/
	
	for (u16 i = 0; i <= 16; i++)
	{
		for (u16 j = 0x0000; j < 0x8000; j++)
		{
			Color5551 cur;

			cur.value = j;
			cur.r = ( cur.r + ((31 - cur.r) * i / 16) );
			cur.g = ( cur.g + ((31 - cur.g) * i / 16) );
			cur.b = ( cur.b + ((31 - cur.b) * i / 16) );
			cur.a = 0;
			PixelOperation::BrightnessUpTable555[i][j]       = cur;
			PixelOperation::BrightnessUpTable666[i][j].value = COLOR555TO666(cur.value);
			PixelOperation::BrightnessUpTable888[i][j].value = COLOR555TO888(cur.value);
			
			cur.value = j;
			cur.r = ( cur.r - (cur.r * i / 16) );
			cur.g = ( cur.g - (cur.g * i / 16) );
			cur.b = ( cur.b - (cur.b * i / 16) );
			cur.a = 0;
			PixelOperation::BrightnessDownTable555[i][j]       = cur;
			PixelOperation::BrightnessDownTable666[i][j].value = COLOR555TO666(cur.value);
			PixelOperation::BrightnessDownTable888[i][j].value = COLOR555TO888(cur.value);
		}
	}
	
	for (u16 c0 = 0; c0 <= 31; c0++)
	{
		for (u16 c1 = 0; c1 <= 31; c1++)
		{
			for (u16 eva = 0; eva <= 16; eva++)
			{
				for (u16 evb = 0; evb <= 16; evb++)
				{
					u8 color = (u8)( ((c0 * eva) + (c1 * evb)) / 16 );
					u8 clampedColor = std::min<u8>(31, color);
					PixelOperation::BlendTable555[eva][evb][c0][c1] = clampedColor;
				}
			}
		}
	}
	
	didInit = true;
}

template <NDSColorFormat OUTPUTFORMAT, bool ISDEBUGRENDER>
FORCEINLINE void PixelOperation::_copy16(GPUEngineCompositorInfo &compInfo, const Color5551 srcColor16) const
{
	Color5551 &dstColor16 = *compInfo.target.lineColor16;
	Color4u8 &dstColor32 = *compInfo.target.lineColor32;
	u8 &dstLayerID = *compInfo.target.lineLayerID;
	
	switch (OUTPUTFORMAT)
	{
		case NDSColorFormat_BGR555_Rev:
			dstColor16 = srcColor16;
			dstColor16.a = 1;
			break;
			
		case NDSColorFormat_BGR666_Rev:
			dstColor32.value = ColorspaceConvert555To6665Opaque<false>(srcColor16.value);
			break;
			
		case NDSColorFormat_BGR888_Rev:
			dstColor32.value = ColorspaceConvert555To8888Opaque<false>(srcColor16.value);
			break;
	}
	
	if (!ISDEBUGRENDER)
	{
		dstLayerID = compInfo.renderState.selectedLayerID;
	}
}

template <NDSColorFormat OUTPUTFORMAT, bool ISDEBUGRENDER>
FORCEINLINE void PixelOperation::_copy32(GPUEngineCompositorInfo &compInfo, const Color4u8 srcColor32) const
{
	Color5551 &dstColor16 = *compInfo.target.lineColor16;
	Color4u8 &dstColor32 = *compInfo.target.lineColor32;
	u8 &dstLayerID = *compInfo.target.lineLayerID;
	
	switch (OUTPUTFORMAT)
	{
		case NDSColorFormat_BGR555_Rev:
			dstColor16.value = ColorspaceConvert6665To5551<false>(srcColor32);
			dstColor16.a = 1;
			break;
			
		case NDSColorFormat_BGR666_Rev:
			dstColor32 = srcColor32;
			dstColor32.a = 0x1F;
			break;
			
		case NDSColorFormat_BGR888_Rev:
			dstColor32 = srcColor32;
			dstColor32.a = 0xFF;
			break;
			
		default:
			return;
	}
	
	if (!ISDEBUGRENDER)
	{
		dstLayerID = compInfo.renderState.selectedLayerID;
	}
}

template <NDSColorFormat OUTPUTFORMAT>
FORCEINLINE void PixelOperation::_brightnessUp16(GPUEngineCompositorInfo &compInfo, const Color5551 srcColor16) const
{
	Color5551 &dstColor16 = *compInfo.target.lineColor16;
	Color4u8 &dstColor32 = *compInfo.target.lineColor32;
	u8 &dstLayerID = *compInfo.target.lineLayerID;
	
	switch (OUTPUTFORMAT)
	{
		case NDSColorFormat_BGR555_Rev:
			dstColor16 = compInfo.renderState.brightnessUpTable555[srcColor16.value & 0x7FFF];
			dstColor16.a = 1;
			break;
			
		case NDSColorFormat_BGR666_Rev:
			dstColor32 = compInfo.renderState.brightnessUpTable666[srcColor16.value & 0x7FFF];
			dstColor32.a = 0x1F;
			break;
			
		case NDSColorFormat_BGR888_Rev:
			dstColor32 = compInfo.renderState.brightnessUpTable888[srcColor16.value & 0x7FFF];
			dstColor32.a = 0xFF;
			break;
	}
	
	dstLayerID = compInfo.renderState.selectedLayerID;
}

template <NDSColorFormat OUTPUTFORMAT>
FORCEINLINE void PixelOperation::_brightnessUp32(GPUEngineCompositorInfo &compInfo, const Color4u8 srcColor32) const
{
	Color5551 &dstColor16 = *compInfo.target.lineColor16;
	Color4u8 &dstColor32 = *compInfo.target.lineColor32;
	u8 &dstLayerID = *compInfo.target.lineLayerID;
	
	if (OUTPUTFORMAT == NDSColorFormat_BGR555_Rev)
	{
		const u16 srcColor16 = ColorspaceConvert6665To5551<false>(srcColor32);
		dstColor16 = compInfo.renderState.brightnessUpTable555[srcColor16 & 0x7FFF];
		dstColor16.a = 1;
	}
	else
	{
		dstColor32 = colorop.increase<OUTPUTFORMAT>(srcColor32, compInfo.renderState.blendEVY);
		dstColor32.a = (OUTPUTFORMAT == NDSColorFormat_BGR888_Rev) ? 0xFF : 0x1F;
	}
	
	dstLayerID = compInfo.renderState.selectedLayerID;
}

template <NDSColorFormat OUTPUTFORMAT>
FORCEINLINE void PixelOperation::_brightnessDown16(GPUEngineCompositorInfo &compInfo, const Color5551 srcColor16) const
{
	Color5551 &dstColor16 = *compInfo.target.lineColor16;
	Color4u8 &dstColor32 = *compInfo.target.lineColor32;
	u8 &dstLayerID = *compInfo.target.lineLayerID;
	
	switch (OUTPUTFORMAT)
	{
		case NDSColorFormat_BGR555_Rev:
			dstColor16 = compInfo.renderState.brightnessDownTable555[srcColor16.value & 0x7FFF];
			dstColor16.a = 1;
			break;
			
		case NDSColorFormat_BGR666_Rev:
			dstColor32 = compInfo.renderState.brightnessDownTable666[srcColor16.value & 0x7FFF];
			dstColor32.a = 0x1F;
			break;
			
		case NDSColorFormat_BGR888_Rev:
			dstColor32 = compInfo.renderState.brightnessDownTable888[srcColor16.value & 0x7FFF];
			dstColor32.a = 0xFF;
			break;
	}
	
	dstLayerID = compInfo.renderState.selectedLayerID;
}

template <NDSColorFormat OUTPUTFORMAT>
FORCEINLINE void PixelOperation::_brightnessDown32(GPUEngineCompositorInfo &compInfo, const Color4u8 srcColor32) const
{
	Color5551 &dstColor16 = *compInfo.target.lineColor16;
	Color4u8 &dstColor32 = *compInfo.target.lineColor32;
	u8 &dstLayerID = *compInfo.target.lineLayerID;
	
	if (OUTPUTFORMAT == NDSColorFormat_BGR555_Rev)
	{
		const u16 srcColor16 = ColorspaceConvert6665To5551<false>(srcColor32);
		dstColor16 = compInfo.renderState.brightnessDownTable555[srcColor16 & 0x7FFF];
		dstColor16.a = 1;
	}
	else
	{
		dstColor32 = colorop.decrease<OUTPUTFORMAT>(srcColor32, compInfo.renderState.blendEVY);
		dstColor32.a = (OUTPUTFORMAT == NDSColorFormat_BGR888_Rev) ? 0xFF : 0x1F;
	}
	
	dstLayerID = compInfo.renderState.selectedLayerID;
}

template <GPULayerType LAYERTYPE>
FORCEINLINE void PixelOperation::__selectedEffect(const GPUEngineCompositorInfo &compInfo, const u8 &dstLayerID, const bool enableColorEffect, const u8 spriteAlpha, const OBJMode spriteMode, ColorEffect &selectedEffect, TBlendTable **selectedBlendTable, u8 &blendEVA, u8 &blendEVB) const
{
	const bool dstTargetBlendEnable = (dstLayerID != compInfo.renderState.selectedLayerID) && compInfo.renderState.dstBlendEnable[dstLayerID];
	
	// 3D rendering has a special override: If the destination pixel is set to blend, then always blend.
	// Test case: When starting a stage in Super Princess Peach, the screen will be solid black unless
	// blending is forced here.
	//
	// This behavior must take priority over checking for the window color effect enable flag.
	// Test case: Dialogue boxes in Front Mission will be rendered with blending disabled unless
	// blend forcing takes priority.
	bool forceDstTargetBlend = (LAYERTYPE == GPULayerType_3D) ? dstTargetBlendEnable : false;
	
	if (LAYERTYPE == GPULayerType_OBJ)
	{
		//translucent-capable OBJ are forcing the function to blend when the second target is satisfied
		const bool isObjTranslucentType = (spriteMode == OBJMode_Transparent) || (spriteMode == OBJMode_Bitmap);
		if (isObjTranslucentType && dstTargetBlendEnable)
		{
			// OBJ without fine-grained alpha are using EVA/EVB for blending. This is signified by receiving 0xFF in the alpha.
			// Test cases:
			// * The spriteblend demo
			// * Glory of Heracles - fairy on the title screen
			// * Phoenix Wright: Ace Attorney - character fade-in/fade-out
			if (spriteAlpha != 0xFF)
			{
				blendEVA = spriteAlpha;
				blendEVB = 16 - spriteAlpha;
				*selectedBlendTable = &PixelOperation::BlendTable555[blendEVA][blendEVB];
			}
			
			forceDstTargetBlend = true;
		}
	}
	
	if (forceDstTargetBlend)
	{
		selectedEffect = ColorEffect_Blend;
	}
	else
	{
		// If we're not forcing blending, then select the color effect based on the BLDCNT target flags.
		if (enableColorEffect && compInfo.renderState.srcEffectEnable[compInfo.renderState.selectedLayerID])
		{
			switch (compInfo.renderState.colorEffect)
			{
				// For the Blend effect, both first and second target flags must be checked.
				case ColorEffect_Blend:
				{
					if (dstTargetBlendEnable) selectedEffect = compInfo.renderState.colorEffect;
					break;
				}
					
				// For the Increase/Decrease Brightness effects, only the first target flag needs to be checked.
				// Test case: Bomberman Land Touch! dialog boxes will render too dark without this check.
				case ColorEffect_IncreaseBrightness:
				case ColorEffect_DecreaseBrightness:
					selectedEffect = compInfo.renderState.colorEffect;
					break;
					
				default:
					break;
			}
		}
	}
}

template <NDSColorFormat OUTPUTFORMAT, GPULayerType LAYERTYPE>
FORCEINLINE void PixelOperation::_unknownEffect16(GPUEngineCompositorInfo &compInfo, const Color5551 srcColor16, const bool enableColorEffect, const u8 spriteAlpha, const OBJMode spriteMode) const
{
	u8 &dstLayerID = *compInfo.target.lineLayerID;
	TBlendTable *selectedBlendTable = compInfo.renderState.blendTable555;
	u8 blendEVA = compInfo.renderState.blendEVA;
	u8 blendEVB = compInfo.renderState.blendEVB;
	ColorEffect selectedEffect = ColorEffect_Disable;
	
	this->__selectedEffect<LAYERTYPE>(compInfo, dstLayerID, enableColorEffect, spriteAlpha, spriteMode, selectedEffect, &selectedBlendTable, blendEVA, blendEVB);
	
	// Render the pixel using the selected color effect.
	dstLayerID = compInfo.renderState.selectedLayerID;
	
	if (OUTPUTFORMAT == NDSColorFormat_BGR555_Rev)
	{
		Color5551 &dstColor16 = *compInfo.target.lineColor16;
		
		switch (selectedEffect)
		{
			case ColorEffect_Disable:
				dstColor16 = srcColor16;
				break;
				
			case ColorEffect_IncreaseBrightness:
				dstColor16 = compInfo.renderState.brightnessUpTable555[srcColor16.value & 0x7FFF];
				break;
				
			case ColorEffect_DecreaseBrightness:
				dstColor16 = compInfo.renderState.brightnessDownTable555[srcColor16.value & 0x7FFF];
				break;
				
			case ColorEffect_Blend:
			{
				if (LAYERTYPE == GPULayerType_3D)
				{
					//dstColor16 = colorop.blend3D(srcColor16, dstColor16);
					printf("GPU: 3D layers cannot be in RGBA5551 format. To composite a 3D layer, use the __selectedEffect32() method instead.\n");
					assert(false);
				}
				else
				{
					dstColor16 = colorop.blend(srcColor16, dstColor16, selectedBlendTable);
				}
				break;
			}
		}
		
		dstColor16.a = 1;
	}
	else
	{
		Color4u8 &dstColor32 = *compInfo.target.lineColor32;
		
		if (OUTPUTFORMAT == NDSColorFormat_BGR666_Rev)
		{
			switch (selectedEffect)
			{
				case ColorEffect_Disable:
					dstColor32.value = ColorspaceConvert555To6665Opaque<false>(srcColor16.value);
					break;
					
				case ColorEffect_IncreaseBrightness:
					dstColor32 = compInfo.renderState.brightnessUpTable666[srcColor16.value & 0x7FFF];
					break;
					
				case ColorEffect_DecreaseBrightness:
					dstColor32 = compInfo.renderState.brightnessDownTable666[srcColor16.value & 0x7FFF];
					break;
					
				case ColorEffect_Blend:
				{
					Color4u8 srcColor32;
					srcColor32.value = ColorspaceConvert555To6665Opaque<false>(srcColor16.value);
					dstColor32 = (LAYERTYPE == GPULayerType_3D) ? colorop.blend3D<OUTPUTFORMAT>(srcColor32, dstColor32) : colorop.blend<OUTPUTFORMAT>(srcColor32, dstColor32, blendEVA, blendEVB);
					break;
				}
			}
		}
		else
		{
			switch (selectedEffect)
			{
				case ColorEffect_Disable:
					dstColor32.value = ColorspaceConvert555To8888Opaque<false>(srcColor16.value);
					break;
					
				case ColorEffect_IncreaseBrightness:
					dstColor32 = compInfo.renderState.brightnessUpTable888[srcColor16.value & 0x7FFF];
					break;
					
				case ColorEffect_DecreaseBrightness:
					dstColor32 = compInfo.renderState.brightnessDownTable888[srcColor16.value & 0x7FFF];
					break;
					
				case ColorEffect_Blend:
				{
					Color4u8 srcColor32;
					srcColor32.value = ColorspaceConvert555To8888Opaque<false>(srcColor16.value);
					dstColor32 = (LAYERTYPE == GPULayerType_3D) ? colorop.blend3D<OUTPUTFORMAT>(srcColor32, dstColor32) : colorop.blend<OUTPUTFORMAT>(srcColor32, dstColor32, blendEVA, blendEVB);
					break;
				}
			}
		}
		
		dstColor32.a = (OUTPUTFORMAT == NDSColorFormat_BGR666_Rev) ? 0x1F : 0xFF;
	}
}

template <NDSColorFormat OUTPUTFORMAT, GPULayerType LAYERTYPE>
FORCEINLINE void PixelOperation::_unknownEffect32(GPUEngineCompositorInfo &compInfo, const Color4u8 srcColor32, const bool enableColorEffect, const u8 spriteAlpha, const OBJMode spriteMode) const
{
	u8 &dstLayerID = *compInfo.target.lineLayerID;
	TBlendTable *selectedBlendTable = compInfo.renderState.blendTable555;
	u8 blendEVA = compInfo.renderState.blendEVA;
	u8 blendEVB = compInfo.renderState.blendEVB;
	ColorEffect selectedEffect = ColorEffect_Disable;
	
	this->__selectedEffect<LAYERTYPE>(compInfo, dstLayerID, enableColorEffect, spriteAlpha, spriteMode, selectedEffect, &selectedBlendTable, blendEVA, blendEVB);
	
	// Render the pixel using the selected color effect.
	dstLayerID = compInfo.renderState.selectedLayerID;
	
	if (OUTPUTFORMAT == NDSColorFormat_BGR555_Rev)
	{
		Color5551 srcColor16;
		srcColor16.value = ColorspaceConvert6665To5551<false>(srcColor32);
		
		Color5551 &dstColor16 = *compInfo.target.lineColor16;
		
		switch (selectedEffect)
		{
			case ColorEffect_Disable:
				dstColor16 = srcColor16;
				break;
				
			case ColorEffect_IncreaseBrightness:
				dstColor16 = compInfo.renderState.brightnessUpTable555[srcColor16.value & 0x7FFF];
				break;
				
			case ColorEffect_DecreaseBrightness:
				dstColor16 = compInfo.renderState.brightnessDownTable555[srcColor16.value & 0x7FFF];
				break;
				
			case ColorEffect_Blend:
			{
				if (LAYERTYPE == GPULayerType_3D)
				{
					dstColor16 = colorop.blend3D(srcColor32, dstColor16);
				}
				else
				{
					dstColor16 = colorop.blend(srcColor16, dstColor16, selectedBlendTable);
				}
				break;
			}
		}
		
		dstColor16.a = 1;
	}
	else
	{
		Color4u8 &dstColor32 = *compInfo.target.lineColor32;
		
		switch (selectedEffect)
		{
			case ColorEffect_Disable:
				dstColor32 = srcColor32;
				break;
				
			case ColorEffect_IncreaseBrightness:
				dstColor32 = colorop.increase<OUTPUTFORMAT>(srcColor32, compInfo.renderState.blendEVY);
				break;
				
			case ColorEffect_DecreaseBrightness:
				dstColor32 = colorop.decrease<OUTPUTFORMAT>(srcColor32, compInfo.renderState.blendEVY);
				break;
				
			case ColorEffect_Blend:
				dstColor32 = (LAYERTYPE == GPULayerType_3D) ? colorop.blend3D<OUTPUTFORMAT>(srcColor32, dstColor32) : colorop.blend<OUTPUTFORMAT>(srcColor32, dstColor32, blendEVA, blendEVB);
				break;
		}
		
		dstColor32.a = (OUTPUTFORMAT == NDSColorFormat_BGR666_Rev) ? 0x1F : 0xFF;
	}
}

template <GPUCompositorMode COMPOSITORMODE, NDSColorFormat OUTPUTFORMAT, GPULayerType LAYERTYPE>
FORCEINLINE void PixelOperation::Composite16(GPUEngineCompositorInfo &compInfo, const u16 srcColor16, const bool enableColorEffect, const u8 spriteAlpha, const u8 spriteMode) const
{
	Color5551 srcColor5551;
	srcColor5551.value = srcColor16;
	
	switch (COMPOSITORMODE)
	{
		case GPUCompositorMode_Debug:
			this->_copy16<OUTPUTFORMAT, true>(compInfo, srcColor5551);
			break;
			
		case GPUCompositorMode_Copy:
			this->_copy16<OUTPUTFORMAT, false>(compInfo, srcColor5551);
			break;
			
		case GPUCompositorMode_BrightUp:
			this->_brightnessUp16<OUTPUTFORMAT>(compInfo, srcColor5551);
			break;
			
		case GPUCompositorMode_BrightDown:
			this->_brightnessDown16<OUTPUTFORMAT>(compInfo, srcColor5551);
			break;
			
		default:
			this->_unknownEffect16<OUTPUTFORMAT, LAYERTYPE>(compInfo, srcColor5551, enableColorEffect, spriteAlpha, (OBJMode)spriteMode);
			break;
	}
}

template <GPUCompositorMode COMPOSITORMODE, NDSColorFormat OUTPUTFORMAT, GPULayerType LAYERTYPE>
FORCEINLINE void PixelOperation::Composite32(GPUEngineCompositorInfo &compInfo, Color4u8 srcColor32, const bool enableColorEffect, const u8 spriteAlpha, const u8 spriteMode) const
{
	switch (COMPOSITORMODE)
	{
		case GPUCompositorMode_Debug:
			this->_copy32<OUTPUTFORMAT, true>(compInfo, srcColor32);
			break;
			
		case GPUCompositorMode_Copy:
			this->_copy32<OUTPUTFORMAT, false>(compInfo, srcColor32);
			break;
			
		case GPUCompositorMode_BrightUp:
			this->_brightnessUp32<OUTPUTFORMAT>(compInfo, srcColor32);
			break;
			
		case GPUCompositorMode_BrightDown:
			this->_brightnessDown32<OUTPUTFORMAT>(compInfo, srcColor32);
			break;
			
		default:
			this->_unknownEffect32<OUTPUTFORMAT, LAYERTYPE>(compInfo, srcColor32, enableColorEffect, spriteAlpha, (OBJMode)spriteMode);
			break;
	}
}

template <size_t ELEMENTSIZE>
static FORCEINLINE void CopyLinesForVerticalCount(void *__restrict dstLineHead, size_t lineWidth, size_t lineCount)
{
	u8 *__restrict dst = (u8 *)dstLineHead + (lineWidth * ELEMENTSIZE);
	
	for (size_t line = 1; line < lineCount; line++)
	{
		memcpy(dst, dstLineHead, lineWidth * ELEMENTSIZE);
		dst += (lineWidth * ELEMENTSIZE);
	}
}

#if defined(ENABLE_AVX2)
	#include "GPU_Operations_AVX2.cpp"
#elif defined(ENABLE_SSE2)
	#include "GPU_Operations_SSE2.cpp"
#elif defined(ENABLE_NEON_A64)
	#include "GPU_Operations_NEON.cpp"
#elif defined(ENABLE_ALTIVEC)
	#include "GPU_Operations_AltiVec.cpp"
#else

template <bool NEEDENDIANSWAP, size_t ELEMENTSIZE>
static FORCEINLINE void __CopyLineSingleUnlimited(void *__restrict dst, const void *__restrict src, size_t pixCount)
{
#if defined(MSB_FIRST)
	if (NEEDENDIANSWAP && (ELEMENTSIZE > 1))
	{
		if (ELEMENTSIZE == 2)
		{
			const u16 *__restrict srcPtr16 = (const u16 *__restrict)src;
			u16 *__restrict dstPtr16       = (u16 *__restrict)dst;
			
			for (size_t i = 0; i < pixCount; i++)
			{
				dstPtr16[i] = LE_TO_LOCAL_16(srcPtr16[i]);
			}
		}
		else if (ELEMENTSIZE == 4)
		{
			const u32 *__restrict srcPtr32 = (const u32 *__restrict)src;
			u32 *__restrict dstPtr32       = (u32 *__restrict)dst;
			
			for (size_t i = 0; i < pixCount; i++)
			{
				dstPtr32[i] = LE_TO_LOCAL_32(srcPtr32[i]);
			}
		}
	}
	else
#endif
	{
		memcpy(dst, src, pixCount * ELEMENTSIZE);
	}
}

template <bool NEEDENDIANSWAP, size_t ELEMENTSIZE>
static FORCEINLINE void __CopyLineSingle(void *__restrict dst, const void *__restrict src)
{
#if defined(MSB_FIRST)
	if (NEEDENDIANSWAP && (ELEMENTSIZE > 1))
	{
		if (ELEMENTSIZE == 2)
		{
			const u16 *__restrict srcPtr16 = (const u16 *__restrict)src;
			u16 *__restrict dstPtr16       = (u16 *__restrict)dst;
			
			for (size_t i = 0; i < GPU_FRAMEBUFFER_NATIVE_WIDTH; i++)
			{
				dstPtr16[i] = LE_TO_LOCAL_16(srcPtr16[i]);
			}
		}
		else if (ELEMENTSIZE == 4)
		{
			const u32 *__restrict srcPtr32 = (const u32 *__restrict)src;
			u32 *__restrict dstPtr32       = (u32 *__restrict)dst;
			
			for (size_t i = 0; i < GPU_FRAMEBUFFER_NATIVE_WIDTH; i++)
			{
				dstPtr32[i] = LE_TO_LOCAL_32(srcPtr32[i]);
			}
		}
	}
	else
#endif
	{
		memcpy(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * ELEMENTSIZE);
	}
}

template <s32 INTEGERSCALEHINT, bool SCALEVERTICAL, bool NEEDENDIANSWAP, size_t ELEMENTSIZE>
static FORCEINLINE void CopyLineExpand(void *__restrict dst, const void *__restrict src, size_t dstWidth, size_t dstLineCount)
{
	// Use INTEGERSCALEHINT to provide a hint to CopyLineExpand() for the fastest execution path.
	// INTEGERSCALEHINT represents the scaling value of the framebuffer width, and is always
	// assumed to be a positive integer.
	//
	// Use cases:
	// - Passing a value of 0 causes CopyLineExpand() to perform a simple copy, using dstWidth
	//   to copy dstWidth elements.
	// - Passing a value of 1 causes CopyLineExpand() to perform a simple copy, ignoring dstWidth
	//   and always copying GPU_FRAMEBUFFER_NATIVE_WIDTH elements.
	// - Passing any negative value causes CopyLineExpand() to assume that the framebuffer width
	//   is NOT scaled by an integer value, and will therefore take the safest (but slowest)
	//   execution path.
	// - Passing any positive value greater than 1 causes CopyLineExpand() to expand the line
	//   using the integer scaling value.
	
	if (INTEGERSCALEHINT == 0)
	{
		__CopyLineSingleUnlimited<NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, dstWidth);
	}
	else if (INTEGERSCALEHINT == 1)
	{
		__CopyLineSingle<NEEDENDIANSWAP, ELEMENTSIZE>(dst, src);
	}
	else if (INTEGERSCALEHINT > 1)
	{
		const size_t scale = dstWidth / GPU_FRAMEBUFFER_NATIVE_WIDTH;
		
		for (size_t srcX = 0, dstX = 0; srcX < GPU_FRAMEBUFFER_NATIVE_WIDTH; srcX++, dstX+=scale)
		{
			for (size_t lx = 0; lx < scale; lx++)
			{
				if (ELEMENTSIZE == 1)
				{
					( (u8 *)dst)[(srcX * scale) + lx] = ( (u8 *)src)[srcX];
				}
				else if (ELEMENTSIZE == 2)
				{
					((u16 *)dst)[(srcX * scale) + lx] = (NEEDENDIANSWAP) ? LE_TO_LOCAL_16( ((u16 *)src)[srcX] ) : ((u16 *)src)[srcX];
				}
				else if (ELEMENTSIZE == 4)
				{
					((u32 *)dst)[(srcX * scale) + lx] = (NEEDENDIANSWAP) ? LE_TO_LOCAL_32( ((u32 *)src)[srcX] ) : ((u32 *)src)[srcX];
				}
			}
		}
		
		if (SCALEVERTICAL)
		{
			CopyLinesForVerticalCount<ELEMENTSIZE>(dst, dstWidth, dstLineCount);
		}
	}
	else
	{
		for (size_t x = 0; x < GPU_FRAMEBUFFER_NATIVE_WIDTH; x++)
		{
			for (size_t p = 0; p < _gpuDstPitchCount[x]; p++)
			{
				if (ELEMENTSIZE == 1)
				{
					( (u8 *)dst)[_gpuDstPitchIndex[x] + p] = ((u8 *)src)[x];
				}
				else if (ELEMENTSIZE == 2)
				{
					((u16 *)dst)[_gpuDstPitchIndex[x] + p] = (NEEDENDIANSWAP) ? LE_TO_LOCAL_16( ((u16 *)src)[x] ) : ((u16 *)src)[x];
				}
				else if (ELEMENTSIZE == 4)
				{
					((u32 *)dst)[_gpuDstPitchIndex[x] + p] = (NEEDENDIANSWAP) ? LE_TO_LOCAL_32( ((u32 *)src)[x] ) : ((u32 *)src)[x];
				}
			}
		}
		
		if (SCALEVERTICAL)
		{
			CopyLinesForVerticalCount<ELEMENTSIZE>(dst, dstWidth, dstLineCount);
		}
	}
}

template <s32 INTEGERSCALEHINT, bool NEEDENDIANSWAP, size_t ELEMENTSIZE>
static FORCEINLINE void CopyLineReduce(void *__restrict dst, const void *__restrict src, size_t srcWidth)
{
	// Use INTEGERSCALEHINT to provide a hint to CopyLineReduce() for the fastest execution path.
	// INTEGERSCALEHINT represents the scaling value of the source framebuffer width, and is always
	// assumed to be a positive integer.
	//
	// Use cases:
	// - Passing a value of 0 causes CopyLineReduce() to perform a simple copy, using srcWidth
	//   to copy srcWidth elements.
	// - Passing a value of 1 causes CopyLineReduce() to perform a simple copy, ignoring srcWidth
	//   and always copying GPU_FRAMEBUFFER_NATIVE_WIDTH elements.
	// - Passing any negative value causes CopyLineReduce() to assume that the framebuffer width
	//   is NOT scaled by an integer value, and will therefore take the safest (but slowest)
	//   execution path.
	// - Passing any positive value greater than 1 causes CopyLineReduce() to expand the line
	//   using the integer scaling value.
	
	if (INTEGERSCALEHINT == 0)
	{
		__CopyLineSingleUnlimited<NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, srcWidth);
	}
	else if (INTEGERSCALEHINT == 1)
	{
		__CopyLineSingle<NEEDENDIANSWAP, ELEMENTSIZE>(dst, src);
	}
	else if (INTEGERSCALEHINT > 1)
	{
		const size_t scale = srcWidth / GPU_FRAMEBUFFER_NATIVE_WIDTH;
		
		for (size_t x = 0; x < GPU_FRAMEBUFFER_NATIVE_WIDTH; x++)
		{
			if (ELEMENTSIZE == 1)
			{
				((u8 *)dst)[x] = ((u8 *)src)[x * scale];
			}
			else if (ELEMENTSIZE == 2)
			{
				((u16 *)dst)[x] = (NEEDENDIANSWAP) ? LE_TO_LOCAL_16( ((u16 *)src)[x * scale] ) : ((u16 *)src)[x * scale];
			}
			else if (ELEMENTSIZE == 4)
			{
				((u32 *)dst)[x] = (NEEDENDIANSWAP) ? LE_TO_LOCAL_32( ((u32 *)src)[x * scale] ) : ((u32 *)src)[x * scale];
			}
		}
	}
	else
	{
		for (size_t i = 0; i < GPU_FRAMEBUFFER_NATIVE_WIDTH; i++)
		{
			if (ELEMENTSIZE == 1)
			{
				( (u8 *)dst)[i] = ((u8 *)src)[_gpuDstPitchIndex[i]];
			}
			else if (ELEMENTSIZE == 2)
			{
				((u16 *)dst)[i] = (NEEDENDIANSWAP) ? LE_TO_LOCAL_16( ((u16 *)src)[_gpuDstPitchIndex[i]] ) : ((u16 *)src)[_gpuDstPitchIndex[i]];
			}
			else if (ELEMENTSIZE == 4)
			{
				((u32 *)dst)[i] = (NEEDENDIANSWAP) ? LE_TO_LOCAL_32( ((u32 *)src)[_gpuDstPitchIndex[i]] ) : ((u32 *)src)[_gpuDstPitchIndex[i]];
			}
		}
	}
}

template <bool ISFIRSTLINE>
void GPUEngineBase::_MosaicLine(GPUEngineCompositorInfo &compInfo)
{
	u16 *mosaicColorBG = this->_mosaicColors.bg[compInfo.renderState.selectedLayerID];
	u16 outColor16;
	bool isOpaque;
	
	for (size_t x = 0; x < GPU_FRAMEBUFFER_NATIVE_WIDTH; x++)
	{
		if (ISFIRSTLINE && (compInfo.renderState.mosaicWidthBG->begin[x] != 0))
		{
			isOpaque = (this->_deferredIndexNative[x] != 0);
			outColor16 = (isOpaque) ? (this->_deferredColorNative[x] & 0x7FFF) : 0xFFFF;
			mosaicColorBG[x] = outColor16;
		}
		else
		{
			outColor16 = mosaicColorBG[compInfo.renderState.mosaicWidthBG->trunc[x]];
			isOpaque = (outColor16 != 0xFFFF);
		}
		
		if (isOpaque)
		{
			this->_deferredColorNative[x] = outColor16;
		}
	}
}

template <GPUCompositorMode COMPOSITORMODE, NDSColorFormat OUTPUTFORMAT, bool WILLPERFORMWINDOWTEST>
void GPUEngineBase::_CompositeNativeLineOBJ_LoopOp(GPUEngineCompositorInfo &compInfo, const u16 *__restrict srcColorNative16, const Color4u8 *__restrict srcColorNative32)
{
	// Do nothing. This is a placeholder for a manually vectorized version of this method.
}

template <GPUCompositorMode COMPOSITORMODE, NDSColorFormat OUTPUTFORMAT, GPULayerType LAYERTYPE, bool WILLPERFORMWINDOWTEST>
size_t GPUEngineBase::_CompositeLineDeferred_LoopOp(GPUEngineCompositorInfo &compInfo, const u8 *__restrict windowTestPtr, const u8 *__restrict colorEffectEnablePtr, const u16 *__restrict srcColorCustom16, const u8 *__restrict srcIndexCustom)
{
	// Do nothing. This is a placeholder for a manually vectorized version of this method.
	return 0;
}

template <GPUCompositorMode COMPOSITORMODE, NDSColorFormat OUTPUTFORMAT, GPULayerType LAYERTYPE, bool WILLPERFORMWINDOWTEST>
size_t GPUEngineBase::_CompositeVRAMLineDeferred_LoopOp(GPUEngineCompositorInfo &compInfo, const u8 *__restrict windowTestPtr, const u8 *__restrict colorEffectEnablePtr, const void *__restrict vramColorPtr)
{
	// Do nothing. This is a placeholder for a manually vectorized version of this method.
	return 0;
}

template <bool ISDEBUGRENDER>
size_t GPUEngineBase::_RenderSpriteBMP_LoopOp(const size_t length, const u8 spriteAlpha, const u8 prio, const u8 spriteNum, const u16 *__restrict vramBuffer,
											  size_t &frameX, size_t &spriteX,
											  u16 *__restrict dst, u8 *__restrict dst_alpha, u8 *__restrict typeTab, u8 *__restrict prioTab)
{
	// Do nothing. This is a placeholder for a manually vectorized version of this method.
	return 0;
}

void GPUEngineBase::_PerformWindowTestingNative(GPUEngineCompositorInfo &compInfo, const size_t layerID, const u8 *__restrict win0, const u8 *__restrict win1, const u8 *__restrict winObj, u8 *__restrict didPassWindowTestNative, u8 *__restrict enableColorEffectNative)
{
	for (size_t i = 0; i < GPU_FRAMEBUFFER_NATIVE_WIDTH; i++)
	{
		// Window 0 has the highest priority, so always check this first.
		if (win0 != NULL)
		{
			if (win0[i] != 0)
			{
				didPassWindowTestNative[i] = compInfo.renderState.WIN0_enable[layerID];
				enableColorEffectNative[i] = compInfo.renderState.WIN0_enable[WINDOWCONTROL_EFFECTFLAG];
				continue;
			}
		}
		
		// Window 1 has medium priority, and is checked after Window 0.
		if (win1 != NULL)
		{
			if (win1[i] != 0)
			{
				didPassWindowTestNative[i] = compInfo.renderState.WIN1_enable[layerID];
				enableColorEffectNative[i] = compInfo.renderState.WIN1_enable[WINDOWCONTROL_EFFECTFLAG];
				continue;
			}
		}
		
		// Window OBJ has low priority, and is checked after both Window 0 and Window 1.
		if (winObj != NULL)
		{
			if (winObj[i] != 0)
			{
				didPassWindowTestNative[i] = compInfo.renderState.WINOBJ_enable[layerID];
				enableColorEffectNative[i] = compInfo.renderState.WINOBJ_enable[WINDOWCONTROL_EFFECTFLAG];
				continue;
			}
		}
		
		// If the pixel isn't inside any windows, then the pixel is outside, and therefore uses the WINOUT flags.
		// This has the lowest priority, and is always checked last.
		didPassWindowTestNative[i] = compInfo.renderState.WINOUT_enable[layerID];
		enableColorEffectNative[i] = compInfo.renderState.WINOUT_enable[WINDOWCONTROL_EFFECTFLAG];
	}
}

template <GPUCompositorMode COMPOSITORMODE, NDSColorFormat OUTPUTFORMAT, bool WILLPERFORMWINDOWTEST>
size_t GPUEngineA::_RenderLine_Layer3D_LoopOp(GPUEngineCompositorInfo &compInfo, const u8 *__restrict windowTestPtr, const u8 *__restrict colorEffectEnablePtr, const Color4u8 *__restrict srcLinePtr)
{
	// Do nothing. This is a placeholder for a manually vectorized version of this method.
	return 0;
}

template<NDSColorFormat OUTPUTFORMAT>
size_t GPUEngineA::_RenderLine_DispCapture_Blend_VecLoop(const void *srcA, const void *srcB, void *dst, const u8 blendEVA, const u8 blendEVB, const size_t length)
{
	// Do nothing. This is a placeholder for a manually vectorized version of this method.
	return 0;
}

template <NDSColorFormat OUTPUTFORMAT>
size_t NDSDisplay::_ApplyMasterBrightnessUp_LoopOp(void *__restrict dst, const size_t pixCount, const u8 intensityClamped)
{
	// Do nothing. This is a placeholder for a manually vectorized version of this method.
	return 0;
}

template <NDSColorFormat OUTPUTFORMAT>
size_t NDSDisplay::_ApplyMasterBrightnessDown_LoopOp(void *__restrict dst, const size_t pixCount, const u8 intensityClamped)
{
	// Do nothing. This is a placeholder for a manually vectorized version of this method.
	return 0;
}

#endif

template <s32 INTEGERSCALEHINT, bool SCALEVERTICAL, bool USELINEINDEX, bool NEEDENDIANSWAP, size_t ELEMENTSIZE>
void CopyLineExpandHinted(const void *__restrict srcBuffer, const size_t srcLineIndex,
						  void *__restrict dstBuffer, const size_t dstLineIndex, const size_t dstLineWidth, const size_t dstLineCount)
{
	switch (INTEGERSCALEHINT)
	{
		case 0:
		{
			const u8 *__restrict src = (USELINEINDEX) ? (u8 *)srcBuffer + (dstLineIndex * dstLineWidth * ELEMENTSIZE) : (u8 *)srcBuffer;
			u8 *__restrict dst = (USELINEINDEX) ? (u8 *)dstBuffer + (dstLineIndex * dstLineWidth * ELEMENTSIZE) : (u8 *)dstBuffer;
			
			CopyLineExpand<INTEGERSCALEHINT, true, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, dstLineWidth * dstLineCount, 1);
			break;
		}
			
		case 1:
		{
			const u8 *__restrict src = (USELINEINDEX) ? (u8 *)srcBuffer + (srcLineIndex * GPU_FRAMEBUFFER_NATIVE_WIDTH * ELEMENTSIZE) : (u8 *)srcBuffer;
			u8 *__restrict dst = (USELINEINDEX) ? (u8 *)dstBuffer + (srcLineIndex * GPU_FRAMEBUFFER_NATIVE_WIDTH * ELEMENTSIZE) : (u8 *)dstBuffer;
			
			CopyLineExpand<INTEGERSCALEHINT, true, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH, 1);
			break;
		}
			
		default:
		{
			const u8 *__restrict src = (USELINEINDEX) ? (u8 *)srcBuffer + (srcLineIndex * GPU_FRAMEBUFFER_NATIVE_WIDTH * ELEMENTSIZE) : (u8 *)srcBuffer;
			u8 *__restrict dst = (USELINEINDEX) ? (u8 *)dstBuffer + (dstLineIndex * dstLineWidth * ELEMENTSIZE) : (u8 *)dstBuffer;
			
			// TODO: Determine INTEGERSCALEHINT earlier in the pipeline, preferably when the framebuffer is first initialized.
			//
			// The implementation below is a stopgap measure for getting the faster code paths to run.
			// However, this setup is not ideal, since the code size will greatly increase in order to
			// include all possible code paths, possibly causing cache misses on lesser CPUs.
			switch (dstLineWidth)
			{
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 2):
					CopyLineExpand<2, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 2, 2);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 3):
					CopyLineExpand<3, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 3, 3);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 4):
					CopyLineExpand<4, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 4, 4);
					break;

// Building on MSVC takes too long when LTO is on (typical use case), so remove these extra calls to
// CopyLineExpand() in order to reduce the number of permutations and make build times more sane.
// Other compilers, such as GCC and Clang, have no problems with building using LTO within a
// reasonable time frame.
#ifndef _MSC_VER
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 5):
					CopyLineExpand<5, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 5, 5);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 6):
					CopyLineExpand<6, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 6, 6);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 7):
					CopyLineExpand<7, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 7, 7);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 8):
					CopyLineExpand<8, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 8, 8);
					break;

				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 9):
					CopyLineExpand<9, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 9, 9);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 10):
					CopyLineExpand<10, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 10, 10);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 11):
					CopyLineExpand<11, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 11, 11);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 12):
					CopyLineExpand<12, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 12, 12);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 13):
					CopyLineExpand<13, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 13, 13);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 14):
					CopyLineExpand<14, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 14, 14);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 15):
					CopyLineExpand<15, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 15, 15);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 16):
					CopyLineExpand<16, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 16, 16);
					break;
#endif
				default:
				{
					if ((dstLineWidth % GPU_FRAMEBUFFER_NATIVE_WIDTH) == 0)
					{
						CopyLineExpand<0x3FFF, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, dstLineWidth, dstLineCount);
					}
					else
					{
						CopyLineExpand<-1, SCALEVERTICAL, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, dstLineWidth, dstLineCount);
					}
					break;
				}
			}
			break;
		}
	}
}

template <s32 INTEGERSCALEHINT, bool SCALEVERTICAL, bool USELINEINDEX, bool NEEDENDIANSWAP, size_t ELEMENTSIZE>
void CopyLineExpandHinted(const GPUEngineLineInfo &lineInfo, const void *__restrict srcBuffer, void *__restrict dstBuffer)
{
	CopyLineExpandHinted<INTEGERSCALEHINT, SCALEVERTICAL, USELINEINDEX, NEEDENDIANSWAP, ELEMENTSIZE>(srcBuffer, lineInfo.indexNative,
																									 dstBuffer, lineInfo.indexCustom, lineInfo.widthCustom, lineInfo.renderCount);
}

template <s32 INTEGERSCALEHINT, bool USELINEINDEX, bool NEEDENDIANSWAP, size_t ELEMENTSIZE>
void CopyLineReduceHinted(const void *__restrict srcBuffer, const size_t srcLineIndex, const size_t srcLineWidth,
						  void *__restrict dstBuffer, const size_t dstLineIndex)
{
	switch (INTEGERSCALEHINT)
	{
		case 0:
		{
			const u8 *__restrict src = (USELINEINDEX) ? (u8 *)srcBuffer + (srcLineIndex * srcLineWidth * ELEMENTSIZE) : (u8 *)srcBuffer;
			u8 *__restrict dst = (USELINEINDEX) ? (u8 *)dstBuffer + (srcLineIndex * srcLineWidth * ELEMENTSIZE) : (u8 *)dstBuffer;
			
			CopyLineReduce<INTEGERSCALEHINT, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, srcLineWidth);
			break;
		}
			
		case 1:
		{
			const u8 *__restrict src = (USELINEINDEX) ? (u8 *)srcBuffer + (dstLineIndex * GPU_FRAMEBUFFER_NATIVE_WIDTH * ELEMENTSIZE) : (u8 *)srcBuffer;
			u8 *__restrict dst = (USELINEINDEX) ? (u8 *)dstBuffer + (dstLineIndex * GPU_FRAMEBUFFER_NATIVE_WIDTH * ELEMENTSIZE) : (u8 *)dstBuffer;
			
			CopyLineReduce<INTEGERSCALEHINT, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH);
			break;
		}
			
		default:
		{
			const u8 *__restrict src = (USELINEINDEX) ? (u8 *)srcBuffer + (srcLineIndex * srcLineWidth * ELEMENTSIZE) : (u8 *)srcBuffer;
			u8 *__restrict dst = (USELINEINDEX) ? (u8 *)dstBuffer + (dstLineIndex * GPU_FRAMEBUFFER_NATIVE_WIDTH * ELEMENTSIZE) : (u8 *)dstBuffer;
			
			// TODO: Determine INTEGERSCALEHINT earlier in the pipeline, preferably when the framebuffer is first initialized.
			//
			// The implementation below is a stopgap measure for getting the faster code paths to run.
			// However, this setup is not ideal, since the code size will greatly increase in order to
			// include all possible code paths, possibly causing cache misses on lesser CPUs.
			switch (srcLineWidth)
			{
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 2):
					CopyLineReduce<2, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 2);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 3):
					CopyLineReduce<3, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 3);
					break;
					
				case (GPU_FRAMEBUFFER_NATIVE_WIDTH * 4):
					CopyLineReduce<4, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, GPU_FRAMEBUFFER_NATIVE_WIDTH * 4);
					break;
					
				default:
				{
					if ((srcLineWidth % GPU_FRAMEBUFFER_NATIVE_WIDTH) == 0)
					{
						CopyLineReduce<0x3FFF, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, srcLineWidth);
					}
					else
					{
						CopyLineReduce<-1, NEEDENDIANSWAP, ELEMENTSIZE>(dst, src, srcLineWidth);
					}
					break;
				}
			}
			break;
		}
	}
}

template <s32 INTEGERSCALEHINT, bool USELINEINDEX, bool NEEDENDIANSWAP, size_t ELEMENTSIZE>
void CopyLineReduceHinted(const GPUEngineLineInfo &lineInfo, const void *__restrict srcBuffer, void *__restrict dstBuffer)
{
	CopyLineReduceHinted<INTEGERSCALEHINT, USELINEINDEX, NEEDENDIANSWAP, ELEMENTSIZE>(srcBuffer, lineInfo.indexCustom, lineInfo.widthCustom,
																					  dstBuffer, lineInfo.indexNative);
}

// These functions are used in gfx3d.cpp
template void CopyLineExpandHinted<0x3FFF, true, false, false, 4>(const GPUEngineLineInfo &lineInfo, const void *__restrict srcBuffer, void *__restrict dstBuffer);
template void CopyLineReduceHinted<0x3FFF, false, false, 4>(const GPUEngineLineInfo &lineInfo, const void *__restrict srcBuffer, void *__restrict dstBuffer);
