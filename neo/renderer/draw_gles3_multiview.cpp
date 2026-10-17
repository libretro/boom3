/*
 * This file is part of the D3wasm project (http://www.continuation-labs.com/projects/d3wasm)
 * Copyright (c) 2019 Gabriel Cuvillier.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "sys/platform.h"
#include "renderer/VertexCache.h"

#include "tr_local.h"

#include "glsl_gles3/glsl_shaders.h"
#include "renderer/spectral_mix.h"
#include "renderer/glsl_blocks.h"

#ifdef HAVE_OPENGLES
#include "renderer/gles_compat.h"
#else
#include "renderer/glsl_desktop.h"
#endif

#if defined(BOOM3_VR)
#include "renderer/vr.h"
#endif

extern bool  hdr_output_active;
extern float hdr_emissive_gain;
extern float hdr_particle_gain;
extern float hdr_scene_encode_scale;
extern bool  HDR_GuiCorrectionActive( unsigned int *tex );

// the ES 3 and ES 3.1 renderers, beside the others in a desktop core
namespace gles3 {

using namespace glsl_es3;

shaderProgram_t interactionShader;
shaderProgram_t interactionBlinnShader;
shaderProgram_t interactionPhongShader;
shaderProgram_t fogShader;
shaderProgram_t blendLightShader;
shaderProgram_t zfillShader;
shaderProgram_t zfillOpaqueShader;
shaderProgram_t zfillClipShader;
shaderProgram_t diffuseMapShader;
shaderProgram_t diffuseMapGuiShader;	// SS_GUI stages under the ACES 2.0 inverse
shaderProgram_t diffuseCubeShader;
shaderProgram_t skyboxCubeShader;
shaderProgram_t reflectionCubeShader;
shaderProgram_t stencilShadowShader;
// the custom stage programs, by glslStageProgram_t; GLSL_STAGE_NONE unused
shaderProgram_t stageShaders[GLSL_STAGE_COUNT];
shaderProgram_t softParticleShader;
// _currentDepth's framebuffer, the view's depth blitted into it after the depth fill
static GLuint		depthCopyFbo;
static bool		depthCopied;		// this view's depth is in _currentDepth
static bool		depthBlitRefused;	// the driver refused the blit at this size

GLuint		viewMatricesBuffer;
// ViewMatrices block (std140): mat4 u_viewProjectionMatrices[2], projection
// times view per eye, rewritten once per view and bound to point 0
#define VIEW_MATRICES_FLOATS	(2 * 16)
#define VIEW_MATRICES_BINDING	0

// the programs were built with BOOM3_MULTIVIEW, for 2-layer VR targets
static bool glslMultiview = false;

/*
The ES 3.1 renderer hands the interaction programs their per-draw values in
two uniform blocks, one per stage, written into the vertex cache's ring and
bound by range: two binds a draw instead of a dozen uniform uploads.  The
blocks hold the very declarations the programs had as plain uniforms,
moved at load time, so the shading is the same code on the same values.
*/
#define DRAW_VERTEX_BINDING		1
#define DRAW_FRAGMENT_BINDING	2

/*
On GL 4.3 and later with shader draw parameters, the depth fill's opaque
surfaces and the shadow volumes are drawn in runs: a run of surfaces that
share the vertex and index buffers and the pass's state goes out as one
glMultiDrawElementsIndirect, each draw reading its model matrix, light
origin and depth hack from a uniform block array at gl_DrawIDARB.  The
commands and the array are written into the vertex cache's ring.  The
vertex programs are the single-draw ones with those three uniforms
redirected into the array, so every vertex lands where it did.
*/
#define DRAW_LIST_BINDING		3
#define RB_BATCH_DRAWS			128
#define RB_BATCH_ENTRY			96		/* std140 rbDraw_t: mat4, vec4, vec4 */
#ifndef GL_DRAW_INDIRECT_BUFFER
#define GL_DRAW_INDIRECT_BUFFER	0x8F3F
#endif

static shaderProgram_t	zfillOpaqueBatchShader;
static shaderProgram_t	stencilShadowBatchShader;
static bool				glslBatches = false;	// both linked

static idCVar r_glslBatches( "r_glslBatches", "1", CVAR_RENDERER | CVAR_BOOL,
	"ES 3.1 renderer on GL 4.3+: draw runs of depth-fill and shadow surfaces with one indirect draw" );

enum {
	DV_MODEL_MATRIX, DV_DEPTH_HACK, DV_LIGHT_PROJECTION, DV_COLOR_MODULATE, DV_COLOR_ADD,
	DV_LIGHT_ORIGIN, DV_VIEW_ORIGIN, DV_BUMP_S, DV_BUMP_T, DV_DIFFUSE_S, DV_DIFFUSE_T,
	DV_SPECULAR_S, DV_SPECULAR_T, DV_COUNT
};
static const char* const drawVertexNames[DV_COUNT] = {
	"u_modelMatrix", "u_depthHack", "u_lightProjection", "u_colorModulate", "u_colorAdd",
	"u_lightOrigin", "u_viewOrigin", "u_bumpMatrixS", "u_bumpMatrixT", "u_diffuseMatrixS",
	"u_diffuseMatrixT", "u_specularMatrixS", "u_specularMatrixT"
};
enum {
	DF_DIFFUSE_COLOR, DF_SPECULAR_COLOR, DF_HDR_PARMS, DF_SPECULAR_EXPONENT, DF_COUNT
};
static const char* const drawFragmentNames[DF_COUNT] = {
	"u_diffuseColor", "u_specularColor", "u_hdrParms", "u_specularExponent"
};

// where each block member sits, per interaction program; -1 absent
typedef struct {
	const shaderProgram_t*	program;
	GLint	vertexOffset[DV_COUNT];
	GLint	fragmentOffset[DF_COUNT];
	GLint	vertexSize;				// 0: the program has no block
	GLint	fragmentSize;
} drawBlocks_t;

static drawBlocks_t	drawBlocks[3];
static int			numDrawBlocks;
// the programs were built with the blocks (R_GLSL_UseDrawBlocks at load)
static bool			glslDrawBlocks = false;
// for a ring that cannot be mapped: one buffer per block, rewritten per draw
static GLuint		drawBlockFallback[2];
// the specular exponent of the interaction pass being drawn
static float		rbSpecularExponent;

/*
====================
Attribute pointers

What each attribute location was last pointed at, so a pointer that changes
nothing is not sent: with base-vertex draws the surfaces of a pass that
share a buffer all point at its start.  Forgotten at the start of each view,
as the frontend may have pointed them elsewhere.
====================
*/
typedef struct {
	const GLvoid*	pointer;
	GLuint			buffer;
	GLsizei			stride;
	GLenum			type;
	GLint			size;
	GLboolean		normalized;
	bool			valid;
} rbAttribPointer_t;

#define RB_ATTRIB_CACHE 16
static rbAttribPointer_t rbAttribs[RB_ATTRIB_CACHE];

static void RB_GLSL_ForgetAttribPointers(void) {
	int i;
	for ( i = 0; i < RB_ATTRIB_CACHE; i++ ) {
		rbAttribs[i].valid = false;
	}
}

/*
====================
RB_GLSL_VertexBase

Binds a vertex block for its attribute pointers and returns where they
start: the block itself, or - the ES 3.1 renderer, the block in a shared
buffer - the buffer's start, the draws then carrying the block's base
vertex.
====================
*/
static const byte* RB_GLSL_VertexBase(vertCache_t* cache, int stride) {
	if ( glConfig.renderer == R_RENDERER_ES31 ) {
		const int baseVertex = vertexCache.BaseVertex(cache, stride);
		if ( baseVertex >= 0 ) {
			backEnd.glState.baseVertex = baseVertex;
			return NULL;
		}
	}
	backEnd.glState.baseVertex = 0;
	return (const byte*) vertexCache.Position(cache);
}


#define ATTR_VERTEX     0   // Don't change this, as WebGL require the vertex attrib 0 to be always bound
#define ATTR_COLOR      1
#define ATTR_TEXCOORD   2
#define ATTR_NORMAL     3
#define ATTR_TANGENT    4
#define ATTR_BITANGENT  5

/*
====================
GL_UseProgram
====================
*/
void GL_UseProgram(shaderProgram_t* program) {
	if ( backEnd.glState.currentProgram == program ) {
		return;
	}

	qglUseProgram(program ? program->program : 0);
	backEnd.glState.currentProgram = program;
}

/*
====================
GL_Uniform1fv
====================
*/
static void GL_Uniform1fv(GLint location, const GLfloat* value) {
	if ( GL_UniformChanged(backEnd.glState.currentProgram, location, value, 1) ) {
		qglUniform1fv(*( GLint * )((char*) backEnd.glState.currentProgram + location), 1, value);
	}
}

/*
====================
GL_Uniform4fv
====================
*/
static void GL_Uniform4fv(GLint location, const GLfloat* value) {
	if ( GL_UniformChanged(backEnd.glState.currentProgram, location, value, 4) ) {
		qglUniform4fv(*( GLint * )((char*) backEnd.glState.currentProgram + location), 1, value);
	}
}

/*
====================
GL_UniformMatrix4fv
====================
*/
static void GL_UniformMatrix4fv(GLint location, const GLfloat* value) {
	if ( GL_UniformChanged(backEnd.glState.currentProgram, location, value, 16) ) {
		qglUniformMatrix4fv(*( GLint * )((char*) backEnd.glState.currentProgram + location), 1, GL_FALSE, value);
	}
}

/*
====================
GL_ViewMatricesUniformBuffer
====================
*/
static void GL_ViewMatricesUniformBuffer(const float value[VIEW_MATRICES_FLOATS]) {

	// Update the scene matrices.
	glBindBuffer(GL_UNIFORM_BUFFER, viewMatricesBuffer);
	float* viewMatrices = (float*)glMapBufferRange(
			GL_UNIFORM_BUFFER,
			0,
			VIEW_MATRICES_FLOATS * sizeof(float),
			GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);

	if (viewMatrices == NULL)
	{
		common->Error("View Matrices Uniform Buffer is NULL");
		return;
	}

	memcpy((char*)viewMatrices, value, VIEW_MATRICES_FLOATS * sizeof(float));

	glUnmapBuffer(GL_UNIFORM_BUFFER);
	glBindBufferBase(GL_UNIFORM_BUFFER, VIEW_MATRICES_BINDING, viewMatricesBuffer);
}

/*
====================
RB_GLSL_DepthHack

The weapon and model depth hacks.  GLES2 moves projectionMatrix[14], which
adds its change times eye w to clip z; the shaders here add u_depthHack
times w after the one view-projection matrix, the same thing without a
projection per hack.  A model hack wins over the weapon one, as on GLES2.
====================
*/
static float RB_GLSL_DepthHack(const viewEntity_t* space) {
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
RB_GLSL_SetSpace

The current program's model matrix and depth hack for a space.
====================
*/
static void RB_GLSL_SetSpace(const viewEntity_t* space) {
	const float hack = RB_GLSL_DepthHack(space);
	GL_UniformMatrix4fv(offsetof(shaderProgram_t, modelMatrix), space->modelMatrix);
	GL_Uniform1fv(offsetof(shaderProgram_t, depthHack), &hack);
}

/*
====================
GL_EnableVertexAttribArray
====================
*/
void GL_EnableVertexAttribArray(GLuint index) {
	qglEnableVertexAttribArray(index);
}

/*
====================
GL_DisableVertexAttribArray
====================
*/
void GL_DisableVertexAttribArray(GLuint index) {
	qglDisableVertexAttribArray(index);
}

/*
====================
GL_VertexAttribPointer
====================
*/
static GLint GL_AttribLocation(GLuint index) {
	// the location every program binds the attribute to (R_LoadGLSLShader),
	// whether or not the current one reads it: the pointer is GL state the
	// next program finds in place
	switch ( index ) {
	case offsetof(shaderProgram_t, attr_Vertex):	return ATTR_VERTEX;
	case offsetof(shaderProgram_t, attr_Color):		return ATTR_COLOR;
	case offsetof(shaderProgram_t, attr_TexCoord):	return ATTR_TEXCOORD;
	case offsetof(shaderProgram_t, attr_Normal):	return ATTR_NORMAL;
	case offsetof(shaderProgram_t, attr_Tangent):	return ATTR_TANGENT;
	case offsetof(shaderProgram_t, attr_Bitangent):	return ATTR_BITANGENT;
	}
	return *( GLint * )((char*) backEnd.glState.currentProgram + index);
}

static void GL_VertexAttribPointer(GLuint index, GLint size, GLenum type,
                                   GLboolean normalized, GLsizei stride,
                                   const GLvoid* pointer) {
	const GLint location = GL_AttribLocation(index);
	if ( location < 0 ) {
		return;
	}
	if ( location < RB_ATTRIB_CACHE ) {
		rbAttribPointer_t* a = &rbAttribs[location];
		const GLuint buffer = vertexCache.BoundArray();
		if ( a->valid && a->pointer == pointer && a->buffer == buffer && a->stride == stride
				&& a->type == type && a->size == size && a->normalized == normalized ) {
			return;
		}
		a->pointer = pointer;
		a->buffer = buffer;
		a->stride = stride;
		a->type = type;
		a->size = size;
		a->normalized = normalized;
		a->valid = buffer != idVertexCache::UNKNOWN_BINDING;
	}
	qglVertexAttribPointer(location, size, type, normalized, stride, pointer);
}

/*
=================
Program binaries

The ES 3.1 renderer keeps each linked program's binary in the save
directory, named by a hash of its sources and of the driver, and loads it
instead of compiling when the same driver asks for the same program: a
start without the compile, which on phones is most of the renderer's
start-up.  A binary the driver refuses is rebuilt from the sources.
=================
*/
#define PROGRAM_BINARY_MAGIC	0x42503342u		/* "B3PB" */
#define PROGRAM_BINARY_VERSION	1u

// the sources R_LoadGLSLShader was given for the program being built
static idStr	pendingSource[2];
static bool		pendingSources = false;

static bool R_ProgramBinariesActive(void) {
	return glConfig.renderer == R_RENDERER_ES31 && glCaps.programBinary;
}

static unsigned long long R_ProgramBinaryHash(unsigned long long h, const char* text) {
	while ( text && *text ) {
		h ^= (unsigned char)*text++;
		h *= 1099511628211ull;
	}
	return h;
}

static idStr R_ProgramBinaryPath(const char* name) {
	unsigned long long h = 14695981039346656037ull;
	h = R_ProgramBinaryHash(h, pendingSource[0].c_str());
	h = R_ProgramBinaryHash(h, "\x01");
	h = R_ProgramBinaryHash(h, pendingSource[1].c_str());
	h = R_ProgramBinaryHash(h, (const char*)qglGetString(GL_VENDOR));
	h = R_ProgramBinaryHash(h, (const char*)qglGetString(GL_RENDERER));
	h = R_ProgramBinaryHash(h, (const char*)qglGetString(GL_VERSION));
	return va("glslcache/%s-%08x%08x.bin", name, (unsigned int)( h >> 32 ), (unsigned int)h);
}

static bool R_LoadProgramBinary(shaderProgram_t* shaderProgram, const char* path) {
	void* file = NULL;
	const int len = fileSystem->ReadFile(path, &file, NULL);
	bool ok = false;
	if ( len > 16 && file ) {
		const unsigned int* head = (const unsigned int*)file;
		if ( head[0] == PROGRAM_BINARY_MAGIC && head[1] == PROGRAM_BINARY_VERSION
				&& (int)head[3] == len - 16 ) {
			GLint linked = 0;
			shaderProgram->program = qglCreateProgram();
			glCaps.ProgramBinary(shaderProgram->program, (GLenum)head[2], (const byte*)file + 16, len - 16);
			qglGetProgramiv(shaderProgram->program, GL_LINK_STATUS, &linked);
			if ( linked ) {
				ok = true;
			} else {
				qglDeleteProgram(shaderProgram->program);
				shaderProgram->program = 0;
			}
		}
	}
	if ( file ) {
		fileSystem->FreeFile(file);
	}
	while ( qglGetError() != GL_NO_ERROR ) { }
	return ok;
}

static void R_SaveProgramBinary(const shaderProgram_t* shaderProgram, const char* path) {
	GLint len = 0;
	qglGetProgramiv(shaderProgram->program, GL_PROGRAM_BINARY_LENGTH, &len);
	if ( len <= 0 ) {
		return;
	}
	byte* file = (byte*)Mem_Alloc(len + 16);
	GLsizei got = 0;
	GLenum format = 0;
	glCaps.GetProgramBinary(shaderProgram->program, len, &got, &format, file + 16);
	if ( got > 0 ) {
		unsigned int* head = (unsigned int*)file;
		head[0] = PROGRAM_BINARY_MAGIC;
		head[1] = PROGRAM_BINARY_VERSION;
		head[2] = format;
		head[3] = (unsigned int)got;
		fileSystem->WriteFile(path, file, got + 16);
	}
	Mem_Free(file);
	while ( qglGetError() != GL_NO_ERROR ) { }
}

static void R_CompileGLSLShader(const char* buffer, shaderProgram_t* shaderProgram, GLenum type) {
	switch ( type ) {
	case GL_VERTEX_SHADER:
		shaderProgram->vertexShader = qglCreateShader(GL_VERTEX_SHADER);
		qglShaderSource(shaderProgram->vertexShader, 1, (const GLchar**) &buffer, 0);
		qglCompileShader(shaderProgram->vertexShader);
		break;
	case GL_FRAGMENT_SHADER:
		shaderProgram->fragmentShader = qglCreateShader(GL_FRAGMENT_SHADER);
		qglShaderSource(shaderProgram->fragmentShader, 1, (const GLchar**) &buffer, 0);
		qglCompileShader(shaderProgram->fragmentShader);
		break;
	default:
		common->Printf("R_LoadGLSLShader: no type\n");
		return;
	}
}

/*
=================
R_LoadGLSLShader

loads GLSL vertex or fragment shaders
=================
*/
static void R_LoadGLSLShader(const char* buffer, shaderProgram_t* shaderProgram, GLenum type) {
	if ( !glConfig.isInitialized ) {
		return;
	}

	idStr source(buffer);
	// ES wants #version before anything, newlines included
	source.StripLeading('\n');
	if ( type == GL_VERTEX_SHADER && glslMultiview ) {
		// #extension has to come before any code, so go straight after #version
		const int eol = source.Find('\n', source.Find("#version"));
		source.Insert("#define BOOM3_MULTIVIEW\n", eol + 1);
	}
	R_GLSLSourceForContext( source, type );
	if ( R_ProgramBinariesActive() && ( type == GL_VERTEX_SHADER || type == GL_FRAGMENT_SHADER ) ) {
		// compiled at link, and only when no binary of the program is kept
		pendingSource[type == GL_VERTEX_SHADER ? 0 : 1] = source;
		pendingSources = true;
		return;
	}
	R_CompileGLSLShader(source.c_str(), shaderProgram, type);
}

/*
=================
R_LinkGLSLShader

links the GLSL vertex and fragment shaders together to form a GLSL program
=================
*/
static bool R_LinkGLSLShader(shaderProgram_t* shaderProgram, const char* name) {
	char buf[BUFSIZ];
	int len;
	GLint linked;
	idStr binaryPath;

	if ( pendingSources ) {
		pendingSources = false;
		binaryPath = R_ProgramBinaryPath(name);
		if ( R_LoadProgramBinary(shaderProgram, binaryPath.c_str()) ) {
			return true;
		}
		R_CompileGLSLShader(pendingSource[0].c_str(), shaderProgram, GL_VERTEX_SHADER);
		R_CompileGLSLShader(pendingSource[1].c_str(), shaderProgram, GL_FRAGMENT_SHADER);
	}

	shaderProgram->program = qglCreateProgram();
	if ( binaryPath.Length() ) {
		glCaps.ProgramParameteri(shaderProgram->program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
	}

	qglAttachShader(shaderProgram->program, shaderProgram->vertexShader);
	qglAttachShader(shaderProgram->program, shaderProgram->fragmentShader);

	// Bind attributes locations
	qglBindAttribLocation(shaderProgram->program, ATTR_VERTEX, "attr_Vertex");
	qglBindAttribLocation(shaderProgram->program, ATTR_COLOR, "attr_Color");
	qglBindAttribLocation(shaderProgram->program, ATTR_TEXCOORD, "attr_TexCoord");
	qglBindAttribLocation(shaderProgram->program, ATTR_NORMAL, "attr_Normal");
	qglBindAttribLocation(shaderProgram->program, ATTR_TANGENT, "attr_Tangent");
	qglBindAttribLocation(shaderProgram->program, ATTR_BITANGENT, "attr_Bitangent");

	qglLinkProgram(shaderProgram->program);

	qglGetProgramiv(shaderProgram->program, GL_LINK_STATUS, &linked);

	if ( com_developer.GetBool()) {
		qglGetShaderInfoLog(shaderProgram->vertexShader, sizeof(buf), &len, buf);
		common->Printf("VS:\n%.*s\n", len, buf);
		qglGetShaderInfoLog(shaderProgram->fragmentShader, sizeof(buf), &len, buf);
		common->Printf("FS:\n%.*s\n", len, buf);
	}

	if ( !linked ) {
		common->Error("R_LinkGLSLShader: program failed to link: %s\n", name);
		return false;
	}

	if ( binaryPath.Length() ) {
		R_SaveProgramBinary(shaderProgram, binaryPath.c_str());
	}
	return true;
}

/*
=================
R_ValidateGLSLProgram

makes sure GLSL program is valid
=================
*/
static bool R_ValidateGLSLProgram(shaderProgram_t* shaderProgram) {
	GLint validProgram;

	qglValidateProgram(shaderProgram->program);

	qglGetProgramiv(shaderProgram->program, GL_VALIDATE_STATUS, &validProgram);

	if ( !validProgram ) {
		common->Printf("R_ValidateGLSLProgram: program invalid\n");
		return false;
	}

	return true;
}

/*
=================
RB_GLSL_GetUniformLocations

=================
*/
static void RB_GLSL_GetUniformLocations(shaderProgram_t* shader) {
	int i;
	char buffer[32];

	GL_UseProgram(shader);

	shader->localLightOrigin = qglGetUniformLocation(shader->program, "u_lightOrigin");

	//May need to move this to the shader matrices uniform block?
	shader->localViewOrigin = qglGetUniformLocation(shader->program, "u_viewOrigin");
	shader->lightProjection = qglGetUniformLocation(shader->program, "u_lightProjection");
	shader->bumpMatrixS = qglGetUniformLocation(shader->program, "u_bumpMatrixS");
	shader->bumpMatrixT = qglGetUniformLocation(shader->program, "u_bumpMatrixT");
	shader->diffuseMatrixS = qglGetUniformLocation(shader->program, "u_diffuseMatrixS");
	shader->diffuseMatrixT = qglGetUniformLocation(shader->program, "u_diffuseMatrixT");
	shader->specularMatrixS = qglGetUniformLocation(shader->program, "u_specularMatrixS");
	shader->specularMatrixT = qglGetUniformLocation(shader->program, "u_specularMatrixT");
	shader->colorModulate = qglGetUniformLocation(shader->program, "u_colorModulate");
	shader->colorAdd = qglGetUniformLocation(shader->program, "u_colorAdd");
	shader->fogColor = qglGetUniformLocation(shader->program, "u_fogColor");
	shader->diffuseColor = qglGetUniformLocation(shader->program, "u_diffuseColor");
	shader->specularColor = qglGetUniformLocation(shader->program, "u_specularColor");
	shader->glColor = qglGetUniformLocation(shader->program, "u_glColor");
	shader->alphaTest = qglGetUniformLocation(shader->program, "u_alphaTest");
	shader->specularExponent = qglGetUniformLocation(shader->program, "u_specularExponent");

	shader->modelMatrix = qglGetUniformLocation(shader->program, "u_modelMatrix");

	shader->depthHack = qglGetUniformLocation(shader->program, "u_depthHack");
	shader->hdrParms = qglGetUniformLocation(shader->program, "u_hdrParms");

	// every program reads its view-projection matrices from one binding point
	const GLuint viewMatricesBlock = glGetUniformBlockIndex(shader->program, "ViewMatrices");
	if ( viewMatricesBlock != GL_INVALID_INDEX ) {
		glUniformBlockBinding(shader->program, viewMatricesBlock, VIEW_MATRICES_BINDING);
	}

	shader->modelViewMatrix = qglGetUniformLocation(shader->program, "u_modelViewMatrix");
	shader->textureMatrix = qglGetUniformLocation(shader->program, "u_textureMatrix");
	shader->clipPlane = qglGetUniformLocation(shader->program, "u_clipPlane");
	shader->fogMatrix = qglGetUniformLocation(shader->program, "u_fogMatrix");
	shader->projectionMatrix = qglGetUniformLocation(shader->program, "u_projectionMatrix");
	shader->vertexParms = qglGetUniformLocation(shader->program, "u_vertexParms");
	shader->screenParms = qglGetUniformLocation(shader->program, "u_screenParms");

	shader->attr_TexCoord = qglGetAttribLocation(shader->program, "attr_TexCoord");
	shader->attr_Tangent = qglGetAttribLocation(shader->program, "attr_Tangent");
	shader->attr_Bitangent = qglGetAttribLocation(shader->program, "attr_Bitangent");
	shader->attr_Normal = qglGetAttribLocation(shader->program, "attr_Normal");
	shader->attr_Vertex = qglGetAttribLocation(shader->program, "attr_Vertex");
	shader->attr_Color = qglGetAttribLocation(shader->program, "attr_Color");

	// Init default values
	for ( i = 0; i < MAX_FRAGMENT_IMAGES; i++ ) {
		idStr::snPrintf(buffer, sizeof(buffer), "u_fragmentMap%d", i);
		shader->u_fragmentMap[i] = qglGetUniformLocation(shader->program, buffer);
		qglUniform1i(shader->u_fragmentMap[i], i);
	}

	for ( i = 0; i < MAX_FRAGMENT_IMAGES; i++ ) {
		idStr::snPrintf(buffer, sizeof(buffer), "u_fragmentCubeMap%d", i);
		shader->u_fragmentCubeMap[i] = qglGetUniformLocation(shader->program, buffer);
		qglUniform1i(shader->u_fragmentCubeMap[i], i);
	}

	if (shader->textureMatrix >= 0) {
		// Load identity matrix for Texture marix
		GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), mat4_identity.ToFloatPtr());
	}

	static const GLfloat one[1] = { 1.0f };
	if (shader->alphaTest >= 0) {
		// Alpha test always pass by default
		GL_Uniform1fv(offsetof(shaderProgram_t, alphaTest), one);
	}

	if (shader->colorModulate >= 0) {
		static const GLfloat zero[1] = { 0.0f };
		GL_Uniform1fv(offsetof(shaderProgram_t, colorModulate), zero);
	}

	if (shader->colorAdd >= 0) {
		GL_Uniform1fv(offsetof(shaderProgram_t, colorAdd), one);
	}

	GL_CheckErrors();

	GL_UseProgram(NULL);
}

