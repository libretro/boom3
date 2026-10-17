/*
===========================================================================

The Vulkan renderer's engine side: the backend commands, the passes, the
images and the vertex cache's memory, described to vk/vk_backend.h in its
terms.  The passes keep the ARB2 path's semantics as the GLSL renderers
do; the device side is all in C under vk/.

===========================================================================
*/

#include "sys/platform.h"
#include "renderer/VertexCache.h"
#include "renderer/Cinematic.h"

#include "tr_local.h"
#include "vk/vk_backend.h"

extern bool  hdr_output_active;
extern float hdr_emissive_gain;
extern float hdr_particle_gain;
extern bool  HDR_GuiCorrectionActive( unsigned int *tex );

/* the view's block for the draws that follow */
static vkb_view_block_t rbVkView;
static bool rbVkViewSet;
static bool rbVkDepthCopied;    /* this view's depth is in _currentDepth */
extern float hdr_scene_encode_scale;

static void RB_VK_DrawStageProgram( const drawSurf_t *surf, const shaderStage_t *pStage, int programId,
	idImage *const *images, int numImages );
static void RB_VK_DrawSoftParticle( const drawSurf_t *surf, const shaderStage_t *pStage, const float color[4] );

/*
====================
RB_VK_Format

The image the engine built, as the device takes it.
====================
*/
static enum vkb_filter RB_VK_Filter( textureFilter_t filter ) {
	if ( filter == TF_NEAREST ) {
		return VKB_FILTER_NEAREST;
	}
	if ( filter == TF_LINEAR ) {
		return VKB_FILTER_LINEAR_NOMIP;
	}
	/* the default filter follows image_filter */
	switch ( globalImages->textureMinFilter ) {
	case GL_NEAREST:
	case GL_NEAREST_MIPMAP_NEAREST:
	case GL_NEAREST_MIPMAP_LINEAR:
		return VKB_FILTER_NEAREST;
	case GL_LINEAR:
		return VKB_FILTER_LINEAR_NOMIP;
	case GL_LINEAR_MIPMAP_NEAREST:
		return VKB_FILTER_LINEAR_MIP_NEAREST;
	default:
		return VKB_FILTER_LINEAR;
	}
}


static enum vkb_wrap RB_VK_Wrap( textureRepeat_t repeat ) {
	switch ( repeat ) {
	case TR_CLAMP:
	case TR_CLAMP_TO_BORDER:
		return VKB_WRAP_CLAMP;
	case TR_CLAMP_TO_ZERO:
		return VKB_WRAP_CLAMP_TO_ZERO;
	case TR_CLAMP_TO_ZERO_ALPHA:
		return VKB_WRAP_CLAMP_TO_ZERO_ALPHA;
	default:
		return VKB_WRAP_REPEAT;
	}
}

static int RB_VK_Anisotropy( const idImage *image ) {
	return image->filter == TF_DEFAULT && globalImages->textureAnisotropy > 1.0f;
}

/* image_filter or image_anisotropy changed: every loaded image's sampler */
void R_VK_ChangeTextureFilter( idImage *image ) {
	if ( image->texnum != idImage::TEXTURE_NOT_LOADED ) {
		vkb_image_filter( image->texnum, RB_VK_Filter( image->filter ), RB_VK_Wrap( image->repeat ),
			RB_VK_Anisotropy( image ) );
	}
}

static int RB_VK_MipLevels( int width, int height ) {
	int levels = 1;
	while ( width > 1 || height > 1 ) {
		width >>= 1;
		height >>= 1;
		levels++;
	}
	return levels;
}

/*
====================
R_VK_GenerateImage

An RGBA image at its upload size with its mip chain, made here as the GL
paths make theirs.
====================
*/
bool R_VK_GenerateImage( idImage *image, const byte *rgba, int width, int height, bool preserveBorder ) {
	const int levels = RB_VK_MipLevels( width, height );
	unsigned id;
	byte *level, *shrunk;
	int w = width, h = height, i;

	if ( !vkb_ready() ) {
		return false;
	}
	id = vkb_image_create( (unsigned)width, (unsigned)height, levels, 0, VKB_FORMAT_RGBA8,
		RB_VK_Filter( image->filter ), RB_VK_Wrap( image->repeat ), RB_VK_Anisotropy( image ) );
	if ( !id ) {
		return false;
	}
	vkb_image_upload( id, 0, 0, rgba, (size_t)width * height * 4 );
	level = NULL;
	for ( i = 1; i < levels; i++ ) {
		shrunk = R_MipMap( level ? level : rgba, w, h, preserveBorder );
		if ( level ) {
			R_StaticFree( level );
		}
		level = shrunk;
		w >>= 1;
		h >>= 1;
		if ( w < 1 ) w = 1;
		if ( h < 1 ) h = 1;
		vkb_image_upload( id, i, 0, level, (size_t)w * h * 4 );
	}
	if ( level ) {
		R_StaticFree( level );
	}
	image->texnum = id;
	image->uploadWidth = width;
	image->uploadHeight = height;
	image->type = TT_2D;
	return true;
}

/*
====================
R_VK_GenerateCubeImage
====================
*/
bool R_VK_GenerateCubeImage( idImage *image, const byte *pic[6], int size ) {
	const int levels = RB_VK_MipLevels( size, size );
	unsigned id;
	int i, face;

	if ( !vkb_ready() ) {
		return false;
	}
	id = vkb_image_create( (unsigned)size, (unsigned)size, levels, 1, VKB_FORMAT_RGBA8,
		RB_VK_Filter( image->filter ), VKB_WRAP_CLAMP, RB_VK_Anisotropy( image ) );
	if ( !id ) {
		return false;
	}
	for ( face = 0; face < 6; face++ ) {
		byte *level = NULL;
		int w = size;
		vkb_image_upload( id, 0, face, pic[face], (size_t)size * size * 4 );
		for ( i = 1; i < levels; i++ ) {
			byte *shrunk = R_MipMap( level ? level : pic[face], w, w, false );
			if ( level ) {
				R_StaticFree( level );
			}
			level = shrunk;
			w >>= 1;
			if ( w < 1 ) w = 1;
			vkb_image_upload( id, i, face, level, (size_t)w * w * 4 );
		}
		if ( level ) {
			R_StaticFree( level );
		}
	}
	image->texnum = id;
	image->uploadWidth = image->uploadHeight = size;
	image->type = TT_CUBIC;
	return true;
}

/*
====================
R_VK_GenerateImageLevels

A precompressed image's levels as they are in the file: the format is
the device's, the data the file's.
====================
*/
bool R_VK_GenerateImageLevels( idImage *image, int format, int width, int height,
							   const byte *const *levels, const int *levelBytes, int numLevels ) {
	unsigned id;
	int i;
	if ( !vkb_ready() || numLevels < 1 ) {
		return false;
	}
	if ( ( format == VKB_FORMAT_BC1 || format == VKB_FORMAT_BC2 || format == VKB_FORMAT_BC3
			|| format == VKB_FORMAT_BC7 ) && !vkb_has_bc() ) {
		return false;
	}
	id = vkb_image_create( (unsigned)width, (unsigned)height, numLevels, 0, (enum vkb_format)format,
		RB_VK_Filter( image->filter ), RB_VK_Wrap( image->repeat ), RB_VK_Anisotropy( image ) );
	if ( !id ) {
		return false;
	}
	for ( i = 0; i < numLevels; i++ ) {
		vkb_image_upload( id, i, 0, levels[i], (size_t)levelBytes[i] );
	}
	image->texnum = id;
	image->uploadWidth = width;
	image->uploadHeight = height;
	image->type = TT_2D;
	return true;
}

