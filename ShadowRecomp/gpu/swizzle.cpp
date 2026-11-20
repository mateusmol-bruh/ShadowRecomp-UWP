#include "swizzle.h"
#include <xxHashMap.h>
#include <kernel/heap.h>
#include <algorithm>
#include <os/logger.h>

/*Xbox uses a swizzle method for textures, this is a memory layout optimization used by GPUs, instead of storing pixels in
row-major order (line by line), the Xbox stores them in a Morton order (Z-order curve)  for better texture cache locality
during rendering. We can't use that on PC, so we have to unswizzle textures that need it.*/


//original code (mostly) re used from CXBX Reloaded

typedef enum _D3DFORMAT
{
	D3DFMT_UNKNOWN = 0,

	D3DFMT_R8G8B8 = 20,
	D3DFMT_A8R8G8B8 = 21,
	D3DFMT_X8R8G8B8 = 22,
	D3DFMT_R5G6B5 = 23,
	D3DFMT_X1R5G5B5 = 24,
	D3DFMT_A1R5G5B5 = 25,
	D3DFMT_A4R4G4B4 = 26,
	D3DFMT_R3G3B2 = 27,
	D3DFMT_A8 = 28,
	D3DFMT_A8R3G3B2 = 29,
	D3DFMT_X4R4G4B4 = 30,
	D3DFMT_A2B10G10R10 = 31,
	D3DFMT_A8B8G8R8 = 32,
	D3DFMT_X8B8G8R8 = 33,
	D3DFMT_G16R16 = 34,
	D3DFMT_A2R10G10B10 = 35,
	D3DFMT_A16B16G16R16 = 36,

	D3DFMT_A8P8 = 40,
	D3DFMT_P8 = 41,

	D3DFMT_L8 = 50,
	D3DFMT_A8L8 = 51,
	D3DFMT_A4L4 = 52,

	D3DFMT_V8U8 = 60,
	D3DFMT_L6V5U5 = 61,
	D3DFMT_X8L8V8U8 = 62,
	D3DFMT_Q8W8V8U8 = 63,
	D3DFMT_V16U16 = 64,
	D3DFMT_A2W10V10U10 = 67,

	D3DFMT_UYVY = SDL_FOURCC('U', 'Y', 'V', 'Y'),
	D3DFMT_R8G8_B8G8 = SDL_FOURCC('R', 'G', 'B', 'G'),
	D3DFMT_YUY2 = SDL_FOURCC('Y', 'U', 'Y', '2'),
	D3DFMT_G8R8_G8B8 = SDL_FOURCC('G', 'R', 'G', 'B'),
	D3DFMT_DXT1 = SDL_FOURCC('D', 'X', 'T', '1'),
	D3DFMT_DXT2 = SDL_FOURCC('D', 'X', 'T', '2'),
	D3DFMT_DXT3 = SDL_FOURCC('D', 'X', 'T', '3'),
	D3DFMT_DXT4 = SDL_FOURCC('D', 'X', 'T', '4'),
	D3DFMT_DXT5 = SDL_FOURCC('D', 'X', 'T', '5'),

	D3DFMT_D16_LOCKABLE = 70,
	D3DFMT_D32 = 71,
	D3DFMT_D15S1 = 73,
	D3DFMT_D24S8 = 75,
	D3DFMT_D24X8 = 77,
	D3DFMT_D24X4S4 = 79,
	D3DFMT_D16 = 80,

	D3DFMT_D32F_LOCKABLE = 82,
	D3DFMT_D24FS8 = 83,

	/* D3D9Ex only -- */
#if !defined(D3D_DISABLE_9EX)

	/* Z-Stencil formats valid for CPU access */
	D3DFMT_D32_LOCKABLE = 84,
	D3DFMT_S8_LOCKABLE = 85,

#endif // !D3D_DISABLE_9EX
	/* -- D3D9Ex only */

	D3DFMT_L16 = 81,

	D3DFMT_VERTEXDATA = 100,
	D3DFMT_INDEX16 = 101,
	D3DFMT_INDEX32 = 102,

	D3DFMT_Q16W16V16U16 = 110,

	D3DFMT_MULTI2_ARGB8 = SDL_FOURCC('M', 'E', 'T', '1'),

	// Floating point surface formats

	// s10e5 formats (16-bits per channel)
	D3DFMT_R16F = 111,
	D3DFMT_G16R16F = 112,
	D3DFMT_A16B16G16R16F = 113,

	// IEEE s23e8 formats (32-bits per channel)
	D3DFMT_R32F = 114,
	D3DFMT_G32R32F = 115,
	D3DFMT_A32B32G32R32F = 116,

	D3DFMT_CxV8U8 = 117,

	/* D3D9Ex only -- */
#if !defined(D3D_DISABLE_9EX)

	// Monochrome 1 bit per pixel format
	D3DFMT_A1 = 118,

	// 2.8 biased fixed point
	D3DFMT_A2B10G10R10_XR_BIAS = 119,

	// Binary format indicating that the data has no inherent type
	D3DFMT_BINARYBUFFER = 199,

#endif // !D3D_DISABLE_9EX
	/* -- D3D9Ex only */
	D3DFMT_FORCE_DWORD = 0x7fffffff
} D3DFORMAT;