/*
=================
R_LoadInteractionShader

An interaction program's stage, its per-draw uniforms moved into the stage's
block when the programs are built with them.
=================
*/
static void R_LoadInteractionShader(const char* buffer, shaderProgram_t* shaderProgram, GLenum type) {
	if ( !glslDrawBlocks ) {
		R_LoadGLSLShader(buffer, shaderProgram, type);
		return;
	}
	char* src = type == GL_VERTEX_SHADER
		? glsl_uniforms_to_block(buffer, "DrawVertex", drawVertexNames, DV_COUNT)
		: glsl_uniforms_to_block(buffer, "DrawFragment", drawFragmentNames, DF_COUNT);
	if ( !src ) {
		common->FatalError("R_LoadInteractionShader: out of memory");
	}
	R_LoadGLSLShader(src, shaderProgram, type);
	free(src);
}

/*
=================
RB_GLSL_BlockMembers

The block's binding and size, and where its members sit; false when the
block is laid out other than the draws write it.
=================
*/
static bool RB_GLSL_BlockMembers(GLuint program, const char* blockName, GLuint binding,
		const char* const* names, int count, GLint* offsets, GLint* size) {
	GLuint indexes[DV_COUNT];
	int i;

	*size = 0;
	for ( i = 0; i < count; i++ ) {
		offsets[i] = -1;
	}
	const GLuint block = glCaps.GetUniformBlockIndex(program, blockName);
	if ( block == GL_INVALID_INDEX ) {
		return true;
	}
	glCaps.UniformBlockBinding(program, block, binding);
	glCaps.GetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, size);

	glCaps.GetUniformIndices(program, count, names, indexes);
	for ( i = 0; i < count; i++ ) {
		GLint v;
		if ( indexes[i] == GL_INVALID_INDEX ) {
			continue;
		}
		glCaps.GetActiveUniformsiv(program, 1, &indexes[i], GL_UNIFORM_OFFSET, &v);
		offsets[i] = v;
		// matrices are written the way GL_FALSE uploads read them:
		// four columns, 16 bytes apart
		glCaps.GetActiveUniformsiv(program, 1, &indexes[i], GL_UNIFORM_TYPE, &v);
		if ( v == GL_FLOAT_MAT4 ) {
			glCaps.GetActiveUniformsiv(program, 1, &indexes[i], GL_UNIFORM_MATRIX_STRIDE, &v);
			if ( v != 16 ) {
				return false;
			}
			glCaps.GetActiveUniformsiv(program, 1, &indexes[i], GL_UNIFORM_IS_ROW_MAJOR, &v);
			if ( v ) {
				return false;
			}
		}
	}
	return true;
}

/*
=================
RB_GLSL_SetupDrawBlocks
=================
*/
static void RB_GLSL_SetupDrawBlocks(shaderProgram_t* shader) {
	if ( !glslDrawBlocks || numDrawBlocks == (int)( sizeof(drawBlocks) / sizeof(drawBlocks[0]) ) ) {
		return;
	}
	drawBlocks_t* b = &drawBlocks[numDrawBlocks];
	if ( !RB_GLSL_BlockMembers(shader->program, "DrawVertex", DRAW_VERTEX_BINDING,
				drawVertexNames, DV_COUNT, b->vertexOffset, &b->vertexSize)
			|| !RB_GLSL_BlockMembers(shader->program, "DrawFragment", DRAW_FRAGMENT_BINDING,
				drawFragmentNames, DF_COUNT, b->fragmentOffset, &b->fragmentSize) ) {
		common->Error("RB_GLSL_SetupDrawBlocks: unexpected uniform block layout");
		return;
	}
	b->program = shader;
	numDrawBlocks++;
}

static const drawBlocks_t* RB_GLSL_DrawBlocks(const shaderProgram_t* shader) {
	int i;
	for ( i = 0; i < numDrawBlocks; i++ ) {
		if ( drawBlocks[i].program == shader ) {
			return &drawBlocks[i];
		}
	}
	return NULL;
}

/*
=================
RB_GLSL_InitShaders

=================
*/
static void R_FreeGLSLShader(shaderProgram_t* shaderProgram) {
	if ( shaderProgram->program ) {
		qglDeleteProgram(shaderProgram->program);
	}
	if ( shaderProgram->vertexShader ) {
		qglDeleteShader(shaderProgram->vertexShader);
	}
	if ( shaderProgram->fragmentShader ) {
		qglDeleteShader(shaderProgram->fragmentShader);
	}
	memset(shaderProgram, 0, sizeof(shaderProgram_t));
}

/*
=================
Run programs

The single-draw vertex program with its model matrix, depth hack and
(shadows) light origin read from the run's array at the draw's index.
NULL when the program does not declare them as expected.
=================
*/
static const char rbDrawList[] =
	"struct rbDraw_t { highp mat4 modelMatrix; highp vec4 lightOrigin; highp vec4 depthHack; };\n"
	"layout(std140) uniform DrawList { rbDraw_t rbDraws[128]; };\n"
	"#define u_modelMatrix rbDraws[RB_DRAW_ID].modelMatrix\n"
	"#define u_depthHack rbDraws[RB_DRAW_ID].depthHack.x\n";

static bool R_RunSource(const char* es, bool shadow, idStr& out) {
	static const char depthHack[] = "uniform highp float u_depthHack;\n";
	static const char modelMatrix[] = "uniform highp mat4 u_modelMatrix;\n";
	static const char lightOrigin[] = "uniform vec4 u_lightOrigin;\n";
	const char* drawId;
	int eol;

	out = es;
	out.StripLeading('\n');
	if ( out.Find(depthHack) < 0 || out.Find(modelMatrix) < 0 || ( shadow && out.Find(lightOrigin) < 0 ) ) {
		return false;
	}
	out.Replace(depthHack, rbDrawList);
	out.Replace(modelMatrix, "");
	if ( shadow ) {
		out.Replace(lightOrigin, "#define u_lightOrigin rbDraws[RB_DRAW_ID].lightOrigin\n");
	}
	R_GLSLSourceForContext(out, GL_VERTEX_SHADER);
	if ( idStr::Cmpn(out.c_str(), "#version 330 core\n", 18) != 0 ) {
		return false;
	}
	drawId = gl_caps_has_extension("GL_ARB_shader_draw_parameters")
		? "#version 430 core\n#extension GL_ARB_shader_draw_parameters : require\n#define RB_DRAW_ID gl_DrawIDARB\n"
		: "#version 460 core\n#define RB_DRAW_ID gl_DrawID\n";
	eol = out.Find('\n');
	out = idStr(drawId) + out.Right(out.Length() - eol - 1);
	return true;
}

static GLuint R_CompileRunShader(GLenum type, const char* source) {
	char log[1024];
	GLsizei len = 0;
	GLint ok = 0;
	const GLuint shader = qglCreateShader(type);

	qglShaderSource(shader, 1, (const GLchar**) &source, 0);
	qglCompileShader(shader);
	qglGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if ( !ok ) {
		qglGetShaderInfoLog(shader, sizeof(log), &len, log);
		common->Printf("run program: %.*s\n", (int)len, log);
		qglDeleteShader(shader);
		return 0;
	}
	return shader;
}

static bool R_BuildRunProgram(shaderProgram_t* program, const char* vertexES, const char* fragmentES, bool shadow) {
	idStr vertex, fragment;
	GLint linked = 0;
	GLuint block;

	R_FreeGLSLShader(program);
	if ( !R_RunSource(vertexES, shadow, vertex) ) {
		return false;
	}
	fragment = fragmentES;
	fragment.StripLeading('\n');
	R_GLSLSourceForContext(fragment, GL_FRAGMENT_SHADER);

	program->vertexShader = R_CompileRunShader(GL_VERTEX_SHADER, vertex.c_str());
	program->fragmentShader = R_CompileRunShader(GL_FRAGMENT_SHADER, fragment.c_str());
	if ( !program->vertexShader || !program->fragmentShader ) {
		R_FreeGLSLShader(program);
		return false;
	}
	program->program = qglCreateProgram();
	qglAttachShader(program->program, program->vertexShader);
	qglAttachShader(program->program, program->fragmentShader);
	qglBindAttribLocation(program->program, ATTR_VERTEX, "attr_Vertex");
	qglBindAttribLocation(program->program, ATTR_TEXCOORD, "attr_TexCoord");
	qglLinkProgram(program->program);
	qglGetProgramiv(program->program, GL_LINK_STATUS, &linked);
	block = linked ? glGetUniformBlockIndex(program->program, "DrawList") : GL_INVALID_INDEX;
	if ( block == GL_INVALID_INDEX ) {
		R_FreeGLSLShader(program);
		return false;
	}
	glUniformBlockBinding(program->program, block, DRAW_LIST_BINDING);
	RB_GLSL_GetUniformLocations(program);
	return true;
}

/*
=================
RB_GLSL_InitRunShaders

The run programs, on the ES 3.1 renderer over a desktop GL 4.3+ context
with shader draw parameters; a driver that will not build them draws every
surface on its own.
=================
*/
static void RB_GLSL_InitRunShaders(void) {
	static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	glslBatches = false;
	R_FreeGLSLShader(&zfillOpaqueBatchShader);
	R_FreeGLSLShader(&stencilShadowBatchShader);
	if ( glConfig.renderer != R_RENDERER_ES31 || glCaps.es || !glCaps.multiDrawIndirect || glslMultiview
			|| glCaps.maxUniformBlockSize < RB_BATCH_DRAWS * RB_BATCH_ENTRY ) {
		return;
	}
	if ( !R_BuildRunProgram(&zfillOpaqueBatchShader, zfillShaderVP, zfillOpaqueShaderFP, false)
			|| !R_BuildRunProgram(&stencilShadowBatchShader, stencilShadowShaderVP, stencilShadowShaderFP, true) ) {
		common->Printf("Draw runs unavailable: their programs did not build\n");
		R_FreeGLSLShader(&zfillOpaqueBatchShader);
		R_FreeGLSLShader(&stencilShadowBatchShader);
		return;
	}
	// the depth fill writes black, as the single-draw program does for these surfaces
	GL_UseProgram(&zfillOpaqueBatchShader);
	GL_Uniform4fv(offsetof(shaderProgram_t, glColor), black);
	GL_UseProgram(NULL);
	glslBatches = true;
	common->Printf("Draw runs: depth fill and shadow volumes through glMultiDrawElementsIndirect\n");
}