/*
====================
R_VK_UploadPrecompressed

A DDS's levels as they are in the file: BC1/2/3/7 blocks, or 32-bit
texels, straight into the device format that holds them.  24-bit and
8-bit files take the one repack their formats need.
====================
*/
void R_VK_UploadPrecompressed( idImage *image, const ddsFileHeader_t *header, const byte *data ) {
	const byte *levels[16];
	int levelBytes[16];
	byte *packed = NULL;
	int format = -1, blockBytes = 0, texelBytes = 0;
	int numMipmaps = ( header->dwFlags & DDSF_MIPMAPCOUNT ) ? (int)header->dwMipMapCount : 1;
	int uw = (int)header->dwWidth, uh = (int)header->dwHeight;
	const byte *imagedata = data + sizeof( ddsFileHeader_t ) + 4;
	int i;

	if ( header->ddspf.dwFlags & DDSF_FOURCC ) {
		switch ( header->ddspf.dwFourCC ) {
		case DDS_MAKEFOURCC( 'D', 'X', 'T', '1' ):
			format = VKB_FORMAT_BC1;
			blockBytes = 8;
			break;
		case DDS_MAKEFOURCC( 'D', 'X', 'T', '3' ):
			format = VKB_FORMAT_BC2;
			blockBytes = 16;
			break;
		case DDS_MAKEFOURCC( 'D', 'X', 'T', '5' ):
		case DDS_MAKEFOURCC( 'R', 'X', 'G', 'B' ):
			format = VKB_FORMAT_BC3;
			blockBytes = 16;
			break;
		case DDS_MAKEFOURCC( 'B', 'C', '7', '0' ):
		case DDS_MAKEFOURCC( 'B', 'C', '7', 'L' ):
			format = VKB_FORMAT_BC7;
			blockBytes = 16;
			break;
		case DDS_MAKEFOURCC( 'D', 'X', '1', '0' ):
			format = VKB_FORMAT_BC7;
			blockBytes = 16;
			imagedata += 20;
			break;
		default:
			common->Warning( "Invalid compressed internal format\n" );
			return;
		}
	} else if ( ( header->ddspf.dwFlags & DDSF_RGB ) && header->ddspf.dwRGBBitCount == 32 ) {
		format = VKB_FORMAT_BGRA8;
		texelBytes = 4;
	} else if ( ( header->ddspf.dwFlags & DDSF_RGB ) && header->ddspf.dwRGBBitCount == 24 ) {
		format = VKB_FORMAT_BGRA8;
		texelBytes = 3;
	} else if ( header->ddspf.dwRGBBitCount == 8 ) {
		format = VKB_FORMAT_A8;
		texelBytes = 1;
	} else {
		common->Warning( "Invalid uncompressed internal format\n" );
		return;
	}
	if ( numMipmaps > 16 ) {
		numMipmaps = 16;
	}
	if ( texelBytes == 3 ) {
		/* 24-bit BGR to BGRA, every level */
		int total = 0, w = uw, h = uh;
		byte *dst;
		for ( i = 0; i < numMipmaps; i++ ) {
			total += w * h * 4;
			w = w > 1 ? w >> 1 : 1;
			h = h > 1 ? h >> 1 : 1;
		}
		packed = (byte *)R_StaticAlloc( total );
		dst = packed;
		w = uw;
		h = uh;
		for ( i = 0; i < numMipmaps; i++ ) {
			const int n = w * h;
			int t;
			levels[i] = dst;
			levelBytes[i] = n * 4;
			for ( t = 0; t < n; t++ ) {
				dst[t * 4 + 0] = imagedata[t * 3 + 0];
				dst[t * 4 + 1] = imagedata[t * 3 + 1];
				dst[t * 4 + 2] = imagedata[t * 3 + 2];
				dst[t * 4 + 3] = 255;
			}
			imagedata += n * 3;
			dst += n * 4;
			w = w > 1 ? w >> 1 : 1;
			h = h > 1 ? h >> 1 : 1;
		}
	} else {
		int w = uw, h = uh;
		for ( i = 0; i < numMipmaps; i++ ) {
			const int size = blockBytes ? ( ( w + 3 ) / 4 ) * ( ( h + 3 ) / 4 ) * blockBytes : w * h * texelBytes;
			levels[i] = imagedata;
			levelBytes[i] = size;
			imagedata += size;
			w = w > 1 ? w >> 1 : 1;
			h = h > 1 ? h >> 1 : 1;
		}
	}
	if ( numMipmaps == 1 && image->filter == TF_DEFAULT ) {
		image->filter = TF_LINEAR;
	}
	if ( !R_VK_GenerateImageLevels( image, format, uw, uh, levels, levelBytes, numMipmaps ) ) {
		common->Warning( "%s: the device takes no %s", image->imgName.c_str(), blockBytes ? "block compressed texture" : "texture" );
	}
	if ( packed ) {
		R_StaticFree( packed );
	}
}

/*
====================
R_VK_GenerateRampImage16
====================
*/
bool R_VK_GenerateRampImage16( idImage *image, const unsigned short *pic, int width, int height, bool luminanceAlpha ) {
	unsigned id;
	if ( !vkb_ready() ) {
		return false;
	}
	id = vkb_image_create( (unsigned)width, (unsigned)height, 1, 0,
		luminanceAlpha ? VKB_FORMAT_LA16 : VKB_FORMAT_I16,
		RB_VK_Filter( image->filter ), RB_VK_Wrap( image->repeat ), 0 );
	if ( !id ) {
		return false;
	}
	vkb_image_upload( id, 0, 0, pic, (size_t)width * height * ( luminanceAlpha ? 4 : 2 ) );
	image->texnum = id;
	image->uploadWidth = width;
	image->uploadHeight = height;
	image->type = TT_2D;
	return true;
}

/*
====================
R_VK_UploadScratch

A cinematic's frame, or the scratch image: the texture is kept across
frames of the same size.
====================
*/
void R_VK_UploadScratch( idImage *image, const byte *data, int cols, int rows ) {
	if ( !vkb_ready() ) {
		return;
	}
	if ( image->texnum == idImage::TEXTURE_NOT_LOADED || image->uploadWidth != cols || image->uploadHeight != rows
			|| image->type != TT_2D ) {
		if ( image->texnum != idImage::TEXTURE_NOT_LOADED ) {
			vkb_image_destroy( image->texnum );
		}
		image->texnum = vkb_image_create( (unsigned)cols, (unsigned)rows, 1, 0, VKB_FORMAT_RGBA8,
			VKB_FILTER_LINEAR_NOMIP, VKB_WRAP_CLAMP, 0 );
		if ( !image->texnum ) {
			image->texnum = idImage::TEXTURE_NOT_LOADED;
			return;
		}
		image->uploadWidth = cols;
		image->uploadHeight = rows;
		image->type = TT_2D;
	}
	vkb_image_upload( image->texnum, 0, 0, data, (size_t)cols * rows * 4 );
}

/*
====================
R_VK_PurgeImage
====================
*/
void R_VK_PurgeImage( idImage *image ) {
	if ( image->texnum != idImage::TEXTURE_NOT_LOADED ) {
		vkb_image_destroy( image->texnum );
		image->texnum = idImage::TEXTURE_NOT_LOADED;
	}
}

/*
====================
R_VK_CopyFramebuffer / R_VK_CopyDepthbuffer

The scene into a target image of the engine's: made at the scene's size
on first use.
====================
*/
static void RB_VK_EnsureTarget( idImage *image, int width, int height, enum vkb_format format ) {
	if ( image->texnum != idImage::TEXTURE_NOT_LOADED
			&& ( image->uploadWidth < width || image->uploadHeight < height || image->internalFormat != (int)format ) ) {
		vkb_image_destroy( image->texnum );
		image->texnum = idImage::TEXTURE_NOT_LOADED;
	}
	if ( image->texnum == idImage::TEXTURE_NOT_LOADED ) {
		image->texnum = vkb_image_create_target( (unsigned)width, (unsigned)height, format );
		if ( !image->texnum ) {
			image->texnum = idImage::TEXTURE_NOT_LOADED;
			return;
		}
		image->uploadWidth = width;
		image->uploadHeight = height;
		image->internalFormat = (int)format;
		image->type = TT_2D;
	}
}

enum vkb_format RB_VK_SceneFormat( void );

void R_VK_CopyFramebuffer( idImage *image, int x, int y, int width, int height ) {
	if ( !vkb_ready() ) {
		return;
	}
	RB_VK_EnsureTarget( image, width, height, RB_VK_SceneFormat() );
	if ( image->texnum != idImage::TEXTURE_NOT_LOADED ) {
		vkb_copy_color( image->texnum, x, y, width, height );
	}
}

void R_VK_CopyDepthbuffer( idImage *image, int x, int y, int width, int height ) {
	if ( !vkb_ready() ) {
		return;
	}
	RB_VK_EnsureTarget( image, width, height, VKB_FORMAT_DEPTH );
	if ( image->texnum != idImage::TEXTURE_NOT_LOADED ) {
		vkb_copy_depth( image->texnum, x, y, width, height );
	}
}

/*
====================
RB_VK_Image

An image's device id, loading it on demand as Bind does.
====================
*/
static unsigned RB_VK_Image( idImage *image ) {
	if ( !image ) {
		return 0;
	}
	if ( image->texnum == idImage::TEXTURE_NOT_LOADED ) {
		image->Bind();
	}
	if ( image->texnum == idImage::TEXTURE_NOT_LOADED ) {
		return 0;
	}
	return image->texnum;
}

/*
====================
RB_VK_StageImage

The image a stage samples: a cinematic's current frame, or its own.
====================
*/
static unsigned RB_VK_StageImage( const textureStage_t *texture ) {
	if ( texture->cinematic ) {
		cinData_t cin;
		if ( r_skipDynamicTextures.GetBool() ) {
			return RB_VK_Image( globalImages->defaultImage );
		}
		cin = texture->cinematic->ImageForTime( (int)( 1000 * ( backEnd.viewDef->floatTime
			+ backEnd.viewDef->renderView.shaderParms[11] ) ) );
		if ( cin.image ) {
			R_VK_UploadScratch( globalImages->cinematicImage, cin.image, cin.imageWidth, cin.imageHeight );
			return RB_VK_Image( globalImages->cinematicImage );
		}
		return RB_VK_Image( globalImages->blackImage );
	}
	return RB_VK_Image( texture->image );
}

/*
====================
RB_VK_SetView

The view block: projection times the view matrix, the engine's clip
space, converted by the shaders.
====================
*/
static void RB_VK_SetView( void ) {
	const viewDef_t *viewDef = backEnd.viewDef;
	myGlMultMatrix( viewDef->worldSpace.modelViewMatrix, viewDef->projectionMatrix, rbVkView.viewProjection );
	memcpy( rbVkView.projection, viewDef->projectionMatrix, sizeof( rbVkView.projection ) );
	memcpy( rbVkView.view, viewDef->worldSpace.modelViewMatrix, sizeof( rbVkView.view ) );
	rbVkView.viewport[0] = (float)viewDef->viewport.x1;
	rbVkView.viewport[1] = (float)viewDef->viewport.y1;
	rbVkView.viewport[2] = 1.0f / (float)( viewDef->viewport.x2 - viewDef->viewport.x1 + 1 );
	rbVkView.viewport[3] = 1.0f / (float)( viewDef->viewport.y2 - viewDef->viewport.y1 + 1 );
	vkb_set_view( &rbVkView );
	rbVkViewSet = true;
}