enum _FormatStorage {
	Undfnd = 0, // Undefined
	Linear,
	Swzzld, // Swizzled
	Cmprsd // Compressed
};

enum _FormatUsage {
	Texture = 0,
	RenderTarget,
	DepthBuffer
};

// About format color components:
// A = alpha, byte : 0 = fully opaque, 255 = fully transparent
// X = ignore these component bits
// R = red
// G = green
// B = blue
// L = luminance, byte : 0 = pure black ARGB(1, 0,0,0) to 255 = pure white ARGB(1,255,255,255)
// P = pallete
enum _ComponentEncoding {
	NoCmpnts = 0, // Format doesn't contain any component (ARGB/QWVU)
	A1R5G5B5,
	X1R5G5B5, // NOTE : A=255
	A4R4G4B4,
	__R5G6B5, // NOTE : A=255
	A8R8G8B8,
	X8R8G8B8, // NOTE : A=255
	____R8B8, // NOTE : A takes R, G takes B
	____G8B8, // NOTE : A takes G, R takes B
	______A8, // TEST : R=G=B= 255
	__R6G5B5, // NOTE : A=255
	R5G5B5A1,
	R4G4B4A4,
	A8B8G8R8,
	B8G8R8A8,
	R8G8B8A8,
	______L8, // NOTE : A=255, R=G=B= L
	_____AL8, // NOTE : A=R=G=B= L
	_____L16, // NOTE : Actually G8B8, with A=R=255
	____A8L8, // NOTE : R=G=B= L
	____DXT1,
	____DXT3,
	____DXT5,
	______P8,
	____YUY2,
	____UYVY,
};

typedef struct _FormatInfo {
	uint8_t bits_per_pixel;
	_FormatStorage stored;
	_ComponentEncoding components;
	D3DFORMAT pc;
	_FormatUsage usage;
	const char* warning;
} FormatInfo;