static bool RB_GLSL_InitShaders(void) {

	if ( backEnd.glState.currentProgram ) {
		GL_UseProgram(NULL);
	}

	// the ES 3.1 renderer's interaction programs take their per-draw
	// values from uniform blocks
	glslDrawBlocks = glConfig.renderer == R_RENDERER_ES31;
	numDrawBlocks = 0;

	// the view-projection matrices of both views, rewritten once per view
	if ( !viewMatricesBuffer ) {
		qglGenBuffers(1, &viewMatricesBuffer);
		glBindBuffer(GL_UNIFORM_BUFFER, viewMatricesBuffer);
		glBufferData(GL_UNIFORM_BUFFER, VIEW_MATRICES_FLOATS * sizeof(float), NULL, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
	}

	// Blinn-Phong interaction shader, the r_usePhong 0 one, with GLES2's
	// load-time substitutions: off, each leaves exactly the stock shader
	common->Printf("Loading Blinn interaction shader\n");
	R_FreeGLSLShader(&interactionBlinnShader);

	R_LoadInteractionShader(interactionShaderVP, &interactionBlinnShader, GL_VERTEX_SHADER);
	{
		idStr fp( interactionBlinnShaderFP );

		/* Toksvig specular antialiasing: N is deliberately unnormalised,
		   so its length carries the mip variance; 12 is the exponent this
		   shader hardcodes. */
		if ( r_specularAA.GetBool() ) {
			const char *tokStock = "  float NdotH2 = NdotH * NdotH;\n  float NdotH4 = NdotH2 * NdotH2;\n  float specularFalloff = NdotH4 * NdotH4 * NdotH4;";
			if ( fp.Find( tokStock ) != -1 ) {
				fp.Replace( tokStock,
					"  float tkLen = length(N);\n"
					"  float tkFt = tkLen / (tkLen + 12.0 * (1.0 - tkLen));\n"
					"  float specularFalloff = pow(max(NdotH, 0.0), 12.0 * tkFt);" );
			} else {
				common->Warning( "r_specularAA: GLES3 falloff lines not found; shader left stock" );
			}
		}
		/* Pseudo-spectral mixing: a basis change either side of the
		   multiply by the light's colour, as on GLES2. */
		const char *stockLine = "fragColor = vec4(dhewm3Epilogue(color), 1.0) * var_Color;";
		if ( r_spectralMix.GetBool() && fp.Find( stockLine ) != -1 ) {
			idStr mix( "vec3 smBase = dhewm3Epilogue(color);\n" );
			mix += va( "  mat3 smM = mat3(%.7f, %.7f, %.7f, %.7f, %.7f, %.7f, %.7f, %.7f, %.7f);\n",
					spectral_mix_forward[0][0], spectral_mix_forward[1][0], spectral_mix_forward[2][0],
					spectral_mix_forward[0][1], spectral_mix_forward[1][1], spectral_mix_forward[2][1],
					spectral_mix_forward[0][2], spectral_mix_forward[1][2], spectral_mix_forward[2][2] );
			mix += va( "  mat3 smN = mat3(%.7f, %.7f, %.7f, %.7f, %.7f, %.7f, %.7f, %.7f, %.7f);\n",
					spectral_mix_inverse[0][0], spectral_mix_inverse[1][0], spectral_mix_inverse[2][0],
					spectral_mix_inverse[0][1], spectral_mix_inverse[1][1], spectral_mix_inverse[2][1],
					spectral_mix_inverse[0][2], spectral_mix_inverse[1][2], spectral_mix_inverse[2][2] );
			mix += "  fragColor = vec4(smN * ((smM * smBase) * (smM * var_Color.rgb)), 1.0);\n";
			mix += "  fragColor.a = var_Color.a;";
			fp.Replace( stockLine, mix.c_str() );
		}
		R_LoadInteractionShader( fp.c_str(), &interactionBlinnShader, GL_FRAGMENT_SHADER );
	}

	if ( !R_LinkGLSLShader(&interactionBlinnShader, "interactionBlinn") && !R_ValidateGLSLProgram(&interactionBlinnShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&interactionBlinnShader);
		RB_GLSL_SetupDrawBlocks(&interactionBlinnShader);
	}

	// PBR interaction shader (r_usePBR 1 and 2)
	common->Printf("Loading PBR interaction shader\n");
	R_FreeGLSLShader(&interactionShader);

	R_LoadInteractionShader(interactionShaderVP, &interactionShader, GL_VERTEX_SHADER);
	R_LoadInteractionShader(interactionShaderFP, &interactionShader, GL_FRAGMENT_SHADER);

	if ( !R_LinkGLSLShader(&interactionShader, "interaction") && !R_ValidateGLSLProgram(&interactionShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&interactionShader);
		RB_GLSL_SetupDrawBlocks(&interactionShader);
	}

	// main Interaction shader, Phong version
	common->Printf("Loading main interaction shader (Phong) \n");
	R_FreeGLSLShader(&interactionPhongShader);

	R_LoadInteractionShader(interactionPhongShaderVP, &interactionPhongShader, GL_VERTEX_SHADER);
	R_LoadInteractionShader(interactionPhongShaderFP, &interactionPhongShader, GL_FRAGMENT_SHADER);

	if ( !R_LinkGLSLShader(&interactionPhongShader, "interactionPhong") &&
	        !R_ValidateGLSLProgram(&interactionPhongShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&interactionPhongShader);
		RB_GLSL_SetupDrawBlocks(&interactionPhongShader);
	}

	// default diffuse shader
	common->Printf("Loading default diffuse shader\n");
	R_FreeGLSLShader(&diffuseMapShader);

	R_LoadGLSLShader(diffuseMapShaderVP, &diffuseMapShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(diffuseMapShaderFP, &diffuseMapShader, GL_FRAGMENT_SHADER);

	if ( !R_LinkGLSLShader(&diffuseMapShader, "diffuseMap") && !R_ValidateGLSLProgram(&diffuseMapShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&diffuseMapShader);
	}

	// the diffuse shader with the GUI's output-transform inverse on the texel
	common->Printf("Loading GUI diffuse shader\n");
	R_FreeGLSLShader(&diffuseMapGuiShader);

	R_LoadGLSLShader(diffuseMapShaderVP, &diffuseMapGuiShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(diffuseMapGuiShaderFP, &diffuseMapGuiShader, GL_FRAGMENT_SHADER);

	if ( !R_LinkGLSLShader(&diffuseMapGuiShader, "diffuseMapGui") && !R_ValidateGLSLProgram(&diffuseMapGuiShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&diffuseMapGuiShader);
	}

	// Skybox cubemap shader
	common->Printf("Loading skybox cubemap shader\n");
	R_FreeGLSLShader(&skyboxCubeShader);

	R_LoadGLSLShader(skyboxCubeShaderVP, &skyboxCubeShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(cubeMapShaderFP, &skyboxCubeShader, GL_FRAGMENT_SHADER);   // Use the common "cubeMapShaderFP"

	if ( !R_LinkGLSLShader(&skyboxCubeShader, "skyboxCube") && !R_ValidateGLSLProgram(&skyboxCubeShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&skyboxCubeShader);
	}

	// Reflection cubemap shader
	common->Printf("Loading reflection cubemap shader\n");
	R_FreeGLSLShader(&reflectionCubeShader);

	R_LoadGLSLShader(reflectionCubeShaderVP, &reflectionCubeShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(cubeMapShaderFP, &reflectionCubeShader, GL_FRAGMENT_SHADER); // Use the common "cubeMapShaderFP"

	if ( !R_LinkGLSLShader(&reflectionCubeShader, "reflectionCube") &&
	        !R_ValidateGLSLProgram(&reflectionCubeShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&reflectionCubeShader);
	}

	// Diffuse cubemap shader
	common->Printf("Loading diffuse cubemap shader\n");
	R_FreeGLSLShader(&diffuseCubeShader);

	R_LoadGLSLShader(diffuseCubeShaderVP, &diffuseCubeShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(cubeMapShaderFP, &diffuseCubeShader, GL_FRAGMENT_SHADER); // Use the common "cubeMapShaderFP"

	if ( !R_LinkGLSLShader(&diffuseCubeShader, "diffuseCube") && !R_ValidateGLSLProgram(&diffuseCubeShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&diffuseCubeShader);
	}

	// Z Fill shader
	common->Printf("Loading Zfill shader\n");
	R_FreeGLSLShader(&zfillShader);

	R_LoadGLSLShader(zfillShaderVP, &zfillShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(zfillShaderFP, &zfillShader, GL_FRAGMENT_SHADER);

	if ( !R_LinkGLSLShader(&zfillShader, "zfill") && !R_ValidateGLSLProgram(&zfillShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&zfillShader);
	}

	// Z Fill shader, opaque surfaces: no texture, no discard
	common->Printf("Loading Zfill shader (opaque)\n");
	R_FreeGLSLShader(&zfillOpaqueShader);

	R_LoadGLSLShader(zfillShaderVP, &zfillOpaqueShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(zfillOpaqueShaderFP, &zfillOpaqueShader, GL_FRAGMENT_SHADER);

	if ( !R_LinkGLSLShader(&zfillOpaqueShader, "zfillOpaque") && !R_ValidateGLSLProgram(&zfillOpaqueShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&zfillOpaqueShader);
	}

	// Z Fill shader, Clip planes version
	common->Printf("Loading Zfill shader (Clip plane version)\n");
	R_FreeGLSLShader(&zfillClipShader);

	R_LoadGLSLShader(zfillClipShaderVP, &zfillClipShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(zfillClipShaderFP, &zfillClipShader, GL_FRAGMENT_SHADER);

	if ( !R_LinkGLSLShader(&zfillClipShader, "zfillClip") && !R_ValidateGLSLProgram(&zfillClipShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&zfillClipShader);
	}

	// Fog shader
	common->Printf("Loading Fog shader\n");
	R_FreeGLSLShader(&fogShader);

	R_LoadGLSLShader(fogShaderVP, &fogShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(fogShaderFP, &fogShader, GL_FRAGMENT_SHADER);

	if ( !R_LinkGLSLShader(&fogShader, "fog") && !R_ValidateGLSLProgram(&fogShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&fogShader);
	}

	// BlendLight shader
	common->Printf("Loading BlendLight shader\n");
	R_FreeGLSLShader(&blendLightShader);

	R_LoadGLSLShader(blendLightShaderVP, &blendLightShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(fogShaderFP, &blendLightShader, GL_FRAGMENT_SHADER);       // Reuse the common "FogShaderFP"

	if ( !R_LinkGLSLShader(&blendLightShader, "blendLight") && !R_ValidateGLSLProgram(&blendLightShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&blendLightShader);
	}

	// Stencil shadow shader
	common->Printf("Loading Stencil shadow shader\n");
	R_FreeGLSLShader(&stencilShadowShader);

	R_LoadGLSLShader(stencilShadowShaderVP, &stencilShadowShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(stencilShadowShaderFP, &stencilShadowShader, GL_FRAGMENT_SHADER);

	if ( !R_LinkGLSLShader(&stencilShadowShader, "stencilShadow") && !R_ValidateGLSLProgram(&stencilShadowShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&stencilShadowShader);
	}

	// soft particles: the diffuse vertex program with the softening fragment program
	common->Printf("Loading soft particle shader\n");
	R_FreeGLSLShader(&softParticleShader);
	R_LoadGLSLShader(diffuseMapShaderVP, &softParticleShader, GL_VERTEX_SHADER);
	R_LoadGLSLShader(softParticleShaderFP, &softParticleShader, GL_FRAGMENT_SHADER);
	if ( !R_LinkGLSLShader(&softParticleShader, "softParticle") && !R_ValidateGLSLProgram(&softParticleShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&softParticleShader);
	}

	// the custom stage programs
	{
		static const struct { int id; const char* name; const char* const* vp; const char* const* fp; } stages[] = {
			{ GLSL_STAGE_HEAT_HAZE, "heatHaze", &heatHazeShaderVP, &heatHazeShaderFP },
			{ GLSL_STAGE_HEAT_HAZE_MASK, "heatHazeMask", &heatHazeShaderVP, &heatHazeMaskShaderFP },
			{ GLSL_STAGE_HEAT_HAZE_MASK_VERTEX, "heatHazeMaskVertex", &heatHazeShaderVP, &heatHazeMaskVertexShaderFP },
			{ GLSL_STAGE_COLOR_PROCESS, "colorProcess", &colorProcessShaderVP, &colorProcessShaderFP },
			{ GLSL_STAGE_BUMPY_ENVIRONMENT, "bumpyEnvironment", &bumpyEnvironmentShaderVP, &bumpyEnvironmentShaderFP },
		};
		int i;
		for ( i = 0; i < (int)( sizeof( stages ) / sizeof( stages[0] ) ); i++ ) {
			shaderProgram_t* const program = &stageShaders[stages[i].id];
			common->Printf("Loading %s stage shader\n", stages[i].name);
			R_FreeGLSLShader(program);
			R_LoadGLSLShader(*stages[i].vp, program, GL_VERTEX_SHADER);
			R_LoadGLSLShader(*stages[i].fp, program, GL_FRAGMENT_SHADER);
			if ( !R_LinkGLSLShader(program, stages[i].name) && !R_ValidateGLSLProgram(program)) {
				return false;
			}
			RB_GLSL_GetUniformLocations(program);
		}
	}

	RB_GLSL_InitRunShaders();

	return true;
}

/*
==================
R_ReloadGLSLPrograms_f
==================
*/
void R_ReloadGLSLPrograms_f(const idCmdArgs& args) {
	common->Printf("----- R_ReloadGLSLPrograms -----\n");

	// R_InitOpenGL calls this with no arguments, also after a context reset,
	// where every old handle is stale and may already name something new:
	// drop them rather than delete them.  From the console (the command name
	// is argument 0) the context is the same one, and RB_GLSL_InitShaders
	// frees each program before it rebuilds it.
	if ( args.Argc() == 0 ) {
		shaderProgram_t* const programs[] = {
			&interactionShader, &interactionBlinnShader, &interactionPhongShader,
			&fogShader, &blendLightShader, &zfillShader, &zfillOpaqueShader, &zfillClipShader,
			&diffuseMapShader, &diffuseMapGuiShader, &diffuseCubeShader, &skyboxCubeShader,
			&reflectionCubeShader, &stencilShadowShader
		};
		int i;
		for ( i = 0; i < (int)(sizeof(programs) / sizeof(programs[0])); i++ ) {
			memset(programs[i], 0, sizeof(shaderProgram_t));
		}
		memset(stageShaders, 0, sizeof(stageShaders));
		memset(&softParticleShader, 0, sizeof(softParticleShader));
		depthCopyFbo = 0;
		depthBlitRefused = false;
		viewMatricesBuffer = 0;
		drawBlockFallback[0] = drawBlockFallback[1] = 0;
	}
	backEnd.glState.currentProgram = NULL;

	if ( !RB_GLSL_InitShaders()) {
		common->Printf("GLSL shaders failed to init.\n");
	}

	common->Printf("-------------------------------\n");
}

/*
==================
RB_GLSL_HDRParms

u_hdrParms for the interaction programs: RB_GLSLEpilogueParms, with the
encoding fold in.
==================
*/
static void RB_GLSL_HDRParms(float hp[4]) {
	RB_GLSLEpilogueParms( false, hp );
}

/*
==================
RB_GLSL_DrawBlockMember
==================
*/
static ID_INLINE void RB_GLSL_DrawBlockMember(byte* block, GLint offset, const float* value, int count) {
	if ( offset >= 0 ) {
		memcpy(block + offset, value, count * sizeof(float));
	}
}

/*
==================
RB_GLSL_BindDrawBlock

size bytes of block data for the binding: from the ring, else rewritten in
a buffer of its own.  Returns where to write them, valid until the draw.
==================
*/
static byte* RB_GLSL_BindDrawBlock(GLuint binding, int slot, GLint size, byte* scratch) {
	GLuint buffer;
	int offset;
	byte* p = (byte*) vertexCache.RingReserve(size, glCaps.uniformBufferAlign, &buffer, &offset);
	if ( p ) {
		glCaps.BindBufferRange(GL_UNIFORM_BUFFER, binding, buffer, offset, size);
		return p;
	}
	if ( !drawBlockFallback[slot] ) {
		qglGenBuffers(1, &drawBlockFallback[slot]);
		glBindBuffer(GL_UNIFORM_BUFFER, drawBlockFallback[slot]);
		glBufferData(GL_UNIFORM_BUFFER, glCaps.maxUniformBlockSize < 4096 ? glCaps.maxUniformBlockSize : 4096,
				NULL, GL_STREAM_DRAW);
	}
	glCaps.BindBufferBase(GL_UNIFORM_BUFFER, binding, drawBlockFallback[slot]);
	return scratch;
}

/*
==================
RB_GLSL_DrawInteractionBlocks

The ES 3.1 renderer's interaction draw: the values RB_GLSL_DrawInteraction
uploads, in the program's blocks.
==================
*/
static void RB_GLSL_DrawInteractionBlocks(const drawInteraction_t* din, const drawBlocks_t* blocks) {
	static const float colorIgnore[2] = { 0.0f, 1.0f };
	static const float colorModulate[2] = { 1 / 255.0f, 0.0f };
	static const float colorInverse[2] = { -1.0f / 255.0f, 1.0f };
	static byte scratch[2][4096];
	const viewEntity_t* space = din->surf->space;
	const float depthHack = RB_GLSL_DepthHack(space);
	const float* color;
	byte* v;
	byte* f;

	switch ( din->vertexColor ) {
	case SVC_MODULATE:
		color = colorModulate;
		break;
	case SVC_INVERSE_MODULATE:
		color = colorInverse;
		break;
	default:
		color = colorIgnore;
		break;
	}

	v = RB_GLSL_BindDrawBlock(DRAW_VERTEX_BINDING, 0, blocks->vertexSize, scratch[0]);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_MODEL_MATRIX], space->modelMatrix, 16);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_DEPTH_HACK], &depthHack, 1);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_LIGHT_PROJECTION], din->lightProjection.ToFloatPtr(), 16);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_COLOR_MODULATE], &color[0], 1);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_COLOR_ADD], &color[1], 1);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_LIGHT_ORIGIN], din->localLightOrigin.ToFloatPtr(), 4);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_VIEW_ORIGIN], din->localViewOrigin.ToFloatPtr(), 4);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_BUMP_S], din->bumpMatrix[0].ToFloatPtr(), 4);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_BUMP_T], din->bumpMatrix[1].ToFloatPtr(), 4);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_DIFFUSE_S], din->diffuseMatrix[0].ToFloatPtr(), 4);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_DIFFUSE_T], din->diffuseMatrix[1].ToFloatPtr(), 4);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_SPECULAR_S], din->specularMatrix[0].ToFloatPtr(), 4);
	RB_GLSL_DrawBlockMember(v, blocks->vertexOffset[DV_SPECULAR_T], din->specularMatrix[1].ToFloatPtr(), 4);
	if ( v == scratch[0] ) {
		glBufferSubData(GL_UNIFORM_BUFFER, 0, blocks->vertexSize, v);
	}

	if ( blocks->fragmentSize ) {
		/* the HDR specular boost, folded into the constant as
		   RB_GLSL_DrawInteraction does */
		const float specBoost = RB_HDRSpecularBoost();
		float sc[4];
		float hp[4];
		sc[0] = din->specularColor[0] * specBoost;
		sc[1] = din->specularColor[1] * specBoost;
		sc[2] = din->specularColor[2] * specBoost;
		sc[3] = din->specularColor[3];
		RB_GLSL_HDRParms(hp);

		f = RB_GLSL_BindDrawBlock(DRAW_FRAGMENT_BINDING, 1, blocks->fragmentSize, scratch[1]);
		RB_GLSL_DrawBlockMember(f, blocks->fragmentOffset[DF_DIFFUSE_COLOR], din->diffuseColor.ToFloatPtr(), 4);
		RB_GLSL_DrawBlockMember(f, blocks->fragmentOffset[DF_SPECULAR_COLOR],
				specBoost != 1.0f ? sc : din->specularColor.ToFloatPtr(), 4);
		RB_GLSL_DrawBlockMember(f, blocks->fragmentOffset[DF_HDR_PARMS], hp, 4);
		RB_GLSL_DrawBlockMember(f, blocks->fragmentOffset[DF_SPECULAR_EXPONENT], &rbSpecularExponent, 1);
		if ( f == scratch[1] ) {
			glBufferSubData(GL_UNIFORM_BUFFER, 0, blocks->fragmentSize, f);
		}
	}

	// the textures, as RB_GLSL_DrawInteraction binds them
	din->bumpImage->Bind();
	GL_SelectTexture(1);
	din->lightFalloffImage->Bind();
	GL_SelectTexture(2);
	din->lightImage->Bind();
	GL_SelectTexture(3);
	din->diffuseImage->Bind();
	GL_SelectTexture(4);
	din->specularImage->Bind();
	GL_SelectTexture(0);

	RB_DrawElementsWithCounters(din->surf->geo);
}