/*
====================
RB_VK_DepthHack

The weapon and model depth hacks, added to clip z per unit of eye w.
====================
*/
static float RB_VK_DepthHack( const viewEntity_t *space ) {
	const float p14 = backEnd.viewDef->projectionMatrix[14];
	if ( space->modelDepthHack != 0.0f ) {
		return -space->modelDepthHack;
	}
	if ( space->weaponDepthHack ) {
		return p14 * 0.25f - p14;
	}
	return 0.0f;
}

/*
====================
RB_VK_Scissor

The surface's scissor, as the GL backends set it.
====================
*/
static void RB_VK_ScissorRect( const idScreenRect &rect ) {
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( rect ) ) {
		backEnd.currentScissor = rect;
		if ( ( backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1 ) < 0.0f
				|| ( backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 ) < 0.0f ) {
			backEnd.currentScissor = backEnd.viewDef->scissor;
		}
		vkb_scissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}
}

static void RB_VK_Scissor( const drawSurf_t *surf ) {
	RB_VK_ScissorRect( surf->scissorRect );
}

/* the engine's polygon offset, as the depth bias takes it */
static void RB_VK_PolygonOffset( vkb_draw_t *draw, float offset ) {
	draw->depthBias[0] = r_offsetUnits.GetFloat() * offset;
	draw->depthBias[1] = r_offsetFactor.GetFloat();
}

/*
====================
RB_VK_Geometry

A surface's vertex and index blocks for a draw; false when either is not
in device memory.
====================
*/
/* The indexes of a surface without an index cache - the GUI model's and
 * other per-frame geometry - go into the ring once per frame, shortened
 * when the vertex count allows. */
static struct {
	const srfTriangles_t *tri;
	unsigned buffer, offset;
	bool shortIndexes;
} vkFrameIndexes;

static bool RB_VK_FrameIndexes( const srfTriangles_t *tri, vkb_draw_t *draw ) {
	if ( vkFrameIndexes.tri != tri ) {
		const bool shortIndexes = tri->numVerts <= 0xffff;
		const size_t bytes = (size_t)tri->numIndexes * ( shortIndexes ? 2 : 4 );
		void *p = vkb_ring_alloc( bytes, 4, &vkFrameIndexes.buffer, &vkFrameIndexes.offset );
		if ( !p ) {
			return false;
		}
		if ( shortIndexes ) {
			unsigned short *dst = (unsigned short *)p;
			for ( int i = 0; i < tri->numIndexes; i++ ) {
				dst[i] = (unsigned short)tri->indexes[i];
			}
		} else {
			memcpy( p, tri->indexes, bytes );
		}
		vkFrameIndexes.tri = tri;
		vkFrameIndexes.shortIndexes = shortIndexes;
	}
	draw->indexBuffer = vkFrameIndexes.buffer;
	draw->indexOffset = vkFrameIndexes.offset;
	draw->shortIndexes = vkFrameIndexes.shortIndexes;
	return true;
}

static bool RB_VK_Geometry( const srfTriangles_t *tri, vkb_draw_t *draw ) {
	const vertCache_t *vc = tri->ambientCache;
	const vertCache_t *ic = tri->indexCache;
	if ( !vc || vc->virtMem || ( ic && ic->virtMem ) ) {
		return false;
	}
	draw->vertexBuffer = vc->vbo;
	draw->vertexOffset = (unsigned)vc->offset;
	draw->indexCount = (unsigned)tri->numIndexes;
	draw->layout = VKB_VERTEX_DRAWVERT;
	if ( ic ) {
		draw->indexBuffer = ic->vbo;
		draw->indexOffset = (unsigned)ic->offset;
		draw->shortIndexes = ic->indexType == GL_UNSIGNED_SHORT;
		return true;
	}
	return RB_VK_FrameIndexes( tri, draw );
}

/*
====================
RB_VK_DrawBegin

The parts of a draw every pass sets the same way.
====================
*/
static void RB_VK_DrawBegin( vkb_draw_t *draw, vkb_draw_block_t *block, const drawSurf_t *surf ) {
	memset( draw, 0, sizeof( *draw ) );
	memset( block, 0, sizeof( *block ) );
	draw->block = block;
	memcpy( block->modelMatrix, surf->space->modelMatrix, sizeof( block->modelMatrix ) );
	memcpy( block->modelViewMatrix, surf->space->modelViewMatrix, sizeof( block->modelViewMatrix ) );
	memcpy( block->textureMatrix, mat4_identity.ToFloatPtr(), sizeof( block->textureMatrix ) );
	block->colorModulateAdd[0] = 0.0f;
	block->colorModulateAdd[1] = 1.0f;
	block->colorModulateAdd[2] = RB_VK_DepthHack( surf->space );
	block->colorModulateAdd[3] = 1.0f;
	block->glColor[0] = block->glColor[1] = block->glColor[2] = block->glColor[3] = 1.0f;
	block->hdrParms[0] = 1.0f;
	block->hdrParms[3] = 1.0f;
	draw->mirror = backEnd.viewDef->isMirror;
	draw->cull = VKB_CULL_NONE;
	draw->stencil = VKB_STENCIL_OFF;
}

static unsigned char RB_VK_Cull( cullType_t cullType ) {
	if ( backEnd.viewDef->isMirror ) {
		return cullType == CT_FRONT_SIDED ? VKB_CULL_FRONT : cullType == CT_BACK_SIDED ? VKB_CULL_BACK : VKB_CULL_NONE;
	}
	return cullType == CT_FRONT_SIDED ? VKB_CULL_BACK : cullType == CT_BACK_SIDED ? VKB_CULL_FRONT : VKB_CULL_NONE;
}

/*
====================
RB_VK_WeaponDepthRange

The weapon's halved depth range around a surface.
====================
*/
static bool RB_VK_WeaponDepthRange( const drawSurf_t *surf ) {
	if ( surf->space->weaponDepthHack && surf->space->modelDepthHack == 0.0f ) {
		vkb_depth_range( 0.0f, 0.5f );
		return true;
	}
	return false;
}

/*
=====================
RB_VK_FillDepthBuffer

The depth fill, as RB_T_GLSL_FillDepthBuffer: black colour, solid for
opaque surfaces, alpha tested per stage for perforated ones, the clip
notch for subviews.
=====================
*/
static void RB_VK_T_FillDepthBuffer( const drawSurf_t *surf ) {
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	vkb_draw_t draw;
	vkb_draw_block_t block;
	int stage;
	bool drawSolid = false;
	bool depthRange;

	if ( !shader->IsDrawn() || !surf->geo->numIndexes || shader->Coverage() == MC_TRANSLUCENT
			|| !surf->geo->ambientCache ) {
		return;
	}
	for ( stage = 0; stage < shader->GetNumStages(); stage++ ) {
		if ( regs[shader->GetStage( stage )->conditionRegister] != 0 ) {
			break;
		}
	}
	if ( stage == shader->GetNumStages() ) {
		return;
	}

	RB_VK_DrawBegin( &draw, &block, surf );
	if ( !RB_VK_Geometry( surf->geo, &draw ) ) {
		return;
	}
	RB_VK_Scissor( surf );
	depthRange = RB_VK_WeaponDepthRange( surf );
	draw.cull = RB_VK_Cull( shader->GetCullType() );
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		RB_VK_PolygonOffset( &draw, shader->GetPolygonOffset() );
	}
	draw.stateBits = GLS_DEPTHFUNC_LESS;
	block.glColor[0] = block.glColor[1] = block.glColor[2] = 0.0f;
	if ( shader->GetSort() == SS_SUBVIEW ) {
		draw.stateBits = GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS;
		block.glColor[0] = block.glColor[1] = block.glColor[2] = 1.0f;
	}
	if ( backEnd.viewDef->numClipPlanes ) {
		idPlane plane;
		R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], plane );
		plane[3] += 0.5f;
		memcpy( block.clipPlane, plane.ToFloatPtr(), sizeof( block.clipPlane ) );
		draw.image[1] = RB_VK_Image( globalImages->alphaNotchImage );
	}

	if ( shader->Coverage() == MC_OPAQUE ) {
		drawSolid = true;
	} else if ( shader->Coverage() == MC_PERFORATED ) {
		bool didDraw = false;
		for ( stage = 0; stage < shader->GetNumStages(); stage++ ) {
			const shaderStage_t *pStage = shader->GetStage( stage );
			if ( !pStage->hasAlphaTest || regs[pStage->conditionRegister] == 0 ) {
				continue;
			}
			didDraw = true;
			block.glColor[3] = regs[pStage->color.registers[3]];
			if ( block.glColor[3] <= 0 ) {
				continue;
			}
			block.colorModulateAdd[3] = regs[pStage->alphaTestRegister];
			draw.image[0] = RB_VK_StageImage( &pStage->texture );
			if ( pStage->texture.hasMatrix ) {
				RB_GetShaderTextureMatrix( regs, &pStage->texture, block.textureMatrix );
			} else {
				memcpy( block.textureMatrix, mat4_identity.ToFloatPtr(), sizeof( block.textureMatrix ) );
			}
			draw.program = backEnd.viewDef->numClipPlanes ? VKB_PROGRAM_ZFILL_CLIP : VKB_PROGRAM_ZFILL;
			vkb_draw( &draw );
		}
		block.glColor[3] = 1.0f;
		block.colorModulateAdd[3] = 1.0f;
		if ( !didDraw ) {
			drawSolid = true;
		}
	}
	if ( drawSolid ) {
		draw.image[0] = 0;
		draw.program = backEnd.viewDef->numClipPlanes ? VKB_PROGRAM_ZFILL_CLIP : VKB_PROGRAM_ZFILL_OPAQUE;
		if ( backEnd.viewDef->numClipPlanes ) {
			draw.image[0] = RB_VK_Image( globalImages->whiteImage );
			block.colorModulateAdd[3] = 0.5f;
		}
		vkb_draw( &draw );
	}
	if ( depthRange ) {
		vkb_depth_range( 0.0f, 1.0f );
	}
}