//todo this may need an enum convert to DX12 / Plume?
static const FormatInfo FormatInfos[] = {
	// Notes :
	// * This table should use a native format that most closely mirrors the Xbox format,
	// and (for now) MUST use a format that uses the same number of bits/bytes per pixel,
	// as otherwise locked buffers wouldn't be of the same size as on the Xbox, which
	// could lead to out-of-bounds access violations.
	// * Each format for which the host D3D8 doesn't have an identical native format,
	// and does specify color components (other than NoCmpnts), MUST specify this fact
	// in the warning field, so EmuXBFormatRequiresConversionToARGB can return a conversion
	// to ARGB is needed (which is implemented in EMUPATCH(D3DResource_Register).

	/* 0x00 X_D3DFMT_L8           */ {  8, Swzzld, ______L8, D3DFMT_L8        },
	/* 0x01 X_D3DFMT_AL8          */ {  8, Swzzld, _____AL8, D3DFMT_L8        , Texture, "X_D3DFMT_AL8 -> D3DFMT_L8" },
	/* 0x02 X_D3DFMT_A1R5G5B5     */ { 16, Swzzld, A1R5G5B5, D3DFMT_A1R5G5B5  },
	/* 0x03 X_D3DFMT_X1R5G5B5     */ { 16, Swzzld, X1R5G5B5, D3DFMT_X1R5G5B5  , RenderTarget },
	/* 0x04 X_D3DFMT_A4R4G4B4     */ { 16, Swzzld, A4R4G4B4, D3DFMT_A4R4G4B4  },
	/* 0x05 X_D3DFMT_R5G6B5       */ { 16, Swzzld, __R5G6B5, D3DFMT_R5G6B5    , RenderTarget },
	/* 0x06 X_D3DFMT_A8R8G8B8     */ { 32, Swzzld, A8R8G8B8, D3DFMT_A8R8G8B8  , RenderTarget },
	/* 0x07 X_D3DFMT_X8R8G8B8     */ { 32, Swzzld, X8R8G8B8, D3DFMT_X8R8G8B8  , RenderTarget }, // Alias : X_D3DFMT_X8L8V8U8 
	/* 0x08 undefined             */ {},
	/* 0x09 undefined             */ {},
	/* 0x0A undefined             */ {},
	/* 0x0B X_D3DFMT_P8           */ {  8, Swzzld, ______P8, D3DFMT_P8        , Texture, "X_D3DFMT_P8 -> D3DFMT_L8" }, // 8-bit palletized
	/* 0x0C X_D3DFMT_DXT1         */ {  4, Cmprsd, ____DXT1, D3DFMT_DXT1      }, // opaque/one-bit alpha // NOTE : DXT1 is half byte per pixel, so divide Size and Pitch calculations by two!
	/* 0x0D undefined             */ {},
	/* 0x0E X_D3DFMT_DXT3         */ {  8, Cmprsd, ____DXT3, D3DFMT_DXT3      }, // Alias : X_D3DFMT_DXT2 // linear alpha
	/* 0x0F X_D3DFMT_DXT5         */ {  8, Cmprsd, ____DXT5, D3DFMT_DXT5      }, // Alias : X_D3DFMT_DXT4 // interpolated alpha
	/* 0x10 X_D3DFMT_LIN_A1R5G5B5 */ { 16, Linear, A1R5G5B5, D3DFMT_A1R5G5B5  },
	/* 0x11 X_D3DFMT_LIN_R5G6B5   */ { 16, Linear, __R5G6B5, D3DFMT_R5G6B5    , RenderTarget },
	/* 0x12 X_D3DFMT_LIN_A8R8G8B8 */ { 32, Linear, A8R8G8B8, D3DFMT_A8R8G8B8  , RenderTarget },
	/* 0x13 X_D3DFMT_LIN_L8       */ {  8, Linear, ______L8, D3DFMT_L8        , RenderTarget },
	/* 0x14 undefined             */ {},
	/* 0x15 undefined             */ {},
	/* 0x16 X_D3DFMT_LIN_R8B8     */ { 16, Linear, ____R8B8, D3DFMT_R5G6B5    , Texture, "X_D3DFMT_LIN_R8B8 -> D3DFMT_R5G6B5" },
	/* 0x17 X_D3DFMT_LIN_G8B8     */ { 16, Linear, ____G8B8, D3DFMT_R5G6B5    , RenderTarget, "X_D3DFMT_LIN_G8B8 -> D3DFMT_R5G6B5" }, // Alias : X_D3DFMT_LIN_V8U8
	/* 0x18 undefined             */ {},
	/* 0x19 X_D3DFMT_A8           */ {  8, Swzzld, ______A8, D3DFMT_A8        , Texture, "X_D3DFMT_A8 -> D3DFMT_A8R8G8B8" },  // D3D9 sets RGB = 0 instead of 1
	/* 0x1A X_D3DFMT_A8L8         */ { 16, Swzzld, ____A8L8, D3DFMT_A8L8      },
	/* 0x1B X_D3DFMT_LIN_AL8      */ {  8, Linear, _____AL8, D3DFMT_L8        , Texture, "X_D3DFMT_LIN_AL8 -> D3DFMT_L8" },
	/* 0x1C X_D3DFMT_LIN_X1R5G5B5 */ { 16, Linear, X1R5G5B5, D3DFMT_X1R5G5B5  , RenderTarget },
	/* 0x1D X_D3DFMT_LIN_A4R4G4B4 */ { 16, Linear, A4R4G4B4, D3DFMT_A4R4G4B4  },
	/* 0x1E X_D3DFMT_LIN_X8R8G8B8 */ { 32, Linear, X8R8G8B8, D3DFMT_X8R8G8B8  , RenderTarget }, // Alias : X_D3DFMT_LIN_X8L8V8U8
	/* 0x1F X_D3DFMT_LIN_A8       */ {  8, Linear, ______A8, D3DFMT_A8        , Texture, "X_D3DFMT_LIN_A8 -> D3DFMT_A8R8G8B8" }, // D3D9 sets RGB = 0 instead of 1
	/* 0x20 X_D3DFMT_LIN_A8L8     */ { 16, Linear, ____A8L8, D3DFMT_A8L8      },
	/* 0x21 undefined             */ {},
	/* 0x22 undefined             */ {},
	/* 0x23 undefined             */ {},
	/* 0x24 X_D3DFMT_YUY2         */ { 16, Linear, ____YUY2, D3DFMT_YUY2      },
	/* 0x25 X_D3DFMT_UYVY         */ { 16, Linear, ____UYVY, D3DFMT_UYVY      },
	/* 0x26 undefined             */ {},
	/* 0x27 X_D3DFMT_L6V5U5       */ { 16, Swzzld, __R6G5B5, D3DFMT_L6V5U5    }, // Alias : X_D3DFMT_R6G5B5 // XQEMU NOTE : This might be signed
	/* 0x28 X_D3DFMT_V8U8         */ { 16, Swzzld, ____G8B8, D3DFMT_V8U8      }, // Alias : X_D3DFMT_G8B8 // XQEMU NOTE : This might be signed
	/* 0x29 X_D3DFMT_R8B8         */ { 16, Swzzld, ____R8B8, D3DFMT_R5G6B5    , Texture, "X_D3DFMT_R8B8 -> D3DFMT_R5G6B5" }, // XQEMU NOTE : This might be signed
	/* 0x2A X_D3DFMT_D24S8        */ { 32, Swzzld, NoCmpnts, D3DFMT_D24S8     , DepthBuffer },
	/* 0x2B X_D3DFMT_F24S8        */ { 32, Swzzld, NoCmpnts, D3DFMT_D24FS8    , DepthBuffer },
	/* 0x2C X_D3DFMT_D16          */ { 16, Swzzld, NoCmpnts, D3DFMT_D16       , DepthBuffer }, // Note : X_D3DFMT_D16 is always lockable on Xbox, D3DFMT_D16 on host is not, but D3DFMT_D16_LOCKABLE often fails SetRenderTarget.
	/* 0x2D X_D3DFMT_F16          */ { 16, Swzzld, NoCmpnts, D3DFMT_D16       , DepthBuffer, "X_D3DFMT_F16 -> D3DFMT_D16" }, // HACK : PC doesn't have D3DFMT_F16 (Float vs Int) // TODO : Use D3DFMT_R16F?
	/* 0x2E X_D3DFMT_LIN_D24S8    */ { 32, Linear, NoCmpnts, D3DFMT_D24S8     , DepthBuffer },
	/* 0x2F X_D3DFMT_LIN_F24S8    */ { 32, Linear, NoCmpnts, D3DFMT_D24FS8    , DepthBuffer },
	/* 0x30 X_D3DFMT_LIN_D16      */ { 16, Linear, NoCmpnts, D3DFMT_D16       , DepthBuffer }, // Note : X_D3DFMT_D16 is always lockable on Xbox, D3DFMT_D16 on host is not, but D3DFMT_D16_LOCKABLE often fails SetRenderTarget.
	/* 0x31 X_D3DFMT_LIN_F16      */ { 16, Linear, NoCmpnts, D3DFMT_D16       , DepthBuffer, "X_D3DFMT_LIN_F16 -> D3DFMT_D16" }, // HACK : PC doesn't have D3DFMT_F16 (Float vs Int) // TODO : Use D3DFMT_R16F?
	/* 0x32 X_D3DFMT_L16          */ { 16, Swzzld, _____L16, D3DFMT_L16       },
	/* 0x33 X_D3DFMT_V16U16       */ { 32, Swzzld, NoCmpnts, D3DFMT_V16U16    },
	/* 0x34 undefined             */ {},
	/* 0x35 X_D3DFMT_LIN_L16      */ { 16, Linear, _____L16, D3DFMT_L16       },
	/* 0x36 X_D3DFMT_LIN_V16U16   */ { 32, Linear, NoCmpnts, D3DFMT_V16U16    }, // Note : Seems unused on Xbox
	/* 0x37 X_D3DFMT_LIN_L6V5U5   */ { 16, Linear, __R6G5B5, D3DFMT_L6V5U5    }, // Alias : X_D3DFMT_LIN_R6G5B5
	/* 0x38 X_D3DFMT_R5G5B5A1     */ { 16, Swzzld, R5G5B5A1, D3DFMT_A1R5G5B5  , Texture, "X_D3DFMT_R5G5B5A1 -> D3DFMT_A1R5G5B5" },
	/* 0x39 X_D3DFMT_R4G4B4A4     */ { 16, Swzzld, R4G4B4A4, D3DFMT_A4R4G4B4  , Texture, "X_D3DFMT_R4G4B4A4 -> D3DFMT_A4R4G4B4" },
	/* 0x3A X_D3DFMT_Q8W8V8U8     */ { 32, Swzzld, A8B8G8R8, D3DFMT_Q8W8V8U8  }, // Alias : X_D3DFMT_A8B8G8R8 // Note : D3DFMT_A8B8G8R8=32 D3DFMT_Q8W8V8U8=63 // TODO : Needs testcase.
	/* 0x3B X_D3DFMT_B8G8R8A8     */ { 32, Swzzld, B8G8R8A8, D3DFMT_A8R8G8B8  , Texture, "X_D3DFMT_B8G8R8A8 -> D3DFMT_A8R8G8B8" },
	/* 0x3C X_D3DFMT_R8G8B8A8     */ { 32, Swzzld, R8G8B8A8, D3DFMT_A8R8G8B8  , Texture, "X_D3DFMT_R8G8B8A8 -> D3DFMT_A8R8G8B8" },
	/* 0x3D X_D3DFMT_LIN_R5G5B5A1 */ { 16, Linear, R5G5B5A1, D3DFMT_A1R5G5B5  , Texture, "X_D3DFMT_LIN_R5G5B5A1 -> D3DFMT_A1R5G5B5" },
	/* 0x3E X_D3DFMT_LIN_R4G4B4A4 */ { 16, Linear, R4G4B4A4, D3DFMT_A4R4G4B4  , Texture, "X_D3DFMT_LIN_R4G4B4A4 -> D3DFMT_A4R4G4B4" },
	/* 0x3F X_D3DFMT_LIN_A8B8G8R8 */ { 32, Linear, A8B8G8R8, D3DFMT_A8B8G8R8  }, // Note : D3DFMT_A8B8G8R8=32 D3DFMT_Q8W8V8U8=63 // TODO : Needs testcase.
	/* 0x40 X_D3DFMT_LIN_B8G8R8A8 */ { 32, Linear, B8G8R8A8, D3DFMT_A8R8G8B8  , Texture, "X_D3DFMT_LIN_B8G8R8A8 -> D3DFMT_A8R8G8B8" },
	/* 0x41 X_D3DFMT_LIN_R8G8B8A8 */ { 32, Linear, R8G8B8A8, D3DFMT_A8R8G8B8  , Texture, "X_D3DFMT_LIN_R8G8B8A8 -> D3DFMT_A8R8G8B8" },
};