/*
==================
RB_GLSL_DrawInteraction
==================
*/
static void RB_GLSL_DrawInteraction(const drawInteraction_t* din) {
	static const GLfloat zero[1] = { 0 };
	static const GLfloat one[1] = { 1 };
	static const GLfloat oneScaled[1] = { 1 / 255.0f };
	static const GLfloat negOneScaled[1] = { -1.0f / 255.0f };

	if ( glslDrawBlocks ) {
		const drawBlocks_t* blocks = RB_GLSL_DrawBlocks(backEnd.glState.currentProgram);
		if ( blocks && blocks->vertexSize ) {
			RB_GLSL_DrawInteractionBlocks(din, blocks);
			return;
		}
	}

	// load all the vertex program parameters
	GL_Uniform4fv(offsetof(shaderProgram_t, localLightOrigin), din->localLightOrigin.ToFloatPtr());
	GL_Uniform4fv(offsetof(shaderProgram_t, localViewOrigin), din->localViewOrigin.ToFloatPtr());
	GL_UniformMatrix4fv(offsetof(shaderProgram_t, lightProjection), din->lightProjection.ToFloatPtr());
	GL_Uniform4fv(offsetof(shaderProgram_t, bumpMatrixS), din->bumpMatrix[0].ToFloatPtr());
	GL_Uniform4fv(offsetof(shaderProgram_t, bumpMatrixT), din->bumpMatrix[1].ToFloatPtr());
	GL_Uniform4fv(offsetof(shaderProgram_t, diffuseMatrixS), din->diffuseMatrix[0].ToFloatPtr());
	GL_Uniform4fv(offsetof(shaderProgram_t, diffuseMatrixT), din->diffuseMatrix[1].ToFloatPtr());
	GL_Uniform4fv(offsetof(shaderProgram_t, specularMatrixS), din->specularMatrix[0].ToFloatPtr());
	GL_Uniform4fv(offsetof(shaderProgram_t, specularMatrixT), din->specularMatrix[1].ToFloatPtr());

	switch ( din->vertexColor ) {
	case SVC_MODULATE: {
		GL_Uniform1fv(offsetof(shaderProgram_t, colorModulate), oneScaled);
		GL_Uniform1fv(offsetof(shaderProgram_t, colorAdd), zero);
		break;
	}
	case SVC_INVERSE_MODULATE: {
		GL_Uniform1fv(offsetof(shaderProgram_t, colorModulate), negOneScaled);
		GL_Uniform1fv(offsetof(shaderProgram_t, colorAdd), one);
		break;
	}
	default:
	case SVC_IGNORE:
		// This is already the default values (zero, one)
		break;
	}

	// set the constant colors
	GL_Uniform4fv(offsetof(shaderProgram_t, diffuseColor), din->diffuseColor.ToFloatPtr());
	/* HDR specular boost, folded into the constant as on GLES2, gated the
	   same way by RB_HDRSpecularBoost; u_specularColor is mediump so the
	   boosted value is not clipped. */
	const float specBoost = RB_HDRSpecularBoost();
	if ( specBoost != 1.0f ) {
		float sc[4];
		sc[0] = din->specularColor[0] * specBoost;
		sc[1] = din->specularColor[1] * specBoost;
		sc[2] = din->specularColor[2] * specBoost;
		sc[3] = din->specularColor[3];
		GL_Uniform4fv(offsetof(shaderProgram_t, specularColor), sc);
	} else {
		GL_Uniform4fv(offsetof(shaderProgram_t, specularColor), din->specularColor.ToFloatPtr());
	}

	if ( backEnd.glState.currentProgram != NULL && backEnd.glState.currentProgram->hdrParms >= 0 ) {
		float hp[4];
		RB_GLSL_HDRParms(hp);
		GL_Uniform4fv(offsetof(shaderProgram_t, hdrParms), hp);
	}

	// set the textures

	// texture 0 will be the per-surface bump map
	// NB: Texture 0 is expected to be active at this point
	din->bumpImage->Bind();

	// texture 1 will be the light falloff texture
	GL_SelectTexture(1);
	din->lightFalloffImage->Bind();

	// texture 2 will be the light projection texture
	GL_SelectTexture(2);
	din->lightImage->Bind();

	// texture 3 is the per-surface diffuse map
	GL_SelectTexture(3);
	din->diffuseImage->Bind();

	// texture 4 is the per-surface specular map
	GL_SelectTexture(4);
	din->specularImage->Bind();

	// Be sure to activate Texture 0 for next interaction, or next pass
	GL_SelectTexture(0);

	// draw it
	RB_DrawElementsWithCounters(din->surf->geo);

	// Restore color modulation state to default values
	if ( din->vertexColor != SVC_IGNORE ) {
		GL_Uniform1fv(offsetof(shaderProgram_t, colorModulate), zero);
		GL_Uniform1fv(offsetof(shaderProgram_t, colorAdd), one);
	}
}

/*
=============
RB_CreateSingleDrawInteractions

This can be used by different draw_* backends to decompose a complex light / surface
interaction into primitive interactions
=============
*/
static void
RB_GLSL_CreateSingleDrawInteractions(const drawSurf_t* surf, void (* DrawInteraction)(const drawInteraction_t*), const viewLight_t* vLight) {
	const idMaterial* surfaceShader = surf->material;
	const float* surfaceRegs = surf->shaderRegisters;
	const idMaterial* lightShader = vLight->lightShader;
	const float* lightRegs = vLight->shaderRegisters;
	drawInteraction_t inter;
	inter.diffuseMatrix[0].Zero();
	inter.diffuseMatrix[1].Zero();
	inter.specularMatrix[0].Zero();
	inter.specularMatrix[1].Zero();

	if ( r_skipInteractions.GetBool() || !surf->geo || !surf->geo->ambientCache ) {
		return;
	}

	// change the scissor if needed
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals(surf->scissorRect)) {
		backEnd.currentScissor = surf->scissorRect;
		if (( backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1 ) < 0.0f ||
		        ( backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 ) < 0.0f ) {
			backEnd.currentScissor = backEnd.viewDef->scissor;
		}
		qglScissor(backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
		           backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
		           backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
		           backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1);
	}

	inter.surf = surf;
	inter.lightFalloffImage = vLight->falloffImage;

	R_GlobalPointToLocal(surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3());
	R_GlobalPointToLocal(surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3());
	inter.localLightOrigin[3] = 0;
	inter.localViewOrigin[3] = 1;
	inter.ambientLight = lightShader->IsAmbientLight();

	// the base projections may be modified by texture matrix on light stages
	idPlane lightProject[4];

	for ( int i = 0; i < 4; i++ ) {
		R_GlobalPlaneToLocal(surf->space->modelMatrix, vLight->lightProject[i], lightProject[i]);
	}

	for ( int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++ ) {
		const shaderStage_t* lightStage = lightShader->GetStage(lightStageNum);

		// ignore stages that fail the condition
		if ( !lightRegs[lightStage->conditionRegister] ) {
			continue;
		}

		inter.lightImage = lightStage->texture.image;

		inter.lightProjection[0] = lightProject[0].ToVec4(); // S
		inter.lightProjection[1] = lightProject[1].ToVec4(); // T
		inter.lightProjection[2] = lightProject[3].ToVec4(); // CAUTION! this is the 4th vector. R = Falloff
		inter.lightProjection[3] = lightProject[2].ToVec4(); // CAUTION! this is the 3rd vector. Q

		// now multiply the texgen by the light texture matrix
		if ( lightStage->texture.hasMatrix ) {
			float lightTextureMatrix[16];
			RB_GetShaderTextureMatrix(lightRegs, &lightStage->texture, lightTextureMatrix);
			RB_BakeTextureMatrixIntoTexgen(inter.lightProjection, lightTextureMatrix);
		}

		inter.bumpImage = NULL;
		inter.specularImage = NULL;
		inter.diffuseImage = NULL;
		inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
		inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;

		float lightColor[4];

		const float lightscale = r_lightScale.GetFloat();
		lightColor[0] = lightscale * lightRegs[lightStage->color.registers[0]];
		lightColor[1] = lightscale * lightRegs[lightStage->color.registers[1]];
		lightColor[2] = lightscale * lightRegs[lightStage->color.registers[2]];
		lightColor[3] = lightRegs[lightStage->color.registers[3]];

		// go through the individual stages
		for ( int surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++ ) {
			const shaderStage_t* surfaceStage = surfaceShader->GetStage(surfaceStageNum);

			switch ( surfaceStage->lighting ) {
			case SL_AMBIENT: {
				// ignore ambient stages while drawing interactions
				break;
			}
			case SL_BUMP: {
				// ignore stage that fails the condition
				if ( !surfaceRegs[surfaceStage->conditionRegister] ) {
					break;
				}

				// draw any previous interaction
				RB_SubmittInteraction(&inter, DrawInteraction);
				inter.diffuseImage = NULL;
				inter.specularImage = NULL;
				RB_SetDrawInteraction(surfaceStage, surfaceRegs, &inter.bumpImage, inter.bumpMatrix, NULL);
				break;
			}
			case SL_DIFFUSE: {
				// ignore stage that fails the condition
				if ( !surfaceRegs[surfaceStage->conditionRegister] ) {
					break;
				}

				if ( inter.diffuseImage ) {
					RB_SubmittInteraction(&inter, DrawInteraction);
				}

				RB_SetDrawInteraction(surfaceStage, surfaceRegs, &inter.diffuseImage,
				                      inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr());
				inter.diffuseColor[0] *= lightColor[0];
				inter.diffuseColor[1] *= lightColor[1];
				inter.diffuseColor[2] *= lightColor[2];
				inter.diffuseColor[3] *= lightColor[3];
				inter.vertexColor = surfaceStage->vertexColor;
				break;
			}
			case SL_SPECULAR: {
				// ignore stage that fails the condition
				if ( !surfaceRegs[surfaceStage->conditionRegister] ) {
					break;
				}

				if ( inter.specularImage ) {
					RB_SubmittInteraction(&inter, DrawInteraction);
				}

				RB_SetDrawInteraction(surfaceStage, surfaceRegs, &inter.specularImage,
				                      inter.specularMatrix, inter.specularColor.ToFloatPtr());
				inter.specularColor[0] *= lightColor[0];
				inter.specularColor[1] *= lightColor[1];
				inter.specularColor[2] *= lightColor[2];
				inter.specularColor[3] *= lightColor[3];
				inter.vertexColor = surfaceStage->vertexColor;
				break;
			}
			}
		}

		// draw the final interaction
		RB_SubmittInteraction(&inter, DrawInteraction);
	}
}

/*
=============
RB_GLSL_CreateDrawInteractions

=============
*/
static void RB_GLSL_CreateDrawInteractions(const drawSurf_t* surf, const viewLight_t* vLight, const int depthFunc = GLS_DEPTHFUNC_EQUAL) {
	if ( !surf ) {
		return;
	}

	// perform setup here that will be constant for all interactions
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depthFunc);

	// bind the vertex and fragment shader and set the specular exponent
	float f = 0;
	switch (r_usePBR.GetInteger()) {
		case 1: //Basic PBR shading
			GL_UseProgram(&interactionShader);
			f = r_specularExponentPBR.GetFloat();
			break;
		case 2: //Enhanced PBR shading
			GL_UseProgram(&interactionShader);
			f = -r_specularExponentPBR.GetFloat();
			break;
		default: // as on GLES2: Phong with its exponent, or Blinn-Phong
			if ( r_usePhong.GetBool() ) {
				GL_UseProgram(&interactionPhongShader);
				f = r_specularExponent.GetFloat();
			} else {
				GL_UseProgram(&interactionBlinnShader);
			}
			break;
	}

	// Set the specular exponent now (NB: it could be cached instead)
	GL_Uniform1fv(offsetof(shaderProgram_t, specularExponent), &f);
	rbSpecularExponent = f;
	// with the blocks, each draw carries its space
	const bool drawBlocksActive = glslDrawBlocks && RB_GLSL_DrawBlocks(backEnd.glState.currentProgram) != NULL;



	// Setup attributes arrays
	// Vertex attribute is always enabled
	// Color attribute is always enabled
	// TexCoord attribute is always enabled
	// Enable the rest
	GL_EnableVertexAttribArray(ATTR_TANGENT);
	GL_EnableVertexAttribArray(ATTR_BITANGENT);
	GL_EnableVertexAttribArray(ATTR_NORMAL);

	backEnd.currentSpace = NULL;

	for ( ; surf; surf = surf->nextOnLight ) {
		// perform setup here that will not change over multiple interaction passes

		if ( surf->space != backEnd.currentSpace && !drawBlocksActive ) {
			RB_GLSL_SetSpace(surf->space);
		}

		// Hack Depth Range if necessary
		bool bNeedRestoreDepthRange = false;
		if (surf->space->weaponDepthHack && surf->space->modelDepthHack == 0.0f) {
			qglDepthRangef(0.0f, 0.5f);
			bNeedRestoreDepthRange = true;
		}

		// set the vertex pointers
		idDrawVert* ac = (idDrawVert*) RB_GLSL_VertexBase(surf->geo->ambientCache, sizeof(idDrawVert));

		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Normal), 3, GL_FLOAT, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, normal));
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Bitangent), 3, GL_FLOAT, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, tangents[1]));
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Tangent), 3, GL_FLOAT, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, tangents[0]));
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_TexCoord), 2, GL_FLOAT, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, st));
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 3, GL_FLOAT, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, xyz));
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Color), 4, GL_UNSIGNED_BYTE, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, color ));

		// this may cause RB_GLSL_DrawInteraction to be exacuted multiple
		// times with different colors and images if the surface or light have multiple layers
		RB_GLSL_CreateSingleDrawInteractions(surf, RB_GLSL_DrawInteraction, vLight);

		// Restore the Depth Range in case it have been hacked
		if ( bNeedRestoreDepthRange ) {
			qglDepthRangef( 0.0f, 1.0f );
		}

		backEnd.currentSpace = surf->space;
	}

	backEnd.currentSpace = NULL;

	// Restore attributes arrays
	// Vertex attribute is always enabled
	// Color attribute is always enabled
	// TexCoord attribute is always enabled
	GL_DisableVertexAttribArray(ATTR_TANGENT);
	GL_DisableVertexAttribArray(ATTR_BITANGENT);
	GL_DisableVertexAttribArray(ATTR_NORMAL);
}


// the separate stencil ops the current shadow pass last set: -1 unknown,
// 0 depth-fail, 1 depth-pass; RB_GLSL_StencilShadowPass forgets them
static int rbShadowExternal = -1;

/*
=====================
RB_T_GLSL_Shadow

the shadow volumes face INSIDE
=====================
*/
/*
=====================
RB_GLSL_ShadowIndexes

How many of a shadow volume's indexes to draw - the sil planes always, the
caps when they can be seen - and whether it is drawn depth-pass (external).
=====================
*/
static int RB_GLSL_ShadowIndexes(const drawSurf_t* surf, const viewLight_t* vLight, bool* external) {
	*external = false;
	if ( !r_useExternalShadows.GetInteger() ) {
		return surf->geo->numIndexes;
	}
	if ( r_useExternalShadows.GetInteger() == 2 ) {	// force to no caps for testing
		return surf->geo->numShadowIndexesNoCaps;
	}
	if ( !( surf->dsFlags & DSF_VIEW_INSIDE_SHADOW ) ) {
		// if we aren't inside the shadow projection, no caps are ever needed
		*external = true;
		return surf->geo->numShadowIndexesNoCaps;
	}
	if ( !vLight->viewInsideLight && !( surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE ) ) {
		// inside the shadow projection but outside the light, and a
		// non-infinite shadow: some caps can be skipped
		*external = true;
		if ( vLight->viewSeesShadowPlaneBits & surf->geo->shadowCapPlaneBits ) {
			// a rear cap can be seen through, but not the surface's own caps
			return surf->geo->numShadowIndexesNoFrontCaps;
		}
		return surf->geo->numShadowIndexesNoCaps;
	}
	// must draw everything
	return surf->geo->numIndexes;
}

/*
=====================
RB_GLSL_ShadowStencilOps

depth-fail stencil shadows, else traditional depth-pass ones; most surfaces
of a light repeat the previous one's ops, sent only on a change
=====================
*/
static void RB_GLSL_ShadowStencilOps(bool external) {
	if ( rbShadowExternal == (int)external ) {
		return;
	}
	if ( !external ) {
		qglStencilOpSeparate(backEnd.viewDef->isMirror ? GL_FRONT : GL_BACK, GL_KEEP, GL_DECR, GL_KEEP);
		qglStencilOpSeparate(backEnd.viewDef->isMirror ? GL_BACK : GL_FRONT, GL_KEEP, GL_INCR, GL_KEEP);
	} else {
		qglStencilOpSeparate(backEnd.viewDef->isMirror ? GL_FRONT : GL_BACK, GL_KEEP, GL_KEEP, GL_INCR);
		qglStencilOpSeparate(backEnd.viewDef->isMirror ? GL_BACK : GL_FRONT, GL_KEEP, GL_KEEP, GL_DECR);
	}
	rbShadowExternal = (int)external;
}

/*
=====================
Draw runs (r_glslBatches)

A run collects the draws of consecutive surfaces that share the vertex
buffer, the index buffer and its type, and the pass's state, then goes out
as one indirect draw.  A surface that cannot join - its data outside a
shared buffer, or the ring full - ends the run and is drawn on its own.
=====================
*/
typedef struct {
	GLuint	count;
	GLuint	instanceCount;
	GLuint	firstIndex;
	GLint	baseVertex;
	GLuint	baseInstance;
} rbIndirect_t;

typedef struct {
	shaderProgram_t*	program;
	vertCache_t*		vertexes;		// the first draw's blocks, to bind the buffers by
	vertCache_t*		indexes;
	GLuint				vertexBuffer;
	GLuint				indexBuffer;
	GLenum				indexType;
	int					stride;			// sizeof idDrawVert: the depth fill, with texcoords
	int					external;		// shadows: the stencil ops, else -1
	bool				depthRange;		// the weapon's halved depth range
	int					draws;
	rbIndirect_t*		commands;
	byte*				data;
	GLuint				commandBuffer;
	GLuint				dataBuffer;
	int					commandOffset;
	int					dataOffset;
} rbBatch_t;

static rbBatch_t rbBatch;

static bool RB_GLSL_Batching(void) {
	return glslBatches && r_glslBatches.GetBool() && vertexCache.Streaming() && !glslMultiview;
}

static void RB_GLSL_BatchFlush(void) {
	rbBatch_t* b = &rbBatch;
	if ( !b->draws ) {
		return;
	}
	GL_UseProgram(b->program);
	if ( b->stride == (int)sizeof(idDrawVert) ) {
		const idDrawVert* ac = (const idDrawVert*) RB_GLSL_VertexBase(b->vertexes, b->stride);
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 3, GL_FLOAT, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, xyz));
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_TexCoord), 2, GL_FLOAT, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, st));
	} else {
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 4, GL_FLOAT, false, sizeof(shadowCache_t),
		                       RB_GLSL_VertexBase(b->vertexes, b->stride));
	}
	vertexCache.Position(b->indexes);
	if ( b->external >= 0 ) {
		RB_GLSL_ShadowStencilOps(b->external != 0);
	}
	if ( b->depthRange ) {
		qglDepthRangef(0.0f, 0.5f);
	}
	glCaps.BindBufferRange(GL_UNIFORM_BUFFER, DRAW_LIST_BINDING, b->dataBuffer, b->dataOffset,
	                       RB_BATCH_DRAWS * RB_BATCH_ENTRY);
	qglBindBufferARB(GL_DRAW_INDIRECT_BUFFER, b->commandBuffer);
	glCaps.MultiDrawElementsIndirect(GL_TRIANGLES, b->indexType, (const void*)(intptr_t) b->commandOffset,
	                                 b->draws, 0);
	qglBindBufferARB(GL_DRAW_INDIRECT_BUFFER, 0);
	if ( b->depthRange ) {
		qglDepthRangef(0.0f, 1.0f);
	}
	b->draws = 0;
}