static void RB_VK_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int i;
	rbVkDepthCopied = false;
	if ( !backEnd.viewDef->viewEntitys ) {
		return;
	}
	/* the view's depth and stencil cleared, the stencil to the midpoint
	 * the shadow volumes count around, as RB_BeginDrawingView does */
	vkb_clear( 0, 1, 1, NULL, 1u << ( glConfig.stencilBits - 1 ) );
	for ( i = 0; i < numDrawSurfs; i++ ) {
		if ( drawSurfs[i]->material->SuppressInSubview() ) {
			continue;
		}
		if ( backEnd.viewDef->isXraySubview && drawSurfs[i]->space->entityDef ) {
			if ( drawSurfs[i]->space->entityDef->parms.xrayIndex != 2 ) {
				continue;
			}
		}
		RB_VK_T_FillDepthBuffer( drawSurfs[i] );
	}
	/* the early depth, for the stages that read it (#3877) */
	rbVkDepthCopied = RB_DepthCaptureWanted();
	if ( rbVkDepthCopied ) {
		R_VK_CopyDepthbuffer( globalImages->currentDepthImage, backEnd.viewDef->viewport.x1,
			backEnd.viewDef->viewport.y1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
			backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
	}
}

/*
=====================
RB_VK_T_RenderShaderPasses

The old-style stages of a surface and the GUI: a map with explicit
texcoords, a cube map by skybox, reflection or normal.
=====================
*/
static void RB_VK_T_RenderShaderPasses( const drawSurf_t *surf ) {
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	unsigned guiLut = 0;
	int stage;
	bool depthRange, softParticle;

	if ( !shader->HasAmbient() || shader->IsPortalSky() || !surf->geo->numIndexes || !surf->geo->ambientCache ) {
		return;
	}
	if ( shader->GetSort() == SS_GUI ) {
		if ( !HDR_GuiCorrectionActive( &guiLut ) ) {
			guiLut = 0;
		}
	}
	RB_VK_Scissor( surf );
	depthRange = RB_VK_WeaponDepthRange( surf );
	/* softened particles: flagged by the front end, with the view's depth captured (#3878) */
	softParticle = ( surf->dsFlags & DSF_SOFT_PARTICLE ) != 0 && surf->particle_radius > 0.0f
		&& rbVkDepthCopied && !r_skipNewAmbient.GetBool();

	for ( stage = 0; stage < shader->GetNumStages(); stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		vkb_draw_t draw;
		vkb_draw_block_t block;
		float color[4];
		float hp[4];

		if ( regs[pStage->conditionRegister] == 0 || pStage->lighting != SL_AMBIENT ) {
			continue;
		}
		if ( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
			continue;
		}
		if ( pStage->newStage ) {
			const newShaderStage_t *newStage = pStage->newStage;
			if ( r_skipNewAmbient.GetBool() || newStage->glslProgram == GLSL_STAGE_NONE ) {
				continue;
			}
			RB_VK_DrawStageProgram( surf, pStage, newStage->glslProgram, newStage->fragmentProgramImages,
				newStage->numFragmentProgramImages );
			continue;
		}
		color[0] = regs[pStage->color.registers[0]];
		color[1] = regs[pStage->color.registers[1]];
		color[2] = regs[pStage->color.registers[2]];
		color[3] = regs[pStage->color.registers[3]];
		if ( softParticle && ( ( pStage->drawStateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_ONE
				|| ( pStage->drawStateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_SRC_ALPHA ) ) {
			const deform_t dfm = shader->Deform();
			const float gain = ( dfm == DFRM_PARTICLE || dfm == DFRM_PARTICLE2 ) ? hdr_particle_gain : hdr_emissive_gain;
			if ( hdr_output_active && gain != 1.0f && ( pStage->drawStateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE ) {
				color[0] *= gain;
				color[1] *= gain;
				color[2] *= gain;
			}
			RB_VK_DrawSoftParticle( surf, pStage, color );
			continue;
		}
		if ( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE )
				&& color[0] <= 0 && color[1] <= 0 && color[2] <= 0 ) {
			continue;
		}
		if ( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
				&& color[3] <= 0 ) {
			continue;
		}

		RB_VK_DrawBegin( &draw, &block, surf );
		if ( !RB_VK_Geometry( surf->geo, &draw ) ) {
			break;
		}
		draw.cull = RB_VK_Cull( shader->GetCullType() );
		draw.stateBits = (unsigned)pStage->drawStateBits;
		if ( !backEnd.viewDef->viewEntitys ) {
			/* a 2D view draws without the depth test, as RB_BeginDrawingView sets it */
			draw.stateBits = ( draw.stateBits & ~(unsigned)( GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHFUNC_EQUAL ) )
				| GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHMASK;
		}
		if ( pStage->privatePolygonOffset ) {
			RB_VK_PolygonOffset( &draw, pStage->privatePolygonOffset );
		} else if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			RB_VK_PolygonOffset( &draw, shader->GetPolygonOffset() );
		}

		switch ( pStage->texture.texgen ) {
		case TG_DIFFUSE_CUBE:
			draw.program = VKB_PROGRAM_DIFFUSE_CUBE;
			if ( pStage->texture.hasMatrix ) {
				RB_GetShaderTextureMatrix( regs, &pStage->texture, block.textureMatrix );
			}
			break;
		case TG_SKYBOX_CUBE:
			draw.program = VKB_PROGRAM_SKYBOX;
			R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg,
				*(idVec3 *)block.localViewOrigin );
			block.localViewOrigin[3] = 1.0f;
			if ( pStage->texture.hasMatrix ) {
				RB_GetShaderTextureMatrix( regs, &pStage->texture, block.textureMatrix );
			}
			break;
		case TG_WOBBLESKY_CUBE:
			draw.program = VKB_PROGRAM_SKYBOX;
			R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg,
				*(idVec3 *)block.localViewOrigin );
			block.localViewOrigin[3] = 1.0f;
			if ( pStage->texture.hasMatrix ) {
				float texturematrix[16];
				RB_GetShaderTextureMatrix( regs, &pStage->texture, texturematrix );
				myGlMultMatrix( texturematrix, surf->wobbleTransform, block.textureMatrix );
			} else {
				memcpy( block.textureMatrix, surf->wobbleTransform, sizeof( block.textureMatrix ) );
			}
			break;
		case TG_SCREEN:
		case TG_SCREEN2:
		case TG_GLASSWARP:
			continue;
		case TG_REFLECT_CUBE: {
			/* with a bump map: per-pixel through the normal map, as ARB2's bumpy environment */
			const shaderStage_t *bumpStage = shader->GetBumpStage();
			if ( bumpStage ) {
				idImage *images[2];
				images[0] = pStage->texture.image;
				images[1] = bumpStage->texture.image;
				RB_VK_DrawStageProgram( surf, pStage, GLSL_STAGE_BUMPY_ENVIRONMENT, images, 2 );
				continue;
			}
			draw.program = VKB_PROGRAM_REFLECT;
			R_TransposeGLMatrix( backEnd.viewDef->worldSpace.modelViewMatrix, block.textureMatrix );
			break;
		}
		default:
			draw.program = guiLut ? VKB_PROGRAM_DIFFUSE_GUI : VKB_PROGRAM_DIFFUSE;
			draw.image[1] = guiLut;
			if ( pStage->texture.hasMatrix ) {
				RB_GetShaderTextureMatrix( regs, &pStage->texture, block.textureMatrix );
			}
			break;
		}

		/* the emissive headroom: an additive stage is emitted light */
		{
			const int blend = pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
			const deform_t dfm = shader->Deform();
			const float gain = ( dfm == DFRM_PARTICLE || dfm == DFRM_PARTICLE2 ) ? hdr_particle_gain : hdr_emissive_gain;
			if ( hdr_output_active && gain != 1.0f && ( blend & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE ) {
				color[0] *= gain;
				color[1] *= gain;
				color[2] *= gain;
			}
		}
		memcpy( block.glColor, color, sizeof( block.glColor ) );
		RB_GLSLEpilogueParms( pStage->texture.texgen != TG_REFLECT_CUBE && RB_FilterBlend( pStage->drawStateBits ), hp );
		memcpy( block.hdrParms, hp, sizeof( block.hdrParms ) );
		switch ( pStage->vertexColor ) {
		case SVC_MODULATE:
			block.colorModulateAdd[0] = 1.0f;
			block.colorModulateAdd[1] = 0.0f;
			break;
		case SVC_INVERSE_MODULATE:
			block.colorModulateAdd[0] = -1.0f;
			block.colorModulateAdd[1] = 1.0f;
			break;
		default:
			break;
		}
		draw.image[0] = RB_VK_StageImage( &pStage->texture );
		vkb_draw( &draw );
	}
	if ( depthRange ) {
		vkb_depth_range( 0.0f, 1.0f );
	}
}