//todo move on or rename swizzle.cpp
BOOL FormatIsDepthBuffer(X_D3DFORMAT Format)
{
	if (Format <= X_D3DFMT_LIN_R8G8B8A8)
		return (FormatInfos[Format].usage == DepthBuffer);

	return false;
}
//same as above
BOOL FormatIsLinear(X_D3DFORMAT Format)
{
	if (Format <= X_D3DFMT_LIN_R8G8B8A8)
		return (FormatInfos[Format].stored == Linear);

	return (Format == X_D3DFMT_VERTEXDATA); // TODO : false;
}
//same as above
BOOL FormatIsRenderTarget(X_D3DFORMAT Format)
{
	if (Format <= X_D3DFMT_LIN_R8G8B8A8)
		return (FormatInfos[Format].usage == RenderTarget);

	return false;
}

BOOL FormatIsSwizzled(X_D3DFORMAT Format)
{
	if (Format <= X_D3DFMT_LIN_R8G8B8A8)
		return (FormatInfos[Format].stored == Swzzld);

	return false;
}



void UnswizzleBox
(
	const void* pSrcBuff,
	const uint32_t dwWidth,
	const uint32_t dwHeight,
	const uint32_t dwDepth,
	const uint32_t dwBytesPerPixel,
	const void* pDstBuff,
	const uint32_t dwDstRowPitch,
	const uint32_t dwDstSlicePitch
) // Source : Dxbx
{
	uint8_t* src_p8 = (uint8_t*)pSrcBuff;
	uint8_t* dst_p8 = (uint8_t*)pDstBuff;


	DWORD dwMaskX = 0, dwMaskY = 0, dwMaskZ = 0;
	for (unsigned int i = 1, j = 1; (i < dwWidth) || (i < dwHeight) || (i < dwDepth); i <<= 1) 
	{
		if (i < dwWidth) {
			dwMaskX |= j;
			j <<= 1;
		}

		if (i < dwHeight) {
			dwMaskY |= j;
			j <<= 1;
		}

		if (i < dwDepth) {
			dwMaskZ |= j;
			j <<= 1;
		}
	}

	const DWORD dwStartX = 0;
	const DWORD dwStartY = 0;
	const DWORD dwStartZ = 0;

	DWORD dwZ = dwStartZ;
	switch (dwBytesPerPixel) 
	{
	case 1: 
	{
		const uint8_t* pSrc = (uint8_t*)pSrcBuff;
		uint8_t* pDestSlice = (uint8_t*)pDstBuff;

		for (unsigned int z = 0; z < dwDepth; z++) 
		{
			uint8_t* pDestRow = pDestSlice;
			DWORD dwY = dwStartY;
			for (unsigned int y = 0; y < dwHeight; y++) {
				DWORD dwYZ = dwY | dwZ;
				DWORD dwX = dwStartX;
				for (unsigned int x = 0; x < dwWidth; x++) {
					unsigned int delta = dwX | dwYZ;
					pDestRow[x] = pSrc[delta]; // copy one pixel
					dwX = (dwX - dwMaskX) & dwMaskX; // step to next pixel in source
				}

				pDestRow += dwDstRowPitch; // / 1; // = / dwBPP; // step to next line in destination
				dwY = (dwY - dwMaskY) & dwMaskY; // step to next line in source
			}

			pDestSlice += dwDstSlicePitch; // / 1; // = / dwBPP; // step to next level in destination
			dwZ = (dwZ - dwMaskZ) & dwMaskZ; // step to next level in source
		}
		break;
	}
	case 2: 
	{
		const uint16_t* pSrc = (uint16_t*)pSrcBuff;
		uint16_t* pDestSlice = (uint16_t*)pDstBuff;

		for (unsigned int z = 0; z < dwDepth; z++) {
			uint16_t* pDestRow = pDestSlice;
			DWORD dwY = dwStartY;
			for (unsigned int y = 0; y < dwHeight; y++) {
				DWORD dwYZ = dwY | dwZ;
				DWORD dwX = dwStartX;
				for (unsigned int x = 0; x < dwWidth; x++) {
					unsigned int delta = dwX | dwYZ;
					pDestRow[x] = pSrc[delta]; // copy one pixel
					dwX = (dwX - dwMaskX) & dwMaskX; // step to next pixel in source
				}

				pDestRow += dwDstRowPitch / 2; // = dwBPP; // step to next line in destination
				dwY = (dwY - dwMaskY) & dwMaskY; // step to next line in source
			}

			pDestSlice += dwDstSlicePitch / 2; // = dwBPP; // step to next level in destination
			dwZ = (dwZ - dwMaskZ) & dwMaskZ; // step to next level in source
		}
		break;
	}
	case 4: 
	{
		const uint32_t* pSrc = (uint32_t*)pSrcBuff;
		uint32_t* pDestSlice = (uint32_t*)pDstBuff;

		for (unsigned int z = 0; z < dwDepth; z++) {
			uint32_t* pDestRow = pDestSlice;
			DWORD dwY = dwStartY;
			for (unsigned int y = 0; y < dwHeight; y++) {
				DWORD dwYZ = dwY | dwZ;
				DWORD dwX = dwStartX;
				for (unsigned int x = 0; x < dwWidth; x++) {
					unsigned int delta = dwX | dwYZ;
					pDestRow[x] = pSrc[delta]; // copy one pixel
					dwX = (dwX - dwMaskX) & dwMaskX; // step to next pixel in source
				}

				pDestRow += dwDstRowPitch / 4; // = dwBPP; // step to next line in destination
				dwY = (dwY - dwMaskY) & dwMaskY; // step to next line in source
			}

			pDestSlice += dwDstSlicePitch / 4; // = dwBPP; // step to next level in destination
			dwZ = (dwZ - dwMaskZ) & dwMaskZ; // step to next level in source
		}
		break;
	}
	}

	src_p8 = (uint8_t*)pSrcBuff;
	dst_p8 = (uint8_t*)pDstBuff;

} // EmuUnswizzleBox NOPATCH