/*
=====================
RB_GLSL_BatchAdd

Adds a draw to the run, flushing the run first when the draw cannot share
it.  False when the draw cannot go in a run at all; the run is then
flushed, for the caller to draw it on its own.
=====================
*/
static bool RB_GLSL_BatchAdd(shaderProgram_t* program, vertCache_t* vertexes, int stride, vertCache_t* indexes,
                             int numIndexes, int external, bool depthRange, const viewEntity_t* space,
                             const float lightOrigin[4]) {
	rbBatch_t* b = &rbBatch;
	rbIndirect_t* cmd;
	float* entry;
	GLenum indexType;
	int indexSize;
	int baseVertex;

	baseVertex = vertexCache.BaseVertex(vertexes, stride);
	if ( baseVertex < 0 || !indexes || !indexes->vbo || !numIndexes ) {
		RB_GLSL_BatchFlush();
		return false;
	}
	indexType = vertexCache.IndexType(indexes);
	indexSize = indexType == GL_UNSIGNED_SHORT ? 2 : 4;
	if ( ( indexes->offset % indexSize ) != 0 ) {
		RB_GLSL_BatchFlush();
		return false;
	}

	if ( b->draws && ( b->program != program || b->vertexBuffer != vertexes->vbo || b->indexBuffer != indexes->vbo
			|| b->indexType != indexType || b->external != external || b->depthRange != depthRange
			|| b->draws == RB_BATCH_DRAWS ) ) {
		RB_GLSL_BatchFlush();
	}
	if ( !b->draws ) {
		b->commands = (rbIndirect_t*) vertexCache.RingReserve(RB_BATCH_DRAWS * (int)sizeof(rbIndirect_t), 4,
		                                                       &b->commandBuffer, &b->commandOffset);
		if ( !b->commands ) {
			return false;
		}
		b->data = (byte*) vertexCache.RingReserve(RB_BATCH_DRAWS * RB_BATCH_ENTRY, glCaps.uniformBufferAlign,
		                                          &b->dataBuffer, &b->dataOffset);
		if ( !b->data ) {
			return false;
		}
		b->program = program;
		b->vertexes = vertexes;
		b->indexes = indexes;
		b->vertexBuffer = vertexes->vbo;
		b->indexBuffer = indexes->vbo;
		b->indexType = indexType;
		b->stride = stride;
		b->external = external;
		b->depthRange = depthRange;
	}

	cmd = &b->commands[b->draws];
	cmd->count = numIndexes;
	cmd->instanceCount = 1;
	cmd->firstIndex = (GLuint)( indexes->offset / indexSize );
	cmd->baseVertex = baseVertex;
	cmd->baseInstance = 0;

	entry = (float*)( b->data + b->draws * RB_BATCH_ENTRY );
	memcpy(entry, space->modelMatrix, 16 * sizeof(float));
	if ( lightOrigin ) {
		memcpy(entry + 16, lightOrigin, 4 * sizeof(float));
	} else {
		entry[16] = entry[17] = entry[18] = entry[19] = 0.0f;
	}
	entry[20] = RB_GLSL_DepthHack(space);
	entry[21] = entry[22] = entry[23] = 0.0f;

	b->draws++;
	return true;
}

/*
=====================
RB_GLSL_BatchScissor

The scissor change the passes make before a surface, the run flushed first
when it moves the scissor.
=====================
*/
static void RB_GLSL_BatchScissor(const drawSurf_t* surf) {
	idScreenRect r;

	if ( !r_useScissor.GetBool() || backEnd.currentScissor.Equals(surf->scissorRect) ) {
		return;
	}
	r = surf->scissorRect;
	if ( ( r.x2 + 1 - r.x1 ) < 0.0f || ( r.y2 + 1 - r.y1 ) < 0.0f ) {
		r = backEnd.viewDef->scissor;
	}
	if ( !r.Equals(backEnd.currentScissor) ) {
		RB_GLSL_BatchFlush();
	}
	backEnd.currentScissor = r;
	qglScissor(backEnd.viewDef->viewport.x1 + r.x1,
	           backEnd.viewDef->viewport.y1 + r.y1,
	           r.x2 + 1 - r.x1,
	           r.y2 + 1 - r.y1);
}

static void RB_T_GLSL_Shadow(const drawSurf_t* surf, const viewLight_t* vLight) {
	bool external;
	int numIndexes;

	if ( !surf->geo->shadowCache ) {
		return;
	}

	// set the light position for the vertex program to project the rear surfaces
	if ( surf->space != backEnd.currentSpace ) {
		idVec4 localLight;

		R_GlobalPointToLocal(surf->space->modelMatrix, vLight->globalLightOrigin, localLight.ToVec3());
		localLight.w = 0.0f;
		GL_Uniform4fv(offsetof(shaderProgram_t, localLightOrigin), localLight.ToFloatPtr());
	}

	GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 4, GL_FLOAT, false, sizeof(shadowCache_t),
	                       RB_GLSL_VertexBase(surf->geo->shadowCache, sizeof(shadowCache_t)));

	numIndexes = RB_GLSL_ShadowIndexes(surf, vLight, &external);
	RB_GLSL_ShadowStencilOps(external);
	RB_DrawShadowElementsWithCounters(surf->geo, numIndexes);
}


/*
======================
RB_RenderDrawSurfChainWithFunction
======================
*/
void RB_GLSL_RenderDrawSurfChainWithFunction(const drawSurf_t* drawSurfs,
        void (* triFunc_)(const drawSurf_t*, const viewLight_t*), const viewLight_t* vLight) {
	const drawSurf_t* drawSurf;

	backEnd.currentSpace = NULL;

	for ( drawSurf = drawSurfs; drawSurf; drawSurf = drawSurf->nextOnLight ) {

		// Change the MVP matrix if needed
		if ( drawSurf->space != backEnd.currentSpace ) {
			RB_GLSL_SetSpace(drawSurf->space);
		}

		// Hack Depth Range if necessary
		bool bNeedRestoreDepthRange = false;
		if (drawSurf->space->weaponDepthHack && drawSurf->space->modelDepthHack == 0.0f) {
			qglDepthRangef(0.0f, 0.5f);
			bNeedRestoreDepthRange = true;
		}

		// change the scissor if needed
		if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals(drawSurf->scissorRect)) {
			backEnd.currentScissor = drawSurf->scissorRect;
			if (( backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1 ) < 0.0f ||
			        ( backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 ) < 0.0f ) {
				backEnd.currentScissor = backEnd.viewDef->scissor;
			}
			qglScissor(backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			           backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			           backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			           backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1);

		}

		// render it
		triFunc_(drawSurf, vLight);

		// Restore the Depth Range in case it have been hacked
		if ( bNeedRestoreDepthRange ) {
			qglDepthRangef( 0.0f, 1.0f );
		}

		backEnd.currentSpace = drawSurf->space;
	}

	backEnd.currentSpace = NULL;
}

/*
=====================
RB_GLSL_ShadowRuns

RB_GLSL_RenderDrawSurfChainWithFunction over RB_T_GLSL_Shadow, in runs.
=====================
*/
static void RB_GLSL_ShadowRuns(const drawSurf_t* drawSurfs, const viewLight_t* vLight) {
	const drawSurf_t* surf;
	const viewEntity_t* singleSpace = NULL;	// what the single-draw program's uniforms hold
	const viewEntity_t* lightSpace = NULL;
	idVec4 localLight;
	bool external;
	bool depthRange;
	int numIndexes;

	localLight.Zero();
	for ( surf = drawSurfs; surf; surf = surf->nextOnLight ) {
		RB_GLSL_BatchScissor(surf);
		if ( !surf->geo->shadowCache ) {
			continue;
		}
		depthRange = surf->space->weaponDepthHack && surf->space->modelDepthHack == 0.0f;

		numIndexes = RB_GLSL_ShadowIndexes(surf, vLight, &external);
		if ( surf->space != lightSpace ) {
			R_GlobalPointToLocal(surf->space->modelMatrix, vLight->globalLightOrigin, localLight.ToVec3());
			localLight.w = 0.0f;
			lightSpace = surf->space;
		}
		if ( RB_GLSL_BatchAdd(&stencilShadowBatchShader, surf->geo->shadowCache, sizeof(shadowCache_t),
		                      surf->geo->indexCache, numIndexes, external ? 1 : 0, depthRange, surf->space,
		                      localLight.ToFloatPtr()) ) {
			backEnd.pc.c_shadowElements++;
			backEnd.pc.c_shadowIndexes += numIndexes;
			backEnd.pc.c_shadowVertexes += surf->geo->numVerts;
			backEnd.pc.c_vboIndexes += numIndexes;
			continue;
		}

		// on its own, through the single-draw program
		GL_UseProgram(&stencilShadowShader);
		backEnd.currentSpace = singleSpace;
		if ( surf->space != backEnd.currentSpace ) {
			RB_GLSL_SetSpace(surf->space);
		}
		if ( depthRange ) {
			qglDepthRangef(0.0f, 0.5f);
		}
		RB_T_GLSL_Shadow(surf, vLight);
		if ( depthRange ) {
			qglDepthRangef(0.0f, 1.0f);
		}
		singleSpace = surf->space;
	}
	RB_GLSL_BatchFlush();
	backEnd.currentSpace = NULL;
}

/*
=====================
RB_GLSL_StencilShadowPass

Stencil test should already be enabled, and the stencil buffer should have
been set to 128 on any surfaces that might receive shadows
=====================
*/
void RB_GLSL_StencilShadowPass(const drawSurf_t* drawSurfs, const viewLight_t* vLight) {

	//////////////
	// Skip cases
	//////////////

	if ( r_shadows.GetInteger() == 0 ) {
		return;
	}

	if ( !drawSurfs ) {
		return;
	}

	//////////////////
	// Setup GL state
	//////////////////

	// Expected client GL state:
	// Vertex attribute enabled
	// Color attribute enabled

	// Use the stencil shadow shader
	GL_UseProgram(&stencilShadowShader);


	// Setup attributes arrays
	// Vertex attribute is always enabled
	// Disable Color attribute (as it is enabled by default)
	// Disable TexCoord attribute (as it is enabled by default)
	GL_DisableVertexAttribArray(ATTR_COLOR);
	GL_DisableVertexAttribArray(ATTR_TEXCOORD);

	// don't write to the color buffer, just the stencil buffer
	GL_State(GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS);

	if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat()) {
		qglPolygonOffset(r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat());
		qglEnable(GL_POLYGON_OFFSET_FILL);
	}

	qglStencilFunc(GL_ALWAYS, 1, 255);

	// Culling will be done two side for shadows
	GL_Cull(CT_TWO_SIDED);

	rbShadowExternal = -1;
	if ( RB_GLSL_Batching() ) {
		RB_GLSL_ShadowRuns(drawSurfs, vLight);
	} else {
		RB_GLSL_RenderDrawSurfChainWithFunction(drawSurfs, RB_T_GLSL_Shadow, vLight);
	}

	// Restore culling
	GL_Cull(CT_FRONT_SIDED);

	if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat()) {
		qglDisable(GL_POLYGON_OFFSET_FILL);
	}

	qglStencilFunc(GL_GEQUAL, 128, 255);
	qglStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);


	// Restore attributes arrays
	// Vertex attribute is always enabled
	// Re-enable Color attribute (as it is enabled by default)
	// Re-enable TexCoord attribute (as it is enabled by default)
	GL_EnableVertexAttribArray(ATTR_COLOR);
	GL_EnableVertexAttribArray(ATTR_TEXCOORD);
}

/*
==================
RB_GLSL_DrawInteractions
==================
*/
void RB_GLSL_DrawInteractions(void) {

	///////////////////////
	// For each light loop
	///////////////////////

	const viewLight_t* vLight;
	//
	// for each light, perform adding and shadowing
	//
	for ( vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next ) {

		//////////////
		// Skip Cases
		//////////////

		// do fogging later
		if ( vLight->lightShader->IsFogLight()) {
			continue;
		}

		if ( vLight->lightShader->IsBlendLight()) {
			continue;
		}

		if ( !vLight->localInteractions && !vLight->globalInteractions
		        && !vLight->translucentInteractions ) {
			continue;
		}

		//////////////////
		// Setup GL state
		//////////////////

		// clear the stencil buffer if needed
		if ( vLight->globalShadows || vLight->localShadows ) {

			if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals(vLight->scissorRect)) {
				backEnd.currentScissor = vLight->scissorRect;
				if (( backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1 ) < 0.0f ||
				        ( backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 ) < 0.0f ) {
					backEnd.currentScissor = backEnd.viewDef->scissor;
				}
				qglScissor(backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				           backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				           backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				           backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1);
			}

			qglClear(GL_STENCIL_BUFFER_BIT);
		} else {
			// no shadows, so no need to read or write the stencil buffer
			// we might in theory want to use GL_ALWAYS instead of disabling
			// completely, to satisfy the invarience rules
			qglStencilFunc(GL_ALWAYS, 128, 255);
		}

		RB_GLSL_StencilShadowPass(vLight->globalShadows, vLight);
		RB_GLSL_CreateDrawInteractions(vLight->localInteractions, vLight);
		RB_GLSL_StencilShadowPass(vLight->localShadows, vLight);
		RB_GLSL_CreateDrawInteractions(vLight->globalInteractions, vLight);

		// translucent surfaces never get stencil shadowed
		if ( r_skipTranslucent.GetBool()) {
			continue;
		}

		qglStencilFunc(GL_ALWAYS, 128, 255);
		RB_GLSL_CreateDrawInteractions(vLight->translucentInteractions, vLight, GLS_DEPTHFUNC_LESS);
	}

	// disable stencil shadow test
	qglStencilFunc(GL_ALWAYS, 128, 255);
}

// NB: oh, a nice global variable. Argh....
static idPlane fogPlanes[4];

/*
=====================
RB_T_BasicFog
=====================
*/
static void RB_T_GLSL_BasicFog(const drawSurf_t* surf, const viewLight_t* vLight) {
	if ( backEnd.currentSpace != surf->space ) {
		idPlane transfoFogPlane[4];

		//S
		R_GlobalPlaneToLocal(surf->space->modelMatrix, fogPlanes[0], transfoFogPlane[0]);
		transfoFogPlane[0][3] += 0.5;
		//T
		transfoFogPlane[1][0] = transfoFogPlane[1][1] = transfoFogPlane[1][2] = 0;
		transfoFogPlane[1][3] = 0.5;
		//T
		R_GlobalPlaneToLocal(surf->space->modelMatrix, fogPlanes[2], transfoFogPlane[2]);
		transfoFogPlane[2][3] += FOG_ENTER;
		//S
		R_GlobalPlaneToLocal(surf->space->modelMatrix, fogPlanes[3], transfoFogPlane[3]);

		idMat4 fogMatrix;
		fogMatrix[0] = transfoFogPlane[0].ToVec4();
		fogMatrix[1] = transfoFogPlane[1].ToVec4();
		fogMatrix[2] = transfoFogPlane[2].ToVec4();
		fogMatrix[3] = transfoFogPlane[3].ToVec4();

		GL_UniformMatrix4fv(offsetof(shaderProgram_t, fogMatrix), fogMatrix.ToFloatPtr());
	}

	idDrawVert* ac = (idDrawVert*) RB_GLSL_VertexBase(surf->geo->ambientCache, sizeof(idDrawVert));

	GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 3, GL_FLOAT, false, sizeof(idDrawVert),
	                       RB_DRAWVERT( ac, xyz));

	RB_DrawElementsWithCounters(surf->geo);
}