/*
====================
RB_VK_DrawStageProgram

A stage through one of the custom stage programs: the stage's images on
their units, its vertex parms, the view's projection and _currentRender
placement.  A post-process stage reads the scene back, so its epilogue
leaves the encoding alone.
====================
*/
static void RB_VK_DrawStageProgram( const drawSurf_t *surf, const shaderStage_t *pStage, int programId,
	idImage *const *images, int numImages ) {
	static const unsigned char programs[GLSL_STAGE_COUNT] = {
		VKB_PROGRAM_COUNT, VKB_PROGRAM_HEAT_HAZE, VKB_PROGRAM_HEAT_HAZE_MASK, VKB_PROGRAM_HEAT_HAZE_MASK_VERTEX,
		VKB_PROGRAM_COLOR_PROCESS, VKB_PROGRAM_BUMPY_ENVIRONMENT
	};
	const float *regs = surf->shaderRegisters;
	const newShaderStage_t *newStage = pStage->newStage;
	vkb_draw_t draw;
	vkb_draw_block_t block;
	float hp[4];
	int i;

	if ( programId <= GLSL_STAGE_NONE || programId >= GLSL_STAGE_COUNT ) {
		return;
	}
	RB_VK_DrawBegin( &draw, &block, surf );
	if ( !RB_VK_Geometry( surf->geo, &draw ) ) {
		return;
	}
	draw.program = programs[programId];
	draw.cull = RB_VK_Cull( surf->material->GetCullType() );
	draw.stateBits = (unsigned)pStage->drawStateBits;
	if ( pStage->privatePolygonOffset ) {
		RB_VK_PolygonOffset( &draw, pStage->privatePolygonOffset );
	} else if ( surf->material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		RB_VK_PolygonOffset( &draw, surf->material->GetPolygonOffset() );
	}
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, *(idVec3 *)block.localViewOrigin );
	block.localViewOrigin[3] = 1.0f;
	if ( newStage ) {
		for ( i = 0; i < newStage->numVertexParms && i < 4; i++ ) {
			block.vertexParms[i][0] = regs[newStage->vertexParms[i][0]];
			block.vertexParms[i][1] = regs[newStage->vertexParms[i][1]];
			block.vertexParms[i][2] = regs[newStage->vertexParms[i][2]];
			block.vertexParms[i][3] = regs[newStage->vertexParms[i][3]];
		}
	}
	{
		const float w = (float)( backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1 );
		const float h = (float)( backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
		block.screenParms[0] = 1.0f / w;
		block.screenParms[1] = 1.0f / h;
		block.screenParms[2] = w / globalImages->currentRenderImage->uploadWidth;
		block.screenParms[3] = h / globalImages->currentRenderImage->uploadHeight;
	}
	if ( surf->material->GetSort() >= SS_POST_PROCESS ) {
		RB_GLSLEpilogueParms( true, hp );
		hp[0] = hp[2] = hp[3] = 1.0f;
	} else {
		RB_GLSLEpilogueParms( RB_FilterBlend( pStage->drawStateBits ), hp );
	}
	memcpy( block.hdrParms, hp, sizeof( block.hdrParms ) );
	for ( i = 0; i < numImages && i < 6; i++ ) {
		draw.image[i] = images[i] ? RB_VK_Image( images[i] ) : 0;
	}
	vkb_draw( &draw );
}

/*
==================
RB_VK_DrawSoftParticle

A particle stage through the softening program (#3878), with the fade
the ARB2 path gives it: over the particle's diameter for an alpha blend,
its radius for an additive one, on the channels the blend reads.
==================
*/
static void RB_VK_DrawSoftParticle( const drawSurf_t *surf, const shaderStage_t *pStage, const float color[4] ) {
	const int srcBlend = pStage->drawStateBits & GLS_SRCBLEND_BITS;
	const float radius = surf->particle_radius;
	const float fadeRange = srcBlend == GLS_SRCBLEND_SRC_ALPHA ? radius * 2.0f : radius;
	const float p14 = backEnd.viewDef->projectionMatrix[14];
	vkb_draw_t draw;
	vkb_draw_block_t block;

	RB_VK_DrawBegin( &draw, &block, surf );
	if ( !RB_VK_Geometry( surf->geo, &draw ) ) {
		return;
	}
	/* the softening replaces the model depth hack; the weapon's stays */
	block.colorModulateAdd[2] = surf->space->weaponDepthHack ? p14 * 0.25f - p14 : 0.0f;
	draw.program = VKB_PROGRAM_SOFT_PARTICLE;
	draw.cull = RB_VK_Cull( surf->material->GetCullType() );
	/* the program decides the depth test, so the particle can overdraw */
	draw.stateBits = (unsigned)pStage->drawStateBits | GLS_DEPTHFUNC_ALWAYS;
	if ( pStage->texture.hasMatrix ) {
		RB_GetShaderTextureMatrix( surf->shaderRegisters, &pStage->texture, block.textureMatrix );
	}
	/* the particle system fades through the vertex colour; a stage that
	 * ignores it takes the material's colour instead */
	if ( pStage->vertexColor == SVC_IGNORE ) {
		memcpy( block.glColor, color, sizeof( block.glColor ) );
	} else {
		block.colorModulateAdd[0] = pStage->vertexColor == SVC_INVERSE_MODULATE ? -1.0f : 1.0f;
		block.colorModulateAdd[1] = pStage->vertexColor == SVC_INVERSE_MODULATE ? 1.0f : 0.0f;
	}
	block.vertexParms[0][0] = radius;
	block.vertexParms[0][1] = 1.0f / fadeRange;
	block.vertexParms[0][2] = 1.0f / radius;
	block.vertexParms[0][3] = 0.0f;
	/* an alpha blend fades its alpha, an additive one its colour */
	block.vertexParms[1][0] = block.vertexParms[1][1] = block.vertexParms[1][2] = srcBlend == GLS_SRCBLEND_SRC_ALPHA ? 1.0f : 0.0f;
	block.vertexParms[1][3] = srcBlend == GLS_SRCBLEND_SRC_ALPHA ? 0.0f : 1.0f;
	block.screenParms[0] = 1.0f / globalImages->currentDepthImage->uploadWidth;
	block.screenParms[1] = 1.0f / globalImages->currentDepthImage->uploadHeight;
	/* the scene encoding fold alone: no gamma or clamp on a blended particle */
	block.hdrParms[0] = block.hdrParms[1] = block.hdrParms[2] = hdr_scene_encode_scale;
	block.hdrParms[3] = 1.0f;
	draw.image[0] = RB_VK_StageImage( &pStage->texture );
	draw.image[1] = RB_VK_Image( globalImages->currentDepthImage );
	vkb_draw( &draw );
}

/*
=====================
RB_VK_DrawInteraction

One light stage on one surface's maps, as RB_SubmittInteraction hands
them over.
=====================
*/
static struct {
	vkb_draw_t draw;
	vkb_draw_block_t block;
} rbVkInter;

static void RB_VK_DrawInteraction( const drawInteraction_t *din ) {
	vkb_draw_t *draw = &rbVkInter.draw;
	vkb_draw_block_t *block = &rbVkInter.block;
	const float specBoost = RB_HDRSpecularBoost();
	float hp[4];

	memcpy( block->localLightOrigin, din->localLightOrigin.ToFloatPtr(), sizeof( block->localLightOrigin ) );
	memcpy( block->localViewOrigin, din->localViewOrigin.ToFloatPtr(), sizeof( block->localViewOrigin ) );
	memcpy( block->lightProjection, din->lightProjection.ToFloatPtr(), sizeof( block->lightProjection ) );
	memcpy( block->bumpMatrixS, din->bumpMatrix[0].ToFloatPtr(), sizeof( block->bumpMatrixS ) );
	memcpy( block->bumpMatrixT, din->bumpMatrix[1].ToFloatPtr(), sizeof( block->bumpMatrixT ) );
	memcpy( block->diffuseMatrixS, din->diffuseMatrix[0].ToFloatPtr(), sizeof( block->diffuseMatrixS ) );
	memcpy( block->diffuseMatrixT, din->diffuseMatrix[1].ToFloatPtr(), sizeof( block->diffuseMatrixT ) );
	memcpy( block->specularMatrixS, din->specularMatrix[0].ToFloatPtr(), sizeof( block->specularMatrixS ) );
	memcpy( block->specularMatrixT, din->specularMatrix[1].ToFloatPtr(), sizeof( block->specularMatrixT ) );
	switch ( din->vertexColor ) {
	case SVC_MODULATE:
		block->colorModulateAdd[0] = 1.0f;
		block->colorModulateAdd[1] = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		block->colorModulateAdd[0] = -1.0f;
		block->colorModulateAdd[1] = 1.0f;
		break;
	default:
		block->colorModulateAdd[0] = 0.0f;
		block->colorModulateAdd[1] = 1.0f;
		break;
	}
	memcpy( block->diffuseColor, din->diffuseColor.ToFloatPtr(), sizeof( block->diffuseColor ) );
	/* the HDR specular boost, folded into the constant as the GL paths do */
	block->specularColor[0] = din->specularColor[0] * specBoost;
	block->specularColor[1] = din->specularColor[1] * specBoost;
	block->specularColor[2] = din->specularColor[2] * specBoost;
	block->specularColor[3] = din->specularColor[3];
	RB_GLSLEpilogueParms( false, hp );
	memcpy( block->hdrParms, hp, sizeof( block->hdrParms ) );

	draw->image[0] = RB_VK_Image( din->bumpImage );
	draw->image[1] = RB_VK_Image( din->lightFalloffImage );
	draw->image[2] = RB_VK_Image( din->lightImage );
	draw->image[3] = RB_VK_Image( din->diffuseImage );
	draw->image[4] = RB_VK_Image( din->specularImage );
	vkb_draw( draw );
}