typedef struct TRGB32
{
	unsigned char B;
	unsigned char G;
	unsigned char R;
	unsigned char A;
} TRGB32;

void ______P8ToARGBRow_C(const uint8_t* src_p8, uint8_t* dst_argb, int32_t width) 
{
	TRGB32* pTexturePalette = *(TRGB32**)dst_argb; // dirty hack to avoid another argument

	int x;
	for (x = 0; x < width; ++x) 
	{
		uint8_t p = src_p8[x];
		TRGB32 color = pTexturePalette[p];
		((TRGB32*)dst_argb)[x] = color;
	}
}


static const FormatToARGBRow ComponentConverters[] = {
	nullptr, // NoCmpnts,
	nullptr, // A1R5G5B5,
	nullptr, // X1R5G5B5, // Test : Convert X into 255
	nullptr, // A4R4G4B4,
	nullptr, // __R5G6B5, // NOTE : A=255
	nullptr, // A8R8G8B8,
	nullptr, // X8R8G8B8, // Test : Convert X into 255
	nullptr, // ____R8B8, // NOTE : A takes R, G takes B
	nullptr, // ____G8B8, // NOTE : A takes G, R takes B
	nullptr, // ______A8,
	nullptr, // __R6G5B5,
	nullptr, // R5G5B5A1,
	nullptr, // R4G4B4A4,
	nullptr, // A8B8G8R8,
	nullptr, // B8G8R8A8,
	nullptr, // R8G8B8A8,
	nullptr, // ______L8, // NOTE : A=255, R=G=B= L
	nullptr, // _____AL8, // NOTE : A=R=G=B= L
	nullptr, // _____L16, // NOTE : Actually G8B8, with A=R=255
	nullptr, // ____A8L8, // NOTE : R=G=B= L
	nullptr, // ____DXT1
	nullptr, // ____DXT3
	nullptr, // ____DXT5
	______P8ToARGBRow_C, // ______P8
	nullptr, // ____YUY2
	nullptr, // ____UYVY
};