/*
==================
RB_FogPass
==================
*/
void RB_GLSL_FogPass(const drawSurf_t* drawSurfs, const drawSurf_t* drawSurfs2, const viewLight_t* vLight) {

	drawSurf_t ds;
	const idMaterial* lightShader;
	const shaderStage_t* stage;
	const float* regs;

	// create a surface for the light frustom triangles, which are oriented drawn side out
	const srfTriangles_t* frustumTris = vLight->frustumTris;

	// if we ran out of vertex cache memory, skip it
	if ( !frustumTris->ambientCache ) {
		return;
	}

	// Initial expected GL state:
	// Texture 0 is active, and bound to NULL
	// Vertex attribute array is enabled
	// All other attributes array are disabled
	// No shaders active

	GL_UseProgram(&fogShader);


	// Setup attributes arrays
	// Vertex attribute is always enabled
	// Disable Color attribute (as it is enabled by default)
	// Disable TexCoord attribute (as it is enabled by default)
	GL_DisableVertexAttribArray(ATTR_COLOR);
	GL_DisableVertexAttribArray(ATTR_TEXCOORD);

	memset(&ds, 0, sizeof(ds));
	ds.space = &backEnd.viewDef->worldSpace;
	ds.geo = frustumTris;
	ds.scissorRect = backEnd.viewDef->scissor;

	// find the current color and density of the fog
	lightShader = vLight->lightShader;
	regs = vLight->shaderRegisters;
	// assume fog shaders have only a single stage
	stage = lightShader->GetStage(0);

	float lightColor[4];

	lightColor[0] = regs[stage->color.registers[0]];
	lightColor[1] = regs[stage->color.registers[1]];
	lightColor[2] = regs[stage->color.registers[2]];
	lightColor[3] = regs[stage->color.registers[3]];

	// FogColor
	GL_Uniform4fv(offsetof(shaderProgram_t, fogColor), lightColor);

	// calculate the falloff planes
	const float a = ( lightColor[3] <= 1.0 ) ? -0.5f / DEFAULT_FOG_DISTANCE : -0.5f / lightColor[3];

	// texture 0 is the falloff image
	// It is expected to be already active
	globalImages->fogImage->Bind();

	fogPlanes[0][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2];
	fogPlanes[0][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[6];
	fogPlanes[0][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[10];
	fogPlanes[0][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[14];

	fogPlanes[1][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[0];
	fogPlanes[1][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[4];
	fogPlanes[1][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[8];
	fogPlanes[1][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[12];

	// texture 1 is the entering plane fade correction
	GL_SelectTexture(1);
	globalImages->fogEnterImage->Bind();
	// reactive texture 0 for next passes
	GL_SelectTexture(0);

	// T will get a texgen for the fade plane, which is always the "top" plane on unrotated lights
	fogPlanes[2][0] = 0.001f * vLight->fogPlane[0];
	fogPlanes[2][1] = 0.001f * vLight->fogPlane[1];
	fogPlanes[2][2] = 0.001f * vLight->fogPlane[2];
	fogPlanes[2][3] = 0.001f * vLight->fogPlane[3];

	// S is based on the view origin
	const float s = backEnd.viewDef->renderView.vieworg * fogPlanes[2].Normal() + fogPlanes[2][3];
	fogPlanes[3][0] = 0;
	fogPlanes[3][1] = 0;
	fogPlanes[3][2] = 0;
	fogPlanes[3][3] = FOG_ENTER + s;

	// draw it
	GL_State(GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL);
	RB_GLSL_RenderDrawSurfChainWithFunction(drawSurfs, RB_T_GLSL_BasicFog, vLight);
	RB_GLSL_RenderDrawSurfChainWithFunction(drawSurfs2, RB_T_GLSL_BasicFog, vLight);

	// the light frustum bounding planes aren't in the depth buffer, so use depthfunc_less instead
	// of depthfunc_equal
	GL_State(GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS);
	GL_Cull(CT_BACK_SIDED);
	RB_GLSL_RenderDrawSurfChainWithFunction(&ds, RB_T_GLSL_BasicFog, vLight);
	// Restore culling
	GL_Cull(CT_FRONT_SIDED);
	GL_State(GLS_DEPTHMASK | GLS_DEPTHFUNC_EQUAL); // Restore DepthFunc

	// Restore attributes arrays
	// Vertex attribute is always enabled
	// Re-enable Color attribute (as it is enabled by default)
	// Re-enable TexCoord attribute (as it is enabled by default)
	GL_EnableVertexAttribArray(ATTR_COLOR);
	GL_EnableVertexAttribArray(ATTR_TEXCOORD);
}

/*
==================
RB_T_FillDepthBuffer
==================
*/
/*
==================
RB_GLSL_DepthFillProgram

Switch the depth fill between its opaque and alpha-tested programs for one
surface; the program switched to gets the surface's space, as the pass only
sets it on the program current when the space changes.
==================
*/
static void RB_GLSL_DepthFillProgram(shaderProgram_t* program, const drawSurf_t* surf) {
	if ( backEnd.glState.currentProgram == program ) {
		return;
	}
	GL_UseProgram(program);
	RB_GLSL_SetSpace(surf->space);
}

void RB_T_GLSL_FillDepthBuffer(const drawSurf_t* surf) {

	const idMaterial* const shader = surf->material;

	//////////////
	// Skip cases
	//////////////

	if ( !shader->IsDrawn()) {
		return;
	}

	//const srfTriangles_t* const tri = surf->geo;

	// some deforms may disable themselves by setting numIndexes = 0
	if ( !surf->geo->numIndexes ) {
		return;
	}

	// translucent surfaces don't put anything in the depth buffer and don't
	// test against it, which makes them fail the mirror clip plane operation
	if ( shader->Coverage() == MC_TRANSLUCENT ) {
		return;
	}

	if ( !surf->geo->ambientCache ) {
		return;
	}

	// get the expressions for conditionals / color / texcoords
	const float* const regs = surf->shaderRegisters;

	// if all stages of a material have been conditioned off, don't do anything
	int stage;
	for ( stage = 0; stage < shader->GetNumStages(); stage++ ) {
		const shaderStage_t* pStage = shader->GetStage(stage);

		// check the stage enable condition
		if ( regs[pStage->conditionRegister] != 0 ) {
			break;
		}
	}

	if ( stage == shader->GetNumStages()) {
		return;
	}

	///////////////////////////////////////////
	// GL Shader setup for the current surface
	///////////////////////////////////////////

	// update the clip plane if needed
	if ( backEnd.viewDef->numClipPlanes && surf->space != backEnd.currentSpace ) {
		idPlane plane;

		R_GlobalPlaneToLocal(surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], plane);
		plane[3] += 0.5;  // the notch is in the middle

		GL_Uniform4fv(offsetof(shaderProgram_t, clipPlane), plane.ToFloatPtr());
	}

	// set polygon offset if necessary
	// NB: will be restored at the end of the process
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
		qglEnable(GL_POLYGON_OFFSET_FILL);
		qglPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
	}

	// Color
	// black by default
	float color[4] = { 0, 0, 0, 1 };
	// subviews will just down-modulate the color buffer by overbright
	// NB: will be restored at end of the process
	if ( shader->GetSort() == SS_SUBVIEW ) {
		GL_State(GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS);
		color[0] = color[1] = color[2] = 1.0f; // NB: was 1.0 / backEnd.overBright );
	}
	GL_Uniform4fv(offsetof(shaderProgram_t, glColor), color);

	// Get vertex data
	idDrawVert* ac = (idDrawVert*) RB_GLSL_VertexBase(surf->geo->ambientCache, sizeof(idDrawVert));

	// Setup attribute pointers
	GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 3, GL_FLOAT, false, sizeof(idDrawVert),
	                       RB_DRAWVERT( ac, xyz));
	GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_TexCoord), 2, GL_FLOAT, false, sizeof(idDrawVert),
	                       RB_DRAWVERT( ac, st));

	bool drawSolid = false;

	if ( shader->Coverage() == MC_OPAQUE ) {
		drawSolid = true;
	}

	////////////////////////////////
	// Perforated surfaces handling
	////////////////////////////////

	// we may have multiple alpha tested stages
	if ( shader->Coverage() == MC_PERFORATED ) {
		// if the only alpha tested stages are condition register omitted,
		// draw a normal opaque surface
		bool didDraw = false;

		///////////////////////
		// For each stage loop
		///////////////////////

		// perforated surfaces may have multiple alpha tested stages
		for ( stage = 0; stage < shader->GetNumStages(); stage++ ) {
			const shaderStage_t* pStage = shader->GetStage(stage);

			//////////////
			// Skip cases
			//////////////

			if ( !pStage->hasAlphaTest ) {
				continue;
			}

			// check the stage enable condition
			if ( regs[pStage->conditionRegister] == 0 ) {
				continue;
			}

			// if we at least tried to draw an alpha tested stage,
			// we won't draw the opaque surface
			didDraw = true;

			// set the alpha modulate
			color[3] = regs[pStage->color.registers[3]];

			// skip the entire stage if alpha would be black
			if ( color[3] <= 0 ) {
				continue;
			}

			//////////////////////////
			// GL Setup for the stage
			//////////////////////////

			// the alpha test needs the program with the texture and the discard
			if ( !backEnd.viewDef->numClipPlanes ) {
				RB_GLSL_DepthFillProgram(&zfillShader, surf);
			}

			// Color
			// alpha testing
			GL_Uniform4fv(offsetof(shaderProgram_t, glColor), color);
			GL_Uniform1fv(offsetof(shaderProgram_t, alphaTest), &regs[pStage->alphaTestRegister]);

			// bind the texture
			pStage->texture.image->Bind();

			// Setup the texture matrix if needed
			// NB: will be restored to identity
			if ( pStage->texture.hasMatrix ) {
				float matrix[16];
				RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, matrix);
				GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), matrix);
			}

			///////////
			// Draw it
			///////////
			RB_DrawElementsWithCounters(surf->geo);

			////////////////////////////////////////////////////////////
			// Restore everything to an acceptable state for next stage
			////////////////////////////////////////////////////////////

			// Restore identity matrix
			if ( pStage->texture.hasMatrix ) {
				GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), mat4_identity.ToFloatPtr());
			}
		}

		if ( !didDraw ) {
			drawSolid = true;
		}

		///////////////////////////////////////////////////////////
		// Restore everything to an acceptable state for next step
		///////////////////////////////////////////////////////////

		// Restore color alpha to opaque
		color[3] = 1;
		GL_Uniform4fv(offsetof(shaderProgram_t, glColor), color);

		// Restore alphatest always passing
		static const float one[1] = { 1 };
		GL_Uniform1fv(offsetof(shaderProgram_t, alphaTest), one);

		// Restore white image binding to Tex0
		globalImages->whiteImage->Bind();

		// back to the opaque program, whose colour this surface already set
		if ( !backEnd.viewDef->numClipPlanes ) {
			RB_GLSL_DepthFillProgram(&zfillOpaqueShader, surf);
		}
	}

	////////////////////////////////////////
	// Normal surfaces case (non perforated)
	////////////////////////////////////////

	// draw the entire surface solid
	if ( drawSolid ) {
		///////////
		// Draw it
		///////////
		RB_DrawElementsWithCounters(surf->geo);
	}

	/////////////////////////////////////////////
	// Restore everything to an acceptable state
	/////////////////////////////////////////////

	// reset polygon offset
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
		qglDisable(GL_POLYGON_OFFSET_FILL);
	}

	// Restore blending
	if ( shader->GetSort() == SS_SUBVIEW ) {
		GL_State(GLS_DEPTHFUNC_LESS);
	}
}

/*
=====================
RB_GLSL_DepthFillRun

Whether RB_T_GLSL_FillDepthBuffer would draw the surface solid black with
the opaque program and no state of its own, which a run can do instead.
=====================
*/
static bool RB_GLSL_DepthFillRun(const drawSurf_t* surf) {
	const idMaterial* const shader = surf->material;
	const float* const regs = surf->shaderRegisters;
	int stage;

	if ( !shader->IsDrawn() || !surf->geo->numIndexes || !surf->geo->ambientCache
			|| shader->Coverage() != MC_OPAQUE || shader->GetSort() == SS_SUBVIEW
			|| shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		return false;
	}
	for ( stage = 0; stage < shader->GetNumStages(); stage++ ) {
		if ( regs[shader->GetStage(stage)->conditionRegister] != 0 ) {
			return true;
		}
	}
	return false;
}

/*
=====================
RB_GLSL_CaptureDepth

The view's depth into _currentDepth, by a blit into the image's own
framebuffer, for the stages that read it.  The image keeps the format of
the scene's depth buffer, which is what a depth blit requires.
=====================
*/
static void RB_GLSL_CaptureDepth(void) {
	idImage* const image = globalImages->currentDepthImage;
	const int x = backEnd.viewDef->viewport.x1;
	const int y = backEnd.viewDef->viewport.y1;
	const int w = backEnd.viewDef->viewport.x2 - x + 1;
	const int h = backEnd.viewDef->viewport.y2 - y + 1;
	const GLuint view = GLimp_ViewFramebuffer();
	int potWidth, potHeight;

	depthCopied = false;
	if ( !glCaps.blit || glslMultiview || depthBlitRefused || !RB_DepthCaptureWanted() ) {
		return;
	}
	potWidth = R_CopySize(w);
	potHeight = R_CopySize(h);
	if ( image->texnum == idImage::TEXTURE_NOT_LOADED || image->uploadWidth < potWidth || image->uploadHeight < potHeight ) {
		bool fresh = true;
		if ( image->texnum == idImage::TEXTURE_NOT_LOADED ) {
			qglGenTextures(1, &image->texnum);
		}
		image->uploadWidth = potWidth;
		image->uploadHeight = potHeight;
		image->type = TT_2D;
		GL_SelectTexture(0);
		image->Bind();
		qglTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, potWidth, potHeight, 0, GL_DEPTH_STENCIL,
		              GL_UNSIGNED_INT_24_8, NULL);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if ( !depthCopyFbo ) {
			glGenFramebuffers(1, &depthCopyFbo);
		}
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depthCopyFbo);
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, image->texnum, 0);
		if ( glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, view);
			glDeleteFramebuffers(1, &depthCopyFbo);
			depthCopyFbo = 0;
			common->Warning("_currentDepth: no framebuffer for the depth copy");
			return;
		}
		// Whether the driver takes a depth blit between these two
		// framebuffers is settled by their formats and sizes, so ask
		// once per allocation: glGetError is a round trip to the driver
		// thread, not something for every frame.
		glBindFramebuffer(GL_READ_FRAMEBUFFER, view);
		while ( qglGetError() != GL_NO_ERROR ) {
		}
		qglDisable(GL_SCISSOR_TEST);
		glCaps.BlitFramebuffer(x, y, x + w, y + h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		qglEnable(GL_SCISSOR_TEST);
		fresh = qglGetError() == GL_NO_ERROR;
		glBindFramebuffer(GL_FRAMEBUFFER, view);
		if ( !fresh ) {
			depthBlitRefused = true;
			common->Warning("_currentDepth: the driver refused the depth blit; particles stay hard");
			return;
		}
		depthCopied = true;
		return;
	}
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depthCopyFbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, view);
	// the blit is scissored like a draw, and the last surface's scissor is set
	qglDisable(GL_SCISSOR_TEST);
	glCaps.BlitFramebuffer(x, y, x + w, y + h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	qglEnable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, view);
	depthCopied = true;
}

/*
=====================
RB_GLSL_FillDepthBuffer

If we are rendering a subview with a near clip plane, use a second texture
to force the alpha test to fail when behind that clip plane
=====================
*/
void RB_GLSL_FillDepthBuffer(drawSurf_t** drawSurfs, int numDrawSurfs) {

	//////////////
	// Skip cases
	//////////////

	// if we are just doing 2D rendering, no need to fill the depth buffer
	if ( !backEnd.viewDef->viewEntitys ) {
		return;
	}

	////////////////////////////////////////
	// GL Shader setup for the current pass
	// (ie. common to each surface)
	////////////////////////////////////////

	// Expected client GL State at this point
	// Tex0 active
	// Vertex attribute enabled
	// Color attribute enabled
	// Shader AlphaTest is one
	// Shader Texture Matrix is Identity

	// If clip planes are enabled in the view, use he "Clip" version of zfill shader
	// and enable the second texture for mirror plane clipping if needed
	if ( backEnd.viewDef->numClipPlanes ) {
		// Use he zfillClip shader
		GL_UseProgram(&zfillClipShader);

		// Bind the Texture 1 to alphaNotchImage
		GL_SelectTexture(1);
		globalImages->alphaNotchImage->Bind();

		// Be sure to reactivate Texture 0, as it will be bound right after
		GL_SelectTexture(0);
	}
	// If no clip planes, just use the regular zfill shader
	else {
		// opaque surfaces draw without discard; an alpha-tested stage switches
		// to zfillShader for itself (RB_GLSL_DepthFillProgram)
		GL_UseProgram(&zfillOpaqueShader);
	}


	// Setup attributes arrays
	// Vertex attribute is always enabled
	// TexCoord attribute is always enabled
	// Disable Color attribute (as it is enabled by default)
	GL_DisableVertexAttribArray(ATTR_COLOR);

	// Texture 0 will be used for alpha tested surfaces. It should be already active.
	// Bind it to white image by default
	globalImages->whiteImage->Bind();

	// Decal surfaces may enable polygon offset
	// GAB Note: Looks like it is not needed, because in case of offsetted surface we will use the offset value of the surface
	// qglPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat());

	// Depth func to LESS
	GL_State(GLS_DEPTHFUNC_LESS);

	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	qglEnable(GL_STENCIL_TEST);
	qglStencilFunc(GL_ALWAYS, 1, 255);

	//////////////////////////
	// For each surfaces loop
	//////////////////////////

	// Optimization to only change MVP matrix when needed
	backEnd.currentSpace = NULL;

	// the opaque surfaces in runs, when the view has no clip plane
	const bool runs = RB_GLSL_Batching() && !backEnd.viewDef->numClipPlanes;
	const viewEntity_t* singleSpace = NULL;	// what the single-draw programs' uniforms hold

	for ( int i = 0; i < numDrawSurfs; i++ ) {

		const drawSurf_t* const drawSurf = drawSurfs[i];

		if ( runs ) {
			const bool depthRange = drawSurf->space->weaponDepthHack && drawSurf->space->modelDepthHack == 0.0f;
			const srfTriangles_t* const tri = drawSurf->geo;

			RB_GLSL_BatchScissor(drawSurf);
			if ( RB_GLSL_DepthFillRun(drawSurf)
					&& RB_GLSL_BatchAdd(&zfillOpaqueBatchShader, tri->ambientCache, sizeof(idDrawVert), tri->indexCache,
					                    tri->numIndexes, -1, depthRange, drawSurf->space, NULL) ) {
				backEnd.pc.c_drawElements++;
				backEnd.pc.c_drawIndexes += tri->numIndexes;
				backEnd.pc.c_drawVertexes += tri->numVerts;
				backEnd.pc.c_vboIndexes += tri->numIndexes;
				continue;
			}

			// on its own, through the single-draw programs
			RB_GLSL_BatchFlush();
			GL_UseProgram(&zfillOpaqueShader);
			backEnd.currentSpace = singleSpace;
			if ( drawSurf->space != backEnd.currentSpace ) {
				RB_GLSL_SetSpace(drawSurf->space);
			}
			if ( depthRange ) {
				qglDepthRangef(0, 0.5);
			}
			RB_T_GLSL_FillDepthBuffer(drawSurf);
			if ( depthRange ) {
				qglDepthRangef(0.0f, 1.0f);
			}
			backEnd.currentSpace = drawSurf->space;
			singleSpace = drawSurf->space;
			continue;
		}

		///////////////////////////////////////////
		// GL shader setup for the current surface
		///////////////////////////////////////////

		// Change the MVP matrix if needed
		if ( drawSurf->space != backEnd.currentSpace ) {
			RB_GLSL_SetSpace(drawSurf->space);
		}

		// Hack Depth Range if necessary
		bool bNeedRestoreDepthRange = false;
		if (drawSurf->space->weaponDepthHack && drawSurf->space->modelDepthHack == 0.0f) {
			qglDepthRangef(0, 0.5);
			bNeedRestoreDepthRange = true;
		}

		// change the scissor if needed
		if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals(drawSurf->scissorRect)) {
			backEnd.currentScissor = drawSurf->scissorRect;
			if (( backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1 ) < 0.0f ||
			        ( backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 ) < 0.0f ) {
				backEnd.currentScissor = backEnd.viewDef->scissor;
			}
			qglScissor(backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			           backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			           backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			           backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1);
		}

		////////////////////
		// Do the real work
		////////////////////
		RB_T_GLSL_FillDepthBuffer(drawSurf);

		/////////////////////////////////////////////
		// Restore everything to an acceptable state
		/////////////////////////////////////////////

		if (bNeedRestoreDepthRange) {
			qglDepthRangef(0.0f, 1.0f);
		}

		// Let's change space for next iteration
		backEnd.currentSpace = drawSurf->space;
	}

	RB_GLSL_BatchFlush();

	/////////////////////////////////////////////
	// Restore everything to an acceptable state
	/////////////////////////////////////////////
	// Restore current space to NULL
	backEnd.currentSpace = NULL;

	// Restore attributes arrays
	// Vertex attribute is always enabled
	// TexCoord attribute is always enabled
	// Re-enable Color attribute (as it is enabled by default)
	GL_EnableVertexAttribArray(ATTR_COLOR);

	// the early depth, for the stages that read it (#3877)
	RB_GLSL_CaptureDepth();
}