/*
=============
RB_VK_CreateSingleDrawInteractions

A light's stages against a surface's bump, diffuse and specular stages,
as RB_CreateSingleDrawInteractions decomposes them.
=============
*/
static void RB_VK_CreateSingleDrawInteractions( const drawSurf_t *surf, const viewLight_t *vLight ) {
	const idMaterial *surfaceShader = surf->material;
	const float *surfaceRegs = surf->shaderRegisters;
	const idMaterial *lightShader = vLight->lightShader;
	const float *lightRegs = vLight->shaderRegisters;
	drawInteraction_t inter;
	idPlane lightProject[4];
	int i, lightStageNum, surfaceStageNum;

	if ( r_skipInteractions.GetBool() || !surf->geo || !surf->geo->ambientCache ) {
		return;
	}
	RB_VK_Scissor( surf );
	inter.diffuseMatrix[0].Zero();
	inter.diffuseMatrix[1].Zero();
	inter.specularMatrix[0].Zero();
	inter.specularMatrix[1].Zero();
	inter.surf = surf;
	inter.lightFalloffImage = vLight->falloffImage;
	R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3() );
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3() );
	inter.localLightOrigin[3] = 0;
	inter.localViewOrigin[3] = 1;
	inter.ambientLight = lightShader->IsAmbientLight();
	for ( i = 0; i < 4; i++ ) {
		R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[i], lightProject[i] );
	}

	for ( lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++ ) {
		const shaderStage_t *lightStage = lightShader->GetStage( lightStageNum );
		const float lightscale = r_lightScale.GetFloat();
		float lightColor[4];
		if ( !lightRegs[lightStage->conditionRegister] ) {
			continue;
		}
		inter.lightImage = lightStage->texture.image;
		inter.lightProjection[0] = lightProject[0].ToVec4();
		inter.lightProjection[1] = lightProject[1].ToVec4();
		inter.lightProjection[2] = lightProject[3].ToVec4();   /* the falloff: the 4th plane */
		inter.lightProjection[3] = lightProject[2].ToVec4();   /* q: the 3rd */
		if ( lightStage->texture.hasMatrix ) {
			float lightTextureMatrix[16];
			RB_GetShaderTextureMatrix( lightRegs, &lightStage->texture, lightTextureMatrix );
			RB_BakeTextureMatrixIntoTexgen( inter.lightProjection, lightTextureMatrix );
		}
		inter.bumpImage = NULL;
		inter.specularImage = NULL;
		inter.diffuseImage = NULL;
		inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
		inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;
		lightColor[0] = lightscale * lightRegs[lightStage->color.registers[0]];
		lightColor[1] = lightscale * lightRegs[lightStage->color.registers[1]];
		lightColor[2] = lightscale * lightRegs[lightStage->color.registers[2]];
		lightColor[3] = lightRegs[lightStage->color.registers[3]];

		for ( surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++ ) {
			const shaderStage_t *surfaceStage = surfaceShader->GetStage( surfaceStageNum );
			switch ( surfaceStage->lighting ) {
			case SL_AMBIENT:
				break;
			case SL_BUMP:
				if ( !surfaceRegs[surfaceStage->conditionRegister] ) {
					break;
				}
				RB_SubmittInteraction( &inter, RB_VK_DrawInteraction );
				inter.diffuseImage = NULL;
				inter.specularImage = NULL;
				RB_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.bumpImage, inter.bumpMatrix, NULL );
				break;
			case SL_DIFFUSE:
				if ( !surfaceRegs[surfaceStage->conditionRegister] ) {
					break;
				}
				if ( inter.diffuseImage ) {
					RB_SubmittInteraction( &inter, RB_VK_DrawInteraction );
				}
				RB_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.diffuseImage, inter.diffuseMatrix,
					inter.diffuseColor.ToFloatPtr() );
				inter.diffuseColor[0] *= lightColor[0];
				inter.diffuseColor[1] *= lightColor[1];
				inter.diffuseColor[2] *= lightColor[2];
				inter.diffuseColor[3] *= lightColor[3];
				inter.vertexColor = surfaceStage->vertexColor;
				break;
			case SL_SPECULAR:
				if ( !surfaceRegs[surfaceStage->conditionRegister] ) {
					break;
				}
				if ( inter.specularImage ) {
					RB_SubmittInteraction( &inter, RB_VK_DrawInteraction );
				}
				RB_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.specularImage, inter.specularMatrix,
					inter.specularColor.ToFloatPtr() );
				inter.specularColor[0] *= lightColor[0];
				inter.specularColor[1] *= lightColor[1];
				inter.specularColor[2] *= lightColor[2];
				inter.specularColor[3] *= lightColor[3];
				inter.vertexColor = surfaceStage->vertexColor;
				break;
			}
		}
		RB_SubmittInteraction( &inter, RB_VK_DrawInteraction );
	}
}

/*
=============
RB_VK_CreateDrawInteractions

The surfaces of a light: additive, against the depth already filled,
through the stencil the shadow volumes left.
=============
*/
static void RB_VK_CreateDrawInteractions( const drawSurf_t *surf, const viewLight_t *vLight, bool shadowed, int depthFunc ) {
	for ( ; surf; surf = surf->nextOnLight ) {
		vkb_draw_t *draw = &rbVkInter.draw;
		vkb_draw_block_t *block = &rbVkInter.block;
		bool depthRange;
		if ( !surf->geo || !surf->geo->ambientCache ) {
			continue;
		}
		RB_VK_DrawBegin( draw, block, surf );
		if ( !RB_VK_Geometry( surf->geo, draw ) ) {
			continue;
		}
		depthRange = RB_VK_WeaponDepthRange( surf );
		draw->program = r_usePhong.GetBool() ? VKB_PROGRAM_INTERACTION_PHONG : VKB_PROGRAM_INTERACTION;
		block->vertexParms[0][0] = r_specularExponent.GetFloat();
		draw->stateBits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depthFunc;
		draw->cull = RB_VK_Cull( surf->material->GetCullType() );
		draw->stencil = shadowed ? VKB_STENCIL_TEST : VKB_STENCIL_OFF;
		draw->stencilRef = 128;
		draw->stencilMask = 255;
		RB_VK_CreateSingleDrawInteractions( surf, vLight );
		if ( depthRange ) {
			vkb_depth_range( 0.0f, 1.0f );
		}
	}
}

/*
=====================
RB_VK_StencilShadowPass

The light's shadow volumes into the stencil: depth-fail where the view
may be inside a volume, depth-pass elsewhere.
=====================
*/
static void RB_VK_StencilShadowPass( const drawSurf_t *surf, const viewLight_t *vLight ) {
	if ( !r_shadows.GetBool() ) {
		return;
	}
	for ( ; surf; surf = surf->nextOnLight ) {
		const srfTriangles_t *tri = surf->geo;
		vkb_draw_t draw;
		vkb_draw_block_t block;
		const vertCache_t *vc = tri->shadowCache;
		idVec4 localLight;
		int numIndexes;
		bool external = false;
		bool depthRange;

		if ( !vc || vc->virtMem ) {
			continue;
		}
		RB_VK_DrawBegin( &draw, &block, surf );
		draw.vertexBuffer = vc->vbo;
		draw.vertexOffset = (unsigned)vc->offset;
		draw.layout = VKB_VERTEX_SHADOW;
		if ( tri->indexCache && !tri->indexCache->virtMem ) {
			draw.indexBuffer = tri->indexCache->vbo;
			draw.indexOffset = (unsigned)tri->indexCache->offset;
			draw.shortIndexes = tri->indexCache->indexType == GL_UNSIGNED_SHORT;
		} else if ( !RB_VK_FrameIndexes( tri, &draw ) ) {
			continue;
		}
		RB_VK_Scissor( surf );
		depthRange = RB_VK_WeaponDepthRange( surf );

		R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, localLight.ToVec3() );
		localLight.w = 0.0f;
		memcpy( block.localLightOrigin, localLight.ToFloatPtr(), sizeof( block.localLightOrigin ) );

		/* the sil planes always; the caps only where they can show */
		if ( !r_useExternalShadows.GetInteger() ) {
			numIndexes = tri->numIndexes;
		} else if ( r_useExternalShadows.GetInteger() == 2 ) {
			numIndexes = tri->numShadowIndexesNoCaps;
		} else if ( !( surf->dsFlags & DSF_VIEW_INSIDE_SHADOW ) ) {
			numIndexes = tri->numShadowIndexesNoCaps;
			external = true;
		} else if ( !vLight->viewInsideLight && !( tri->shadowCapPlaneBits & SHADOW_CAP_INFINITE ) ) {
			if ( vLight->viewSeesShadowPlaneBits & tri->shadowCapPlaneBits ) {
				numIndexes = tri->numShadowIndexesNoFrontCaps;
			} else {
				numIndexes = tri->numShadowIndexesNoCaps;
			}
			external = true;
		} else {
			numIndexes = tri->numIndexes;
		}
		draw.indexCount = (unsigned)numIndexes;
		draw.program = VKB_PROGRAM_SHADOW;
		draw.cull = VKB_CULL_NONE;
		draw.stateBits = GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS;
		draw.stencil = external ? VKB_STENCIL_SHADOW_PASS : VKB_STENCIL_SHADOW_FAIL;
		draw.stencilRef = 1;
		draw.stencilMask = 255;
		if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
			draw.depthBias[0] = -r_shadowPolygonOffset.GetFloat();
			draw.depthBias[1] = r_shadowPolygonFactor.GetFloat();
		}
		vkb_draw( &draw );
		if ( depthRange ) {
			vkb_depth_range( 0.0f, 1.0f );
		}
	}
}