const FormatToARGBRow XBFormatComponentConverter(X_D3DFORMAT Format)
{
	if (Format <= X_D3DFMT_LIN_R8G8B8A8)
	{
		if (FormatInfos[Format].components != NoCmpnts)
		{
			auto res = ComponentConverters[FormatInfos[Format].components];
			if (res == nullptr)
			{
				LOGN_WARNING("XBFormatComponentConverter: Missing function conversion for texture!");
			}
			return  res;
		}

	}

	return nullptr;
}


DWORD XBFormatBitsPerPixel(X_D3DFORMAT Format)
{
	if (Format <= X_D3DFMT_LIN_R8G8B8A8)
		if (FormatInfos[Format].bits_per_pixel > 0)
			return FormatInfos[Format].bits_per_pixel;

	return 16;
}


DWORD XBFormatBytesPerPixel(X_D3DFORMAT Format)
{
	return ((XBFormatBitsPerPixel(Format) + 4) / 8);
}

BOOL XBFormatIsCompressed(X_D3DFORMAT Format)
{
	if (Format <= X_D3DFMT_LIN_R8G8B8A8)
		return (FormatInfos[Format].stored == Cmprsd);

	return false;
}


// Is there a converter available from the supplied format to ARGB?
bool XBFormatCanBeConvertedToARGB(X_D3DFORMAT Format)
{
	const FormatToARGBRow info = XBFormatComponentConverter(Format);
	return (info != nullptr);
}