/*
==================
RB_GLSL_DrawStageProgram

A stage through one of the custom stage programs: the surface's
attributes, the stage's images on their units, its vertex parms, and the
view's projection and _currentRender placement.  A post-process stage
reads the scene back, so its epilogue leaves the encoding alone.
==================
*/
static void RB_GLSL_DrawStageProgram(const drawSurf_t* surf, const shaderStage_t* pStage, int programId,
                                     idImage* const* images, int numImages, const idDrawVert* ac,
                                     const idVec4& localViewOrigin) {
	shaderProgram_t* const program = &stageShaders[programId];
	const float* const regs = surf->shaderRegisters;
	const newShaderStage_t* const newStage = pStage->newStage;
	const bool tangents = programId == GLSL_STAGE_BUMPY_ENVIRONMENT;
	float parms[MAX_VERTEX_PARMS][4];
	float hp[4];
	int i;

	GL_UseProgram(program);
	RB_GLSL_SetSpace(surf->space);

	GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 3, GL_FLOAT, false, sizeof(idDrawVert),
	                       RB_DRAWVERT( ac, xyz));
	GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_TexCoord), 2, GL_FLOAT, false, sizeof(idDrawVert),
	                       RB_DRAWVERT( ac, st));
	GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Color), 4, GL_UNSIGNED_BYTE, false, sizeof(idDrawVert),
	                       RB_DRAWVERT( ac, color ));
	if ( tangents ) {
		GL_EnableVertexAttribArray(ATTR_NORMAL);
		GL_EnableVertexAttribArray(ATTR_TANGENT);
		GL_EnableVertexAttribArray(ATTR_BITANGENT);
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Normal), 3, GL_FLOAT, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, normal));
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Tangent), 3, GL_FLOAT, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, tangents[0]));
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Bitangent), 3, GL_FLOAT, false, sizeof(idDrawVert),
		                       RB_DRAWVERT( ac, tangents[1]));
	}

	GL_UniformMatrix4fv(offsetof(shaderProgram_t, modelViewMatrix), surf->space->modelViewMatrix);
	GL_UniformMatrix4fv(offsetof(shaderProgram_t, projectionMatrix), backEnd.viewDef->projectionMatrix);
	GL_Uniform4fv(offsetof(shaderProgram_t, localViewOrigin), localViewOrigin.ToFloatPtr());
	if ( newStage && program->vertexParms >= 0 && newStage->numVertexParms > 0 ) {
		for ( i = 0; i < newStage->numVertexParms; i++ ) {
			parms[i][0] = regs[newStage->vertexParms[i][0]];
			parms[i][1] = regs[newStage->vertexParms[i][1]];
			parms[i][2] = regs[newStage->vertexParms[i][2]];
			parms[i][3] = regs[newStage->vertexParms[i][3]];
		}
		qglUniform4fv(program->vertexParms, newStage->numVertexParms, &parms[0][0]);
	}
	if ( program->screenParms >= 0 ) {
		const float w = (float)( backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1 );
		const float h = (float)( backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
		float sp[4];
		sp[0] = 1.0f / w;
		sp[1] = 1.0f / h;
		sp[2] = w / globalImages->currentRenderImage->uploadWidth;
		sp[3] = h / globalImages->currentRenderImage->uploadHeight;
		GL_Uniform4fv(offsetof(shaderProgram_t, screenParms), sp);
	}
	if ( surf->material->GetSort() >= SS_POST_PROCESS ) {
		RB_GLSLEpilogueParms( true, hp );
		hp[0] = hp[2] = hp[3] = 1.0f;
	} else {
		RB_GLSLEpilogueParms( RB_FilterBlend( pStage->drawStateBits ), hp );
	}
	GL_Uniform4fv(offsetof(shaderProgram_t, hdrParms), hp);

	for ( i = 0; i < numImages; i++ ) {
		if ( images[i] ) {
			GL_SelectTexture(i);
			images[i]->Bind();
		}
	}
	GL_SelectTexture(0);

	GL_State(pStage->drawStateBits);
	RB_DrawElementsWithCounters(surf->geo);

	for ( i = 1; i < numImages; i++ ) {
		if ( images[i] ) {
			GL_SelectTexture(i);
			globalImages->BindNull();
		}
	}
	GL_SelectTexture(0);
	if ( tangents ) {
		GL_DisableVertexAttribArray(ATTR_NORMAL);
		GL_DisableVertexAttribArray(ATTR_TANGENT);
		GL_DisableVertexAttribArray(ATTR_BITANGENT);
	}
}

/*
==================
RB_GLSL_DrawSoftParticle

A particle stage through the softening program (#3878), with the fade the
ARB2 path gives it: over the particle's diameter for an alpha blend, its
radius for an additive one, on the channels the blend reads.
==================
*/
static void RB_GLSL_DrawSoftParticle(const drawSurf_t* surf, const shaderStage_t* pStage, const idDrawVert* ac,
                                     const float color[4]) {
	static const GLfloat zero[1] = { 0 };
	static const GLfloat one[1] = { 1 };
	static const GLfloat oneScaled[1] = { 1 / 255.0f };
	static const GLfloat negOneScaled[1] = { -1.0f / 255.0f };
	static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const int srcBlend = pStage->drawStateBits & GLS_SRCBLEND_BITS;
	const float radius = surf->particle_radius;
	const float fadeRange = srcBlend == GLS_SRCBLEND_SRC_ALPHA ? radius * 2.0f : radius;
	float parms[2][4];
	float sp[4];
	float hp[4];

	GL_UseProgram(&softParticleShader);
	// the softening replaces the model depth hack; the weapon's stays
	{
		const float p14 = backEnd.viewDef->projectionMatrix[14];
		const float hack = surf->space->weaponDepthHack ? p14 * 0.25f - p14 : 0.0f;
		GL_UniformMatrix4fv(offsetof(shaderProgram_t, modelMatrix), surf->space->modelMatrix);
		GL_Uniform1fv(offsetof(shaderProgram_t, depthHack), &hack);
	}
	GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 3, GL_FLOAT, false, sizeof(idDrawVert),
	                       RB_DRAWVERT( ac, xyz));
	GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_TexCoord), 2, GL_FLOAT, false, sizeof(idDrawVert),
	                       RB_DRAWVERT( ac, st));
	GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Color), 4, GL_UNSIGNED_BYTE, false, sizeof(idDrawVert),
	                       RB_DRAWVERT( ac, color ));

	if ( pStage->texture.hasMatrix ) {
		float matrix[16];
		RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, matrix);
		GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), matrix);
	} else {
		GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), mat4_identity.ToFloatPtr());
	}
	// the particle system fades through the vertex colour; a stage that
	// ignores it takes the material's colour instead
	if ( pStage->vertexColor == SVC_IGNORE ) {
		GL_Uniform4fv(offsetof(shaderProgram_t, glColor), color);
		GL_Uniform1fv(offsetof(shaderProgram_t, colorModulate), zero);
		GL_Uniform1fv(offsetof(shaderProgram_t, colorAdd), one);
	} else {
		GL_Uniform4fv(offsetof(shaderProgram_t, glColor), white);
		GL_Uniform1fv(offsetof(shaderProgram_t, colorModulate),
		              pStage->vertexColor == SVC_INVERSE_MODULATE ? negOneScaled : oneScaled);
		GL_Uniform1fv(offsetof(shaderProgram_t, colorAdd), pStage->vertexColor == SVC_INVERSE_MODULATE ? one : zero);
	}

	parms[0][0] = radius;
	parms[0][1] = 1.0f / fadeRange;
	parms[0][2] = 1.0f / radius;
	parms[0][3] = 0.0f;
	// an alpha blend fades its alpha, an additive one its colour
	parms[1][0] = parms[1][1] = parms[1][2] = srcBlend == GLS_SRCBLEND_SRC_ALPHA ? 1.0f : 0.0f;
	parms[1][3] = srcBlend == GLS_SRCBLEND_SRC_ALPHA ? 0.0f : 1.0f;
	qglUniform4fv(softParticleShader.vertexParms, 2, &parms[0][0]);

	sp[0] = 1.0f / globalImages->currentDepthImage->uploadWidth;
	sp[1] = 1.0f / globalImages->currentDepthImage->uploadHeight;
	sp[2] = sp[3] = 0.0f;
	GL_Uniform4fv(offsetof(shaderProgram_t, screenParms), sp);

	// the scene encoding fold alone: no gamma or clamp on a blended particle
	hp[0] = hp[1] = hp[2] = hdr_scene_encode_scale;
	hp[3] = 1.0f;
	GL_Uniform4fv(offsetof(shaderProgram_t, hdrParms), hp);

	GL_SelectTexture(1);
	globalImages->currentDepthImage->Bind();
	GL_SelectTexture(0);
	pStage->texture.image->Bind();

	// the program decides the depth test, so the particle can overdraw
	GL_State(pStage->drawStateBits | GLS_DEPTHFUNC_ALWAYS);
	RB_DrawElementsWithCounters(surf->geo);

	GL_SelectTexture(1);
	globalImages->BindNull();
	GL_SelectTexture(0);
}

/*
==================
RB_GLSL_T_RenderShaderPasses

This is also called for the generated 2D rendering
==================
*/
void RB_GLSL_T_RenderShaderPasses(const drawSurf_t* surf) {

	// global constants
	static const GLfloat zero[1] = { 0 };
	static const GLfloat one[1] = { 1 };
	static const GLfloat oneScaled[1] = { 1 / 255.0f };
	static const GLfloat negOneScaled[1] = { -1.0f / 255.0f };

	// usefull pointers
	const idMaterial* const shader = surf->material;
//	const srfTriangles_t* const tri = surf->geo;

	//////////////
	// Skip cases
	//////////////

#ifdef NO_LIGHT
	if ( !r_noLight.GetBool() )
#endif
	if ( !shader->HasAmbient()) {
		return;
	}

	if ( shader->IsPortalSky()) {
		return;
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if ( !surf->geo->numIndexes ) {
		return;
	}

	if ( !surf->geo->ambientCache ) {
		return;
	}


	///////////////////////////////////
	// GL shader setup for the surface
	// (ie. common to each Stage)
	///////////////////////////////////

	// change the scissor if needed
	if ( r_useScissor.GetBool() &&
	    !backEnd.currentScissor.Equals(surf->scissorRect)) {
		backEnd.currentScissor = surf->scissorRect;
		if (( backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1 ) < 0.0f ||
		        ( backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 ) < 0.0f ) {
			backEnd.currentScissor = backEnd.viewDef->scissor;
		}
		qglScissor(backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
		           backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
		           backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
		           backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1);

	}

	// set polygon offset if necessary
	// NB: must be restored at end of process
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
		qglEnable(GL_POLYGON_OFFSET_FILL);
		qglPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
	}

	// set face culling appropriately
	GL_Cull(shader->GetCullType());

	// Location of vertex attributes data
	const idDrawVert* const ac = (const idDrawVert* const) RB_GLSL_VertexBase(surf->geo->ambientCache, sizeof(idDrawVert));

	// get the expressions for conditionals / color / texcoords
	const float* const regs = surf->shaderRegisters;

	// Caches to set per surface shader GL state only when necessary
	bool bMVPSet[TG_GLASSWARP-TG_EXPLICIT];
	memset(bMVPSet, 0, (TG_GLASSWARP-TG_EXPLICIT)*sizeof(bool));
	bool bVASet [TG_GLASSWARP-TG_EXPLICIT];
	memset(bVASet, 0, (TG_GLASSWARP-TG_EXPLICIT)*sizeof(bool));

	// precompute the local view origin (might be needed for some texgens)
	idVec4 localViewOrigin;
	R_GlobalPointToLocal(surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3());
	localViewOrigin.w = 1.0f;

	// softened particles: flagged by the front end, with the view's depth captured (#3878)
	const bool softParticle = ( surf->dsFlags & DSF_SOFT_PARTICLE ) != 0 && surf->particle_radius > 0.0f
		&& depthCopied && softParticleShader.program && !r_skipNewAmbient.GetBool();

	// the GUI correction LUT, when the surface is a GUI and the output transform asks for it
	unsigned int guiLut = 0;
	if ( shader->GetSort() == SS_GUI ) {
		if ( !HDR_GuiCorrectionActive( &guiLut ) ) {
			guiLut = 0;
		}
	}

	///////////////////////
	// For each stage loop
	///////////////////////
	for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {

		const shaderStage_t* const pStage = shader->GetStage(stage);

		///////////////
		// Skip cases
		///////////////

		// check the enable condition
		if ( regs[pStage->conditionRegister] == 0 ) {
			continue;
		}

		// skip the stages involved in lighting
		if ( pStage->lighting != SL_AMBIENT ) {
			continue;
		}

		// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
		if (( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS )) ==
		        ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE )) {
			continue;
		}

		// see if we are a new-style stage
		const newShaderStage_t* const newStage = pStage->newStage;

		if ( newStage ) {
			// new style stages: the ones the game data names have programs here
			if ( r_skipNewAmbient.GetBool() || newStage->glslProgram == GLSL_STAGE_NONE
					|| !stageShaders[newStage->glslProgram].program ) {
				continue;
			}
			RB_GLSL_DrawStageProgram(surf, pStage, newStage->glslProgram, newStage->fragmentProgramImages,
			                         newStage->numFragmentProgramImages, ac, localViewOrigin);
		} else if ( softParticle && ( ( pStage->drawStateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_ONE
				|| ( pStage->drawStateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_SRC_ALPHA ) ) {
			// softened by the engine, unless the material brought its own program (above)
			const deform_t dfm = surf->material->Deform();
			const float gain = ( dfm == DFRM_PARTICLE || dfm == DFRM_PARTICLE2 )
				? hdr_particle_gain : hdr_emissive_gain;
			float color[4];
			color[0] = regs[pStage->color.registers[0]];
			color[1] = regs[pStage->color.registers[1]];
			color[2] = regs[pStage->color.registers[2]];
			color[3] = regs[pStage->color.registers[3]];
			// an additive particle is emitted light: the emissive headroom
			if ( hdr_output_active && gain != 1.0f
					&& ( pStage->drawStateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE ) {
				color[0] *= gain;
				color[1] *= gain;
				color[2] *= gain;
			}
			RB_GLSL_DrawSoftParticle(surf, pStage, ac, color);
		} else {

			// old style stages

			/////////////////////////
			// Additional skip cases
			/////////////////////////

			// precompute the color
			const float color[4] = {
				regs[pStage->color.registers[0]],
				regs[pStage->color.registers[1]],
				regs[pStage->color.registers[2]],
				regs[pStage->color.registers[3]]
			};

			// skip the entire stage if an add would be black
			if (
			    ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS )) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE )
			    && color[0] <= 0 && color[1] <= 0 && color[2] <= 0 ) {
				continue;
			}

			// skip the entire stage if a blend would be completely transparent
			if (( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS )) ==
			        ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
			        && color[3] <= 0 ) {
				continue;
			}

			/////////////////////////////////
			// GL shader setup for the stage
			/////////////////////////////////
			// The very first thing we need to do before going down into GL is to choose he correct GLSL shader depending on
			// the associated TexGen. Then, setup its specific uniforms/attribs, and then only we can setup the common uniforms/attribs

			if ( pStage->texture.texgen == TG_DIFFUSE_CUBE ) {
				// Not used in game, but implemented because trivial

				// This is diffuse cube mapping
				GL_UseProgram(&diffuseCubeShader);


				// Possible that normals should be transformed by a normal matrix in the shader ? I am not sure...

				// Setup texcoord array to use the normals
				GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_TexCoord), 3, GL_FLOAT, false, sizeof(idDrawVert),
				                       RB_DRAWVERT( ac, normal));

				// Setup the texture matrix
				if ( pStage->texture.hasMatrix ) {
					float matrix[16];
					RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, matrix);
					GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), matrix);
				}
			} else if ( pStage->texture.texgen == TG_SKYBOX_CUBE ) {
				// This is skybox cube mapping
				GL_UseProgram(&skyboxCubeShader);


				// Disable TexCoord attribute
				GL_DisableVertexAttribArray(ATTR_TEXCOORD);

				// Setup the local view origin uniform
				GL_Uniform4fv(offsetof(shaderProgram_t, localViewOrigin), localViewOrigin.ToFloatPtr());

				// Setup the texture matrix
				if ( pStage->texture.hasMatrix ) {
					float matrix[16];
					RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, matrix);
					GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), matrix);
				}
			} else if ( pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {
				// This is skybox cube mapping, with special texture matrix
				GL_UseProgram(&skyboxCubeShader);


				// Disable TexCoord attribute
				GL_DisableVertexAttribArray(ATTR_TEXCOORD);

				// Setup the local view origin uniform
				GL_Uniform4fv(offsetof(shaderProgram_t, localViewOrigin), localViewOrigin.ToFloatPtr());

				// Setup the texture matrix
				// Note: here, we combine the shader texturematrix and precomputed wobblesky matrix
				if ( pStage->texture.hasMatrix ) {
					float texturematrix[16];
					RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, texturematrix);
					float finalmatrix[16];
					myGlMultMatrix(texturematrix, surf->wobbleTransform, finalmatrix);
					GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), finalmatrix);
				} else {
					GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), surf->wobbleTransform);
				}
			} else if ( pStage->texture.texgen == TG_SCREEN ) {
				// Not used in game, so not implemented
				continue;
			} else if ( pStage->texture.texgen == TG_SCREEN2 ) {
				// Not used in game, so not implemented
				continue;
			} else if ( pStage->texture.texgen == TG_GLASSWARP ) {
				// Not used in game, so not implemented. The shader code is even not present in original D3 data
				continue;
			} else if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {
				// This is reflection cubemapping
				// with a bump map: per-pixel through the normal map, as ARB2's bumpy environment
				const shaderStage_t* const bumpStage = shader->GetBumpStage();
				if ( bumpStage && stageShaders[GLSL_STAGE_BUMPY_ENVIRONMENT].program ) {
					idImage* images[2];
					images[0] = pStage->texture.image;
					images[1] = bumpStage->texture.image;
					RB_GLSL_DrawStageProgram(surf, pStage, GLSL_STAGE_BUMPY_ENVIRONMENT, images, 2, ac, localViewOrigin);
					continue;
				}
				GL_UseProgram(&reflectionCubeShader);


				// NB: in original D3, if the surface had a bump map it would lead to the "Bumpy reflection cubemaping" shader being used.
				// This is not implemented for now, we only do standard reflection cubemaping. Visual difference is really minor.

				// Setup texcoord array to use the normals
				GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_TexCoord), 3, GL_FLOAT, false, sizeof(idDrawVert),
				                       RB_DRAWVERT( ac, normal));

				// Setup the viewMatrix, we will need it to compute the reflection
				GL_UniformMatrix4fv(offsetof(shaderProgram_t, modelViewMatrix), surf->space->modelViewMatrix);

				// Setup the texture matrix like original D3 code does: using the transpose viewMatrix of the view
				// NB: this is curious, not sure why this is done like this....
				float mat[16];
				R_TransposeGLMatrix(backEnd.viewDef->worldSpace.modelViewMatrix, mat);
				GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), mat);
			} else { // TG_EXPLICIT
				// Otherwise, this is just regular surface shader with explicit texcoords
				/* An in-world GUI is authored in display space; under the
				   ACES 2.0 output transform its texel goes through the
				   transform's inverse first (the LUT on unit 1), so the
				   forward transform puts it back.  Only SS_GUI pays for
				   the fetches. */
				if ( guiLut ) {
					GL_UseProgram(&diffuseMapGuiShader);
					GL_SelectTexture(1);
					qglBindTexture(GL_TEXTURE_2D, guiLut);
					backEnd.glState.tmu[1].current2DMap = -1;
					GL_SelectTexture(0);
				} else {
					GL_UseProgram(&diffuseMapShader);
				}


				// Setup the TexCoord pointer
				GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_TexCoord), 2, GL_FLOAT, false, sizeof(idDrawVert),
				                       RB_DRAWVERT( ac, st));

				// Setup the texture matrix
				if ( pStage->texture.hasMatrix ) {
					float matrix[16];
					RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, matrix);
					GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), matrix);
				}
			}

			// Now we have a shader, we can setup the uniforms and attribute pointers common to all kind of shaders
			// The specifics have already been done in the shader selection code (see above)

			// Non-stage dependent state (per drawsurf, may be done once per GL shader)
			{
				// Vertex Attributes
				if ( !bVASet[pStage->texture.texgen] ) {

					// Setup the Vertex Attrib pointer
					GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 3, GL_FLOAT, false, sizeof(idDrawVert),
					                       RB_DRAWVERT( ac, xyz));

					// Setup the Color pointer
					GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Color), 4, GL_UNSIGNED_BYTE, false, sizeof(idDrawVert),
					                       RB_DRAWVERT( ac, color ));

					bVASet[pStage->texture.texgen] = true;
				}

				// MVP
				if ( !bMVPSet[pStage->texture.texgen] ) {
					RB_GLSL_SetSpace(surf->space);

					bMVPSet[pStage->texture.texgen] = true;
				}
			}

			// Stage dependent state

			/* Emissive headroom, the rule draw_common.cpp and GLES2 apply:
			   an additive stage is emitted light, scaled by the emissive
			   gain (particles by their own) in HDR; alpha is left alone. */
			float emissive[4];
			const float *stageColor = color;
			{
				const int blend = pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
				const deform_t dfm = surf->material->Deform();
				const float gain = ( dfm == DFRM_PARTICLE || dfm == DFRM_PARTICLE2 )
					? hdr_particle_gain : hdr_emissive_gain;
				if ( hdr_output_active && gain != 1.0f
						&& ( blend & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE ) {
					emissive[0] = color[0] * gain;
					emissive[1] = color[1] * gain;
					emissive[2] = color[2] * gain;
					emissive[3] = color[3];
					stageColor = emissive;
				}
			}

			// Setup the Color uniform
			GL_Uniform4fv(offsetof(shaderProgram_t, glColor), stageColor);

			// the epilogue: a filter blend's source is a factor, so it skips the
			// fold; a reflection is scene energy whatever its blend
			if ( backEnd.glState.currentProgram->hdrParms >= 0 ) {
				float hp[4];
				RB_GLSLEpilogueParms( pStage->texture.texgen != TG_REFLECT_CUBE
				                      && RB_FilterBlend( pStage->drawStateBits ), hp );
				GL_Uniform4fv(offsetof(shaderProgram_t, hdrParms), hp);
			}

			// Setup the Color modulation
			switch ( pStage->vertexColor ) {
			case SVC_MODULATE: {
				GL_Uniform1fv(offsetof(shaderProgram_t, colorModulate), oneScaled);
				GL_Uniform1fv(offsetof(shaderProgram_t, colorAdd), zero);
				break;
			}
			case SVC_INVERSE_MODULATE: {
				GL_Uniform1fv(offsetof(shaderProgram_t, colorModulate), negOneScaled);
				GL_Uniform1fv(offsetof(shaderProgram_t, colorAdd), one);
				break;
			}
			default:
			case SVC_IGNORE:
				// This is already the default values (zero, one)
				break;
			}

			// bind the texture (this will be either a dynamic texture, or a static one)
			RB_BindVariableStageImage(&pStage->texture, regs);

			// set the state
			GL_State(pStage->drawStateBits);

			// set privatePolygonOffset if necessary
			if ( pStage->privatePolygonOffset ) {
				qglEnable(GL_POLYGON_OFFSET_FILL);
				qglPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset);
			}