/*
==================
RB_VK_DrawInteractions

Every light's shadows and interactions, as RB_GLSL_DrawInteractions.
==================
*/
static void RB_VK_DrawInteractions( void ) {
	const viewLight_t *vLight;
	for ( vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next ) {
		bool shadowed = false;
		if ( vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight() ) {
			continue;
		}
		if ( !vLight->localInteractions && !vLight->globalInteractions && !vLight->translucentInteractions ) {
			continue;
		}
		if ( vLight->globalShadows || vLight->localShadows ) {
			/* the stencil cleared for this light's volumes, within its scissor */
			RB_VK_ScissorRect( vLight->scissorRect );
			vkb_clear( 0, 0, 1, NULL, 1u << ( glConfig.stencilBits - 1 ) );
			shadowed = true;
		}
		RB_VK_StencilShadowPass( vLight->globalShadows, vLight );
		RB_VK_CreateDrawInteractions( vLight->localInteractions, vLight, shadowed, GLS_DEPTHFUNC_EQUAL );
		RB_VK_StencilShadowPass( vLight->localShadows, vLight );
		RB_VK_CreateDrawInteractions( vLight->globalInteractions, vLight, shadowed, GLS_DEPTHFUNC_EQUAL );
		/* translucent surfaces never get stencil shadowed */
		if ( r_skipTranslucent.GetBool() ) {
			continue;
		}
		RB_VK_CreateDrawInteractions( vLight->translucentInteractions, vLight, false, GLS_DEPTHFUNC_LESS );
	}
}

/*
==================
RB_VK_FogPass

A fog light: its surfaces through the fog and enter maps, then the
light's frustum from inside.
==================
*/
static idPlane rbVkFogPlanes[4];

static void RB_VK_T_BasicFog( const drawSurf_t *surf, const viewLight_t *vLight, unsigned stateBits, unsigned char cull ) {
	vkb_draw_t draw;
	vkb_draw_block_t block;
	idPlane local[4];
	const float *regs = vLight->shaderRegisters;
	const shaderStage_t *stage = vLight->lightShader->GetStage( 0 );
	bool depthRange;

	if ( !surf->geo || !surf->geo->ambientCache ) {
		return;
	}
	RB_VK_DrawBegin( &draw, &block, surf );
	if ( !RB_VK_Geometry( surf->geo, &draw ) ) {
		return;
	}
	RB_VK_Scissor( surf );
	depthRange = RB_VK_WeaponDepthRange( surf );
	R_GlobalPlaneToLocal( surf->space->modelMatrix, rbVkFogPlanes[0], local[0] );
	local[0][3] += 0.5f;
	local[1][0] = local[1][1] = local[1][2] = 0.0f;
	local[1][3] = 0.5f;
	R_GlobalPlaneToLocal( surf->space->modelMatrix, rbVkFogPlanes[2], local[2] );
	local[2][3] += FOG_ENTER;
	R_GlobalPlaneToLocal( surf->space->modelMatrix, rbVkFogPlanes[3], local[3] );
	memcpy( &block.lightProjection[0], local[0].ToFloatPtr(), 4 * sizeof( float ) );
	memcpy( &block.lightProjection[4], local[1].ToFloatPtr(), 4 * sizeof( float ) );
	memcpy( &block.lightProjection[8], local[2].ToFloatPtr(), 4 * sizeof( float ) );
	memcpy( &block.lightProjection[12], local[3].ToFloatPtr(), 4 * sizeof( float ) );
	block.fogColor[0] = regs[stage->color.registers[0]];
	block.fogColor[1] = regs[stage->color.registers[1]];
	block.fogColor[2] = regs[stage->color.registers[2]];
	block.fogColor[3] = regs[stage->color.registers[3]];
	draw.program = VKB_PROGRAM_FOG;
	draw.stateBits = stateBits;
	draw.cull = cull;
	draw.image[0] = RB_VK_Image( globalImages->fogImage );
	draw.image[1] = RB_VK_Image( globalImages->fogEnterImage );
	vkb_draw( &draw );
	if ( depthRange ) {
		vkb_depth_range( 0.0f, 1.0f );
	}
}

static void RB_VK_FogPass( const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2, const viewLight_t *vLight ) {
	const srfTriangles_t *frustumTris = vLight->frustumTris;
	const float *regs = vLight->shaderRegisters;
	const shaderStage_t *stage = vLight->lightShader->GetStage( 0 );
	const float *mv = backEnd.viewDef->worldSpace.modelViewMatrix;
	const float alpha = regs[stage->color.registers[3]];
	const float a = ( alpha <= 1.0f ) ? -0.5f / DEFAULT_FOG_DISTANCE : -0.5f / alpha;
	const unsigned surfaceBits = GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL;
	drawSurf_t ds;
	float s;
	const drawSurf_t *surf;

	if ( !frustumTris->ambientCache ) {
		return;
	}
	memset( &ds, 0, sizeof( ds ) );
	ds.space = &backEnd.viewDef->worldSpace;
	ds.geo = frustumTris;
	ds.scissorRect = backEnd.viewDef->scissor;

	rbVkFogPlanes[0][0] = a * mv[2];
	rbVkFogPlanes[0][1] = a * mv[6];
	rbVkFogPlanes[0][2] = a * mv[10];
	rbVkFogPlanes[0][3] = a * mv[14];
	rbVkFogPlanes[1][0] = a * mv[0];
	rbVkFogPlanes[1][1] = a * mv[4];
	rbVkFogPlanes[1][2] = a * mv[8];
	rbVkFogPlanes[1][3] = a * mv[12];
	/* T: the fade plane, the "top" plane on unrotated lights */
	rbVkFogPlanes[2][0] = 0.001f * vLight->fogPlane[0];
	rbVkFogPlanes[2][1] = 0.001f * vLight->fogPlane[1];
	rbVkFogPlanes[2][2] = 0.001f * vLight->fogPlane[2];
	rbVkFogPlanes[2][3] = 0.001f * vLight->fogPlane[3];
	/* S: by the view origin */
	s = backEnd.viewDef->renderView.vieworg * rbVkFogPlanes[2].Normal() + rbVkFogPlanes[2][3];
	rbVkFogPlanes[3][0] = 0;
	rbVkFogPlanes[3][1] = 0;
	rbVkFogPlanes[3][2] = 0;
	rbVkFogPlanes[3][3] = FOG_ENTER + s;

	for ( surf = drawSurfs; surf; surf = surf->nextOnLight ) {
		RB_VK_T_BasicFog( surf, vLight, surfaceBits, RB_VK_Cull( CT_FRONT_SIDED ) );
	}
	for ( surf = drawSurfs2; surf; surf = surf->nextOnLight ) {
		RB_VK_T_BasicFog( surf, vLight, surfaceBits, RB_VK_Cull( CT_FRONT_SIDED ) );
	}
	/* the light frustum's planes aren't in the depth buffer: less, from inside */
	RB_VK_T_BasicFog( &ds, vLight,
		GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS,
		RB_VK_Cull( CT_BACK_SIDED ) );
}

/*
=====================
RB_VK_BlendLight

The falloff and projection maps blended onto the surfaces, in the
light's colour, without the surface's own maps.
=====================
*/
static void RB_VK_T_BlendLight( const drawSurf_t *surf, const viewLight_t *vLight, const shaderStage_t *stage,
	const float lightColor[4] ) {
	const srfTriangles_t *tri = surf->geo;
	vkb_draw_t draw;
	vkb_draw_block_t block;
	idPlane lightProject[4];
	bool depthRange;
	int i;

	RB_VK_DrawBegin( &draw, &block, surf );
	if ( tri->ambientCache ) {
		if ( !RB_VK_Geometry( tri, &draw ) ) {
			return;
		}
	} else if ( tri->shadowCache && !tri->shadowCache->virtMem ) {
		draw.vertexBuffer = tri->shadowCache->vbo;
		draw.vertexOffset = (unsigned)tri->shadowCache->offset;
		draw.layout = VKB_VERTEX_SHADOW;
		draw.indexCount = (unsigned)tri->numIndexes;
		if ( tri->indexCache && !tri->indexCache->virtMem ) {
			draw.indexBuffer = tri->indexCache->vbo;
			draw.indexOffset = (unsigned)tri->indexCache->offset;
			draw.shortIndexes = tri->indexCache->indexType == GL_UNSIGNED_SHORT;
		} else if ( !RB_VK_FrameIndexes( tri, &draw ) ) {
			return;
		}
	} else {
		return;
	}
	RB_VK_Scissor( surf );
	depthRange = RB_VK_WeaponDepthRange( surf );
	for ( i = 0; i < 4; i++ ) {
		R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[i], lightProject[i] );
		memcpy( &block.lightProjection[i * 4], lightProject[i].ToFloatPtr(), 4 * sizeof( float ) );
	}
	memcpy( block.fogColor, lightColor, sizeof( block.fogColor ) );
	draw.program = VKB_PROGRAM_BLEND_LIGHT;
	draw.stateBits = GLS_DEPTHMASK | (unsigned)stage->drawStateBits | GLS_DEPTHFUNC_EQUAL;
	draw.cull = RB_VK_Cull( CT_FRONT_SIDED );
	draw.image[0] = RB_VK_Image( stage->texture.image );
	draw.image[1] = RB_VK_Image( vLight->falloffImage );
	vkb_draw( &draw );
	if ( depthRange ) {
		vkb_depth_range( 0.0f, 1.0f );
	}
}