// Returns if convertion to ARGB is required. This is the case when
// the format has a warning message and there's a converter present.
bool XBFormatRequiresConversionToARGB(X_D3DFORMAT Format)
{
	if (FormatInfos[Format].warning != nullptr)
	{
		if (XBFormatCanBeConvertedToARGB(Format))
			return true;
	}

	return false;
}

bool ConvertD3DTextureToARGBBuffer(
	X_D3DFORMAT X_Format,
	uint8_t* pSrc,
	int32_t SrcWidth, int32_t SrcHeight, int32_t SrcRowPitch, int32_t SrcSlicePitch,
	uint8_t* pDst, int32_t DstRowPitch, int32_t DstSlicePitch,
	uint32_t uiDepth,
	void* pPalleteData
)
{

	const FormatToARGBRow ConvertRowToARGB = XBFormatComponentConverter(X_Format);

	if (ConvertRowToARGB == nullptr)
		return false; // Unhandled conversion

	uint8_t* unswizleBuffer = nullptr;
	if (FormatIsSwizzled(X_Format))
	{
		unswizleBuffer = (uint8_t*)g_userHeap.AllocPhysical(SrcSlicePitch * uiDepth, 1);
		// First we need to unswizzle the texture data
		UnswizzleBox(
			pSrc, SrcWidth, SrcHeight, uiDepth,
			XBFormatBytesPerPixel(X_Format),
			// Note : use src pitch on dest, because this is an intermediate step :
			unswizleBuffer, SrcRowPitch, SrcSlicePitch
		);
		// Convert colors from the unswizzled buffer
		pSrc = unswizleBuffer;
	}

	intptr_t AdditionalArgument;
	if (X_Format == X_D3DFMT_P8)
		AdditionalArgument = (intptr_t)pPalleteData;
	else
		AdditionalArgument = DstRowPitch;

	if (XBFormatIsCompressed(X_Format))
	{
		if (SrcWidth < 4 || SrcHeight < 4) 
		{
			// HACK: The compressed DXT conversion code currently writes more pixels than it should, which can cause a crash.
			// This code will get hit when converting compressed texture mipmaps on hardware that somehow doesn't support DXT natively
			LOGN_WARNING("Converting DXT textures smaller than a block is not currently implemented. Ignoring conversion!");
			return true;
		}

		// All compressed formats (DXT1, DXT3 and DXT5) encode blocks of 4 pixels on 4 lines
		SrcHeight = (SrcHeight + 3) / 4;
		DstRowPitch *= 4;
	}

	uint8_t* pSrcSlice = pSrc;
	uint8_t* pDstSlice = pDst;
	for (uint32_t z = 0; z < uiDepth; z++) 
	{
		uint8_t* pSrcRow = pSrcSlice;
		uint8_t* pDstRow = pDstSlice;
		for (int32_t y = 0; y < SrcHeight; y++) 
		{

			*(intptr_t*)pDstRow =  AdditionalArgument; // Dirty hack, to avoid an extra parameter to all conversion callbacks

			ConvertRowToARGB(pSrcRow, pDstRow, SrcWidth);
			pSrcRow += SrcRowPitch;
			pDstRow += DstRowPitch;
		}

		pSrcSlice += SrcSlicePitch;
		pDstSlice += DstSlicePitch;
	}

	if (unswizleBuffer)
		g_userHeap.Free(unswizleBuffer);

	return true;
}