#ifdef NO_LIGHT
			if (r_noLight.GetBool() )
			{
				if (pStage->drawStateBits!=9000)
	                GL_State(pStage->drawStateBits);
	            else
	            {
	                if (shader->TestMaterialFlag(MF_POLYGONOFFSET))
	                    GL_State(GLS_SRCBLEND_ONE|GLS_DSTBLEND_ONE|GLS_DEPTHFUNC_LESS);
	                else
	                    GL_State(GLS_SRCBLEND_ONE|GLS_DSTBLEND_ONE|GLS_DEPTHFUNC_LESS);
	            }
            }
#endif
			/////////////////////
			// Draw the surface!
			/////////////////////
			RB_DrawElementsWithCounters(surf->geo);

			/////////////////////////////////////////////
			// Restore everything to an acceptable state
			/////////////////////////////////////////////

			// Disable the other attributes array
			if ( pStage->texture.texgen == TG_DIFFUSE_CUBE ) {
				// Restore identity to the texture matrix
				if ( pStage->texture.hasMatrix) {
					GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), mat4_identity.ToFloatPtr());
				}
			} else if ( pStage->texture.texgen == TG_SKYBOX_CUBE ) {
				// Reenable TexCoord attribute
				GL_EnableVertexAttribArray(ATTR_TEXCOORD);

				// Restore identity to the texture matrix
				if ( pStage->texture.hasMatrix) {
					GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), mat4_identity.ToFloatPtr());
				}
			} else if ( pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {
				// Reenable TexCoord attribute
				GL_EnableVertexAttribArray(ATTR_TEXCOORD);

				// Restore identity to the texture matrix (shall be done each time, as there is the wobblesky transform combined inside)
				GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), mat4_identity.ToFloatPtr());
			} else if ( pStage->texture.texgen == TG_SCREEN ) {
			} else if ( pStage->texture.texgen == TG_SCREEN2 ) {
			} else if ( pStage->texture.texgen == TG_GLASSWARP ) {
			} else if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {
				// Restore identity to the texture matrix (shall be done each time)
				GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), mat4_identity.ToFloatPtr());
			} else {
				// Restore identity to the texture matrix
				if ( pStage->texture.hasMatrix) {
					GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), mat4_identity.ToFloatPtr());
				}
			}

			// unset privatePolygonOffset if necessary
			if ( pStage->privatePolygonOffset && !surf->material->TestMaterialFlag(MF_POLYGONOFFSET)) {
				qglDisable(GL_POLYGON_OFFSET_FILL);
			} else if ( pStage->privatePolygonOffset && surf->material->TestMaterialFlag(MF_POLYGONOFFSET)) {
				qglPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
			}

			// Restore color modulation state to default values
			if ( pStage->vertexColor != SVC_IGNORE ) {
				GL_Uniform1fv(offsetof(shaderProgram_t, colorModulate), zero);
				GL_Uniform1fv(offsetof(shaderProgram_t, colorAdd), one);
			}

			// Don't touch the rest, as this will either reset by the next stage, or handled by end of this method
		}
	}

	/////////////////////////////////////////////
	// Restore everything to an acceptable state
	/////////////////////////////////////////////

	// reset polygon offset
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
		qglDisable(GL_POLYGON_OFFSET_FILL);
	}
}

/*
=====================
RB_GLSL_DrawShaderPasses

Draw non-light dependent passes
=====================
*/
int RB_GLSL_DrawShaderPasses(drawSurf_t** drawSurfs, int numDrawSurfs) {

	//////////////
	// Skip cases
	//////////////

	// only obey skipAmbient if we are rendering a view
	if ( backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool()) {
		return numDrawSurfs;
	}

	// if we are about to draw the first surface that needs
	// the rendering in a texture, copy it over
	if ( drawSurfs[0]->material->GetSort() >= SS_POST_PROCESS ) {
		if ( r_skipPostProcess.GetBool()) {
			return 0;
		}

		// only dump if in a 3d view
		if ( backEnd.viewDef->viewEntitys ) {
			globalImages->currentRenderImage->CopyFramebuffer(backEnd.viewDef->viewport.x1,
			                                                  backEnd.viewDef->viewport.y1,
			                                                  backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
			                                                  backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1,
			                                                  true);
		}

		backEnd.currentRenderCopied = true;
	}

	////////////////////////////////////////
	// GL shader setup for the current pass
	// (ie. common to each surface)
	////////////////////////////////////////

	// Texture 0 is expected to be active

	// Setup attributes arrays
	// Vertex attribute is always enabled
	// Color attribute is always enabled
	// Texcoord attribute is always enabled

	/////////////////////////
	// For each surface loop
	/////////////////////////

	backEnd.currentSpace = NULL;

	int i;
	for ( i = 0; i < numDrawSurfs; i++ ) {

		//////////////
		// Skip cases
		//////////////

		if ( drawSurfs[i]->material->SuppressInSubview()) {
			continue;
		}


		if ( backEnd.viewDef->isXraySubview && drawSurfs[i]->space->entityDef ) {
			if ( drawSurfs[i]->space->entityDef->parms.xrayIndex != 2 ) {
				continue;
			}
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if ( drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS
		        && !backEnd.currentRenderCopied ) {
			break;
		}


		// Hack Depth Range if necessary
		bool bNeedRestoreDepthRange = false;
		if (drawSurfs[i]->space->weaponDepthHack && drawSurfs[i]->space->modelDepthHack == 0.0f) {
			qglDepthRangef(0.0f, 0.5f);
			bNeedRestoreDepthRange = true;
		}

		////////////////////
		// Do the real work
		////////////////////
		RB_GLSL_T_RenderShaderPasses(drawSurfs[i]);

		if (bNeedRestoreDepthRange) {
			qglDepthRangef(0.0f, 1.0f);
		}

		backEnd.currentSpace = drawSurfs[i]->space;
	}

	/////////////////////////////////////////////
	// Restore everything to an acceptable state
	/////////////////////////////////////////////

	backEnd.currentSpace = NULL;

	// Restore culling
	GL_Cull(CT_FRONT_SIDED);

	// Restore attributes arrays
	// Vertex attribute is always enabled
	// Color attribute is always enabled
	// Texcoord attribute is always enabled

	// Trashed state:
	//   Current Program
	//   Tex0 binding

	// Return the counter of drawn surfaces
	return i;
}

/*
=====================
RB_T_BlendLight

=====================
*/
static void RB_T_GLSL_BlendLight(const drawSurf_t *surf, const viewLight_t* vLight) {
//	const srfTriangles_t *tri = surf->geo;

	////////////
	// GL setup
	////////////

	// Shader uniforms

	// Setup the fogMatrix as being the local Light Projection
	// Only do this once per space
	if (backEnd.currentSpace != surf->space) {
		idPlane lightProject[4];

		int i;
		for (i = 0; i < 4; i++) {
			R_GlobalPlaneToLocal(surf->space->modelMatrix, vLight->lightProject[i], lightProject[i]);
		}

		idMat4 fogMatrix;
		fogMatrix[0] = lightProject[0].ToVec4();
		fogMatrix[1] = lightProject[1].ToVec4();
		fogMatrix[2] = lightProject[2].ToVec4();
		fogMatrix[3] = lightProject[3].ToVec4();
		GL_UniformMatrix4fv(offsetof(shaderProgram_t, fogMatrix), fogMatrix.ToFloatPtr());
	}

	// Attributes pointers

	// This gets used for both blend lights and shadow draws
	if (surf->geo->ambientCache) {
		idDrawVert *ac = (idDrawVert *) RB_GLSL_VertexBase(surf->geo->ambientCache, sizeof(idDrawVert));
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 3, GL_FLOAT, false, sizeof(idDrawVert), RB_DRAWVERT( ac, xyz));
	} else if (surf->geo->shadowCache) {
		shadowCache_t *sc = (shadowCache_t *) RB_GLSL_VertexBase(surf->geo->shadowCache, sizeof(shadowCache_t));
		GL_VertexAttribPointer(offsetof(shaderProgram_t, attr_Vertex), 3, GL_FLOAT, false, sizeof(shadowCache_t), RB_MEMBER_AT( sc, shadowCache_t, xyz ));
	}

	////////////////////
	// Draw the surface
	////////////////////
	RB_DrawElementsWithCounters(surf->geo);
}

/*
=====================
RB_GLSL BlendLight

Dual texture together the falloff and projection texture with a blend
mode to the framebuffer, instead of interacting with the surface texture
=====================
*/
void RB_GLSL_BlendLight(const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2, const viewLight_t* vLight) {
	const idMaterial * const lightShader = vLight->lightShader;
	const float * const regs = vLight->shaderRegisters;

	//////////////
	// Skip Cases
	//////////////

	if (!drawSurfs) {
		return;
	}

	if (r_skipBlendLights.GetBool()) {
		return;
	}

	////////////////////////////////////
	// GL setup for the current pass
	// (ie. common to all Light Stages)
	////////////////////////////////////

	// Use blendLight shader
	GL_UseProgram(&blendLightShader);


	// Texture 1 will get the falloff texture
	GL_SelectTexture(1);
	vLight->falloffImage->Bind();

	// Texture 0 will get the projected texture
	GL_SelectTexture(0);

	////////////////////////
	// For each Light Stage
	////////////////////////

	int i;
	for (i = 0; i < lightShader->GetNumStages(); i++) {
		const shaderStage_t *stage = lightShader->GetStage(i);

		//////////////
		// Skip Cases
		//////////////

		if (!regs[stage->conditionRegister]) {
			continue;
		}

		////////////////////////////////////////
		// GL setup for the current Light Stage
		// (ie. common to all surfaces)
		////////////////////////////////////////

		// Global GL state

		// Setup the drawState
		GL_State(GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL);

		// Bind the projected texture
		stage->texture.image->Bind();

		// Shader Uniforms

		// Setup the texture matrix
		if ( stage->texture.hasMatrix ) {
			float matrix[16];
			RB_GetShaderTextureMatrix(regs, &stage->texture, matrix);
			GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), matrix);
		}

		// Setup the Fog Color
		float lightColor[4];
		lightColor[0] = regs[stage->color.registers[0]];
		lightColor[1] = regs[stage->color.registers[1]];
		lightColor[2] = regs[stage->color.registers[2]];
		lightColor[3] = regs[stage->color.registers[3]];
		// an additive blend light is a glow the volume emits: the emissive headroom, as on a material stage
		if ( hdr_output_active && hdr_emissive_gain != 1.0f
				&& ( stage->drawStateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE ) {
			lightColor[0] *= hdr_emissive_gain;
			lightColor[1] *= hdr_emissive_gain;
			lightColor[2] *= hdr_emissive_gain;
		}
		GL_Uniform4fv(offsetof(shaderProgram_t, fogColor), lightColor);

		////////////////////
		// Do the Real Work
		////////////////////

		RB_GLSL_RenderDrawSurfChainWithFunction(drawSurfs, RB_T_GLSL_BlendLight, vLight);
		RB_GLSL_RenderDrawSurfChainWithFunction(drawSurfs2, RB_T_GLSL_BlendLight, vLight);

		////////////////////
		// GL state restore
		////////////////////

		// Restore texture matrix to identity
		if (stage->texture.hasMatrix) {
			GL_UniformMatrix4fv(offsetof(shaderProgram_t, textureMatrix), mat4_identity.ToFloatPtr());
		}
	}
}

/*
==================
RB_FogAllLights
==================
*/
void RB_GLSL_FogAllLights(void) {

	//////////////
	// Skip Cases
	//////////////

	if (r_skipFogLights.GetBool() || backEnd.viewDef->isXraySubview /* dont fog in xray mode*/ ) {
		return;
	}

	/////////////////////////////////////////////
	// GL setup for the current pass
	// (ie. common to both fog and blend lights)
	/////////////////////////////////////////////

	// Disable Stencil Test
	qglDisable(GL_STENCIL_TEST);

	// Disable TexCoord array
	// Disable Color array
	GL_DisableVertexAttribArray(ATTR_TEXCOORD);
	GL_DisableVertexAttribArray(ATTR_COLOR);

	//////////////////
	// For each Light
	//////////////////

	const viewLight_t *vLight;
	for (vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next) {

		//////////////
		// Skip Cases
		//////////////

		// We are only interested in Fog and Blend lights
		if (!vLight->lightShader->IsFogLight() && !vLight->lightShader->IsBlendLight()) {
			continue;
		}

		///////////////////////
		// Do the Light passes
		///////////////////////

		if (vLight->lightShader->IsFogLight()) {
			RB_GLSL_FogPass(vLight->globalInteractions, vLight->localInteractions, vLight);
		} else if (vLight->lightShader->IsBlendLight()) {
			RB_GLSL_BlendLight(vLight->globalInteractions, vLight->localInteractions, vLight);
		}
	}

	////////////////////
	// GL state restore
	////////////////////

	// Re-enable TexCoord array
	// Re-enable Color array
	GL_EnableVertexAttribArray(ATTR_TEXCOORD);
	GL_EnableVertexAttribArray(ATTR_COLOR);

	// Re-enable Stencil Test
	qglEnable(GL_STENCIL_TEST);
}


/*
=================
RB_GLSL_ViewProjection

projection * view for one view, with the eye's clip placement folded into
the x and y rows: x' = s.x * x + o.x * w, y' = s.y * y + o.y * w.
=================
*/
static void RB_GLSL_ViewProjection(const float view[16], const float projection[16],
		const float clip[4], float out[16]) {
	int col;
	myGlMultMatrix(view, projection, out);
	for ( col = 0; col < 4; col++ ) {
		const float w = out[col * 4 + 3];
		out[col * 4 + 0] = clip[0] * out[col * 4 + 0] + clip[2] * w;
		out[col * 4 + 1] = clip[1] * out[col * 4 + 1] + clip[3] * w;
	}
}

/*
=================
RB_BeginGLSLShaderPasses
=================
*/
void RB_GLSL_PrepareShaders(void) {
	static const float noEyeClip[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	float viewMatrices[VIEW_MATRICES_FLOATS];
	const float *view[2];
	const float *clip[2];
	int eye;

	// No shaders set by default
	GL_UseProgram(NULL);

	// the frontend may have moved them since the last view
	RB_GLSL_ForgetAttribPointers();
	backEnd.glState.baseVertex = 0;

#ifdef BOOM3_VR_MULTIVIEW
	// in VR every view draws into 2-layer targets, which only multiview programs can
	if ( VR_Active() != glslMultiview ) {
		glslMultiview = VR_Active();
		if ( !RB_GLSL_InitShaders() ) {
			common->Printf("GLSL shaders failed to init.\n");
		}
	}
#endif

	// One matrix per view for the whole view: the projection is the view's
	// own (the 2D views carry their ortho one), the depth hacks are a
	// per-space uniform.
#ifdef BOOM3_VR_MULTIVIEW
	// subviews inherit vrView 3 for the target, but are drawn from their own one view
	if ( backEnd.viewDef->vrView == 3 && !backEnd.viewDef->isSubview ) {
		view[0] = backEnd.viewDef->vrEyeViewMatrix[0];
		view[1] = backEnd.viewDef->vrEyeViewMatrix[1];
		clip[0] = backEnd.viewDef->vrEyeClip[0];
		clip[1] = backEnd.viewDef->vrEyeClip[1];
		// scissor rects are in the shared projection, which matches neither eye
		qglDisable(GL_SCISSOR_TEST);
	} else
#endif
	{
		// one view: both layers (when there are two) see the same thing
		view[0] = view[1] = backEnd.viewDef->worldSpace.modelViewMatrix;
		clip[0] = clip[1] = noEyeClip;
		qglEnable(GL_SCISSOR_TEST);
	}
	for ( eye = 0; eye < 2; eye++ ) {
		RB_GLSL_ViewProjection(view[eye], backEnd.viewDef->projectionMatrix, clip[eye],
				viewMatrices + eye * 16);
	}
	GL_ViewMatricesUniformBuffer(viewMatrices);

	// Always enable the vertex, color and texcoord attributes arrays
	GL_EnableVertexAttribArray(ATTR_VERTEX);
	GL_EnableVertexAttribArray(ATTR_COLOR);
	GL_EnableVertexAttribArray(ATTR_TEXCOORD);
	// Disable the other arrays
	GL_DisableVertexAttribArray(ATTR_NORMAL);
	GL_DisableVertexAttribArray(ATTR_TANGENT);
	GL_DisableVertexAttribArray(ATTR_BITANGENT);
}

/*
=================
RB_GLSL_EndFrame

Leave the uniform buffer binding points as the frontend expects them: it
shares the context and does not track them.
=================
*/
void RB_GLSL_EndFrame(void) {
	glBindBufferBase(GL_UNIFORM_BUFFER, VIEW_MATRICES_BINDING, 0);
	if ( glslBatches ) {
		glBindBufferBase(GL_UNIFORM_BUFFER, DRAW_LIST_BINDING, 0);
	}
	if ( glslDrawBlocks ) {
		glBindBufferBase(GL_UNIFORM_BUFFER, DRAW_VERTEX_BINDING, 0);
		glBindBufferBase(GL_UNIFORM_BUFFER, DRAW_FRAGMENT_BINDING, 0);
	}
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

} // namespace gles3