static void RB_VK_BlendLight( const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2, const viewLight_t *vLight ) {
	const idMaterial *lightShader = vLight->lightShader;
	const float *regs = vLight->shaderRegisters;
	int i;
	if ( !drawSurfs || r_skipBlendLights.GetBool() ) {
		return;
	}
	for ( i = 0; i < lightShader->GetNumStages(); i++ ) {
		const shaderStage_t *stage = lightShader->GetStage( i );
		const drawSurf_t *surf;
		float lightColor[4];
		if ( !regs[stage->conditionRegister] ) {
			continue;
		}
		lightColor[0] = regs[stage->color.registers[0]];
		lightColor[1] = regs[stage->color.registers[1]];
		lightColor[2] = regs[stage->color.registers[2]];
		lightColor[3] = regs[stage->color.registers[3]];
		/* an additive blend light is a glow the volume emits: the emissive headroom */
		if ( hdr_output_active && hdr_emissive_gain != 1.0f
				&& ( stage->drawStateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE ) {
			lightColor[0] *= hdr_emissive_gain;
			lightColor[1] *= hdr_emissive_gain;
			lightColor[2] *= hdr_emissive_gain;
		}
		for ( surf = drawSurfs; surf; surf = surf->nextOnLight ) {
			RB_VK_T_BlendLight( surf, vLight, stage, lightColor );
		}
		for ( surf = drawSurfs2; surf; surf = surf->nextOnLight ) {
			RB_VK_T_BlendLight( surf, vLight, stage, lightColor );
		}
	}
}

/*
==================
RB_VK_FogAllLights
==================
*/
static void RB_VK_FogAllLights( void ) {
	const viewLight_t *vLight;
	if ( r_skipFogLights.GetBool() || backEnd.viewDef->isXraySubview ) {
		return;
	}
	for ( vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next ) {
		if ( vLight->lightShader->IsFogLight() ) {
			RB_VK_FogPass( vLight->globalInteractions, vLight->localInteractions, vLight );
		} else if ( vLight->lightShader->IsBlendLight() ) {
			RB_VK_BlendLight( vLight->globalInteractions, vLight->localInteractions, vLight );
		}
	}
}

/*
=====================
RB_VK_DrawShaderPasses

Returns the number of surfaces drawn; the post-process ones wait for
_currentRender.
=====================
*/
static int RB_VK_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int i;
	if ( numDrawSurfs == 0 || r_skipAmbient.GetBool() ) {
		return numDrawSurfs;
	}
	if ( drawSurfs[0]->material->GetSort() >= SS_POST_PROCESS ) {
		if ( r_skipPostProcess.GetBool() ) {
			return 0;
		}
		R_VK_CopyFramebuffer( globalImages->currentRenderImage, backEnd.viewDef->viewport.x1,
			backEnd.viewDef->viewport.y1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
			backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
		backEnd.currentRenderCopied = true;
	}
	backEnd.currentSpace = NULL;
	for ( i = 0; i < numDrawSurfs; i++ ) {
		if ( drawSurfs[i]->material->SuppressInSubview() ) {
			continue;
		}
		if ( backEnd.viewDef->isXraySubview && drawSurfs[i]->space->entityDef ) {
			if ( drawSurfs[i]->space->entityDef->parms.xrayIndex != 2 ) {
				continue;
			}
		}
		if ( drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS && !backEnd.currentRenderCopied ) {
			break;
		}
		RB_VK_T_RenderShaderPasses( drawSurfs[i] );
		backEnd.currentSpace = drawSurfs[i]->space;
	}
	return i;
}

/*
=====================
RB_VK_DrawView

A view: the depth fill, the lights, the ambient and post-process passes.
=====================
*/
static void RB_VK_DrawView( const void *data ) {
	const drawSurfsCommand_t *cmd = (const drawSurfsCommand_t *)data;
	const viewDef_t *viewDef;
	drawSurf_t **drawSurfs;
	int numDrawSurfs, processed;

	backEnd.viewDef = cmd->viewDef;
	viewDef = backEnd.viewDef;
	if ( viewDef->numDrawSurfs == 0 ) {
		return;
	}
	backEnd.pc.c_surfaces += viewDef->numDrawSurfs;
	backEnd.currentRenderCopied = false;
	backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;

	vkb_viewport( tr.viewportOffset[0] + viewDef->viewport.x1, tr.viewportOffset[1] + viewDef->viewport.y1,
		viewDef->viewport.x2 + 1 - viewDef->viewport.x1, viewDef->viewport.y2 + 1 - viewDef->viewport.y1 );
	vkb_scissor( tr.viewportOffset[0] + viewDef->viewport.x1 + viewDef->scissor.x1,
		tr.viewportOffset[1] + viewDef->viewport.y1 + viewDef->scissor.y1,
		viewDef->scissor.x2 + 1 - viewDef->scissor.x1, viewDef->scissor.y2 + 1 - viewDef->scissor.y1 );
	backEnd.currentScissor = viewDef->scissor;
	RB_VK_SetView();
	RB_DetermineLightScale();

	drawSurfs = (drawSurf_t **)&viewDef->drawSurfs[0];
	numDrawSurfs = viewDef->numDrawSurfs;

	RB_VK_FillDepthBuffer( drawSurfs, numDrawSurfs );
	RB_VK_DrawInteractions();
	processed = RB_VK_DrawShaderPasses( drawSurfs, numDrawSurfs );
	RB_VK_FogAllLights();
	if ( processed < numDrawSurfs ) {
		RB_VK_DrawShaderPasses( drawSurfs + processed, numDrawSurfs - processed );
	}
}

/*
====================
RB_VK_SetBuffer

The frame's clear, as RB_SetBuffer decides it.
====================
*/
static void RB_VK_SetBuffer( const void *data ) {
	const setBufferCommand_t *cmd = (const setBufferCommand_t *)data;
	float c[4];
	backEnd.frameCount = cmd->frameCount;
	if ( !( r_clear.GetFloat() || idStr::Length( r_clear.GetString() ) != 1 || r_lockSurfaces.GetBool()
			|| r_singleArea.GetBool() || r_showOverDraw.GetBool() ) ) {
		return;
	}
	c[3] = 1.0f;
	if ( sscanf( r_clear.GetString(), "%f %f %f", &c[0], &c[1], &c[2] ) == 3 ) {
	} else if ( r_clear.GetInteger() == 2 ) {
		c[0] = c[1] = c[2] = 0.0f;
	} else if ( r_showOverDraw.GetBool() ) {
		c[0] = c[1] = c[2] = 1.0f;
	} else {
		c[0] = 0.4f;
		c[1] = 0.0f;
		c[2] = 0.25f;
	}
	vkb_scissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	vkb_clear( 1, 0, 0, c, 0 );
}

/*
====================
RB_VK_ExecuteBackEndCommands

The frame's commands onto the device: a frame opens on the first view
and closes at the swap.
====================
*/
void RB_VK_BeginFrame( void );
extern int backEndStartTime, backEndFinishTime;
void RB_VK_ExecuteBackEndCommands( const emptyCommand_t *cmds ) {
	int c_draw3d = 0, c_draw2d = 0;
	if ( cmds->commandId == RC_NOP && !cmds->next ) {
		return;
	}
	backEndStartTime = Core_Milliseconds();
	globalImages->CompleteBackgroundImageLoads();
	vkFrameIndexes.tri = NULL;
	for ( ; cmds; cmds = (const emptyCommand_t *)cmds->next ) {
		switch ( cmds->commandId ) {
		case RC_NOP:
			break;
		case RC_DRAW_VIEW:
			RB_VK_BeginFrame();
			RB_VK_DrawView( cmds );
			if ( ( (const drawSurfsCommand_t *)cmds )->viewDef->viewEntitys ) {
				c_draw3d++;
			} else {
				c_draw2d++;
			}
			break;
		case RC_SET_BUFFER:
			RB_VK_BeginFrame();
			RB_VK_SetBuffer( cmds );
			break;
		case RC_SWAP_BUFFERS:
			RB_VK_BeginFrame();
			GLimp_SwapBuffers();
			break;
		case RC_COPY_RENDER: {
			const copyRenderCommand_t *cmd = (const copyRenderCommand_t *)cmds;
			if ( cmd->image && !r_skipCopyTexture.GetBool() ) {
				RB_VK_BeginFrame();
				R_VK_CopyFramebuffer( cmd->image, cmd->x, cmd->y, cmd->imageWidth, cmd->imageHeight );
			}
			break;
		}
		default:
			common->Error( "RB_VK_ExecuteBackEndCommands: bad commandId" );
			break;
		}
	}
	backEndFinishTime = Core_Milliseconds();
	backEnd.pc.msec = backEndFinishTime - backEndStartTime;
}
