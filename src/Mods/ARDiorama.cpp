#include "ARDiorama.h"
#include "ARScene.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "PvzpLib/PvzpDebug.h"
#include "SexyAppFramework/graphics/GLInterface.h"
#include "SexyAppFramework/graphics/GLPlatform.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <vector>

// The page's diorama (ARViewPage.h, buildDiorama) drawn with the game's own GL: the same layout, cards and taps.
namespace
{
	// Laid out in game pixels like the page: x across (0 is the game's x 400), z toward the player (0 is the game's
	// y 300), y up from the table with the lawn at BASE. PAGE_TO_BOARD takes that to the board's space in meters,
	// its origin on the lawn over the middle of the day's base.
	constexpr float		PIXEL = 0.40f / BOARD_WIDTH;	// meters per game pixel at scale 1: the game is 40 cm wide
	constexpr float		BASE = 32;						// the wooden base under the lawn
	constexpr float		RIM = 10;
	constexpr float		LIP = 46;						// the base's front edge, where the progress lies
	constexpr float		HOUSE = -220;					// the picture's left end, folded up as the house
	constexpr float		LAWN_X0 = -40;					// the lawn across, in game x: the porch to the sidewalk
	constexpr float		LAWN_X1 = BOARD_WIDTH + 60;
	constexpr float		CENTER_X = (LAWN_X0 + LAWN_X1) / 2 - BOARD_WIDTH / 2;
	constexpr float		CENTER_Z = (74 - RIM + BOARD_HEIGHT + RIM + LIP) / 2 - BOARD_HEIGHT / 2;
	constexpr float		BASE_WIDTH = LAWN_X1 - LAWN_X0 + 2 * RIM;
	constexpr float		PANEL_TILT = -0.3f;				// the seed bank leans back
	constexpr float		METER_TILT = -1.5708f + 0.35f;	// the progress lies on the front edge, raised a little
	constexpr int		MAX_FIGURES = 600;
	constexpr int		MAX_QUADS = 2048;
	constexpr uint32_t	BACKDROP_INTERVAL_MS = 250;		// ice and craters change
	constexpr int		ATLAS_WIDTH = ARScene::ATLAS_WIDTH;
	constexpr int		ATLAS_HEIGHT = ARScene::ATLAS_MAX_HEIGHT;
	constexpr int		BACKDROP_X = -BOARD_OFFSET;		// the backdrop's left edge, in game x

	struct Vec3
	{
		float x = 0, y = 0, z = 0;
	};

	Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

	Vec3 Normalized(Vec3 a)
	{
		float aLength = std::sqrt(Dot(a, a));
		return aLength > 1e-9f ? a * (1 / aLength) : Vec3{ 0, 0, 0 };
	}

	// Column-major 4x4 (OpenGL order)
	void Mul(const float a[16], const float b[16], float theOut[16])
	{
		float r[16];
		for (int c = 0; c < 4; c++)
			for (int i = 0; i < 4; i++)
				r[c * 4 + i] = a[i] * b[c * 4] + a[4 + i] * b[c * 4 + 1] + a[8 + i] * b[c * 4 + 2] + a[12 + i] * b[c * 4 + 3];
		memcpy(theOut, r, sizeof(r));
	}

	bool Invert(const float m[16], float theOut[16])
	{
		float inv[16];
		inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
		inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
		inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
		inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
		inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
		inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
		inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
		inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
		inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
		inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
		inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
		inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
		inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
		inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
		inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
		inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
		float aDet = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
		if (std::fabs(aDet) < 1e-20f)
			return false;
		for (int i = 0; i < 16; i++)
			theOut[i] = inv[i] / aDet;
		return true;
	}

	Vec3 TransformPoint(const float m[16], Vec3 p)
	{
		float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
		Vec3 r = { m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13], m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] };
		return std::fabs(w) > 1e-12f ? r * (1 / w) : r;
	}

	Vec3 TransformDir(const float m[16], Vec3 d)
	{
		return { m[0] * d.x + m[4] * d.y + m[8] * d.z, m[1] * d.x + m[5] * d.y + m[9] * d.z, m[2] * d.x + m[6] * d.y + m[10] * d.z };
	}

	// The page's game pixels to the board's meters
	const float PAGE_TO_BOARD[16] = {
		PIXEL, 0, 0, 0,
		0, PIXEL, 0, 0,
		0, 0, PIXEL, 0,
		-CENTER_X * PIXEL, -BASE * PIXEL, -CENTER_Z * PIXEL, 1,
	};

	// Where the lawn lies for each background, in game coordinates: x0..x1 across, from the back edge, where the
	// fence or the sky stands up, to the bottom of the screen.
	struct Layout
	{
		float mX0 = LAWN_X0;
		float mX1 = LAWN_X1;
		float mBack = 74;
		float mFront = BOARD_HEIGHT;
	};

	Layout LawnLayout(int theBackground)
	{
		Layout aLayout;
		aLayout.mBack = theBackground == BackgroundType::BACKGROUND_5_ROOF ? 58.0f : theBackground >= BackgroundType::BACKGROUND_3_POOL ? 72.0f : 74.0f;
		return aLayout;
	}

	Vec3 Local(float theGameX, float theY, float theGameY)
	{
		return { theGameX - BOARD_WIDTH / 2, theY, theGameY - BOARD_HEIGHT / 2 };
	}

	// A figure's card: pixels across from its feet (l, r) and up from the lawn (b, t)
	struct Box
	{
		float l = 0, r = 0, b = 0, t = 0;
	};

	Box FigureBox(const ARScene::Figure& theFigure)
	{
		float ox = theFigure.mFeetX - theFigure.mGameX, oy = theFigure.mFeetY - theFigure.mGameY;
		return { -ox, theFigure.mAtlas.mWidth - ox, oy - theFigure.mAtlas.mHeight, oy };
	}

	// Positions between two scenes: each figure slides from where it was shown to its new place (and height) over
	// the time between the scenes, so a pea or a falling sun moves smoothly at any update rate.
	struct Track
	{
		ARScene::Figure	mFigure;
		Box				mBox;
		uint32_t		mStart = 0;
		float			mDuration = 80;
		float			mX0 = 0;
		float			mY0 = 0;
		float			mH0 = 0;
	};

	struct Shown
	{
		const Track*	mTrack;
		Vec3			mFeet;		// local, on the lawn
		Box				mBox;
		float			mDistance;
	};

	struct Vertex
	{
		float		mX, mY, mZ;
		uint32_t	mColor;			// RGBA bytes
		float		mU, mV;
	};

	constexpr uint32_t Rgba(int r, int g, int b, int a = 255)
	{
		return static_cast<uint32_t>(r) | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b) << 16 | static_cast<uint32_t>(a) << 24;
	}

	enum Mode
	{
		MODE_COLOR,			// flat color: the wood, the table
		MODE_PICTURE,		// the backdrop, opaque
		MODE_ATLAS,			// the figures and panels: premultiplied colors with alpha
		MODE_SHADOW,		// a round soft shadow
		MODE_BOARD_SHADOW,	// a square soft shadow, under the base
	};

	const char* SHADER_CODE = R"GLSL(
varying vec4 vColor;
varying vec2 vUv;
#ifdef VERTEX
uniform mat4 uMvp;
attribute vec3 aPos;
attribute vec4 aColor;
attribute vec2 aUv;
void main()
{
	vColor = aColor;
	vUv = aUv;
	gl_Position = uMvp * vec4(aPos, 1.0);
}
#else
uniform sampler2D uTex;
uniform float uMode;
void main()
{
	vec4 c = vColor;
	if (uMode > 3.5)
	{
		vec2 e = smoothstep(vec2(0.86), vec2(1.0), abs(vUv));
		c = vec4(0.0, 0.0, 0.0, vColor.a * (1.0 - e.x) * (1.0 - e.y));
	}
	else if (uMode > 2.5)
		c = vec4(0.0, 0.0, 0.0, vColor.a * (1.0 - smoothstep(0.25, 1.0, length(vUv))));
	else if (uMode > 1.5)
	{
		c = texture2D(uTex, vUv);
		if (c.a < 0.004 && max(c.r, max(c.g, c.b)) < 0.004)
			discard;
		c = vec4(min(c.rgb, vec3(1.0)), c.a);
	}
	else if (uMode > 0.5)
		c = vec4(texture2D(uTex, vUv).rgb * vColor.rgb, 1.0);
	gl_FragColor = c;
}
#endif
)GLSL";

	bool					gGLReady = false;
	bool					gGLFailed = false;
	GLuint					gProgram = 0;
	GLint					gUfMvp = -1;
	GLint					gUfTex = -1;
	GLint					gUfMode = -1;
	GLuint					gVbo = 0;
	GLuint					gIbo = 0;
	GLuint					gAtlasTex = 0;
	GLuint					gBackdropTex = 0;
	bool					gMipmaps = false;

	bool					gHasScene = false;
	ARScene::Scene			gScene;
	Layout					gLayout;
	std::unordered_map<uint32_t, Track> gTracks;
	uint32_t				gLastUpdate = 0;		// theNow of the last update
	uint32_t				gLastSceneTicks = 0;	// SDL ticks of the last update, for the slides
	const Board*			gSceneBoard = nullptr;
	const Board*			gBackdropBoard = nullptr;
	int						gBackdropType = -1;
	uint32_t				gLastBackdrop = 0;
	Vec3					gCam;					// the camera at the last draw, in the page's space
	bool					gHaveCam = false;
	std::vector<Vertex>		gVertices;
	std::vector<Shown>		gShown;

	// Measured for the log while it draws
	uint64_t				gStatUpdateTicks = 0;
	uint64_t				gStatDrawTicks = 0;
	uint32_t				gStatUpdates = 0;
	uint32_t				gStatDraws = 0;
	uint32_t				gStatSince = 0;

	bool FinishForStats()
	{
		static const bool sFinish = [] {
			const char* aEnv = SDL_getenv("PVZ_AR_DIORAMA_FINISH");
			return aEnv && *aEnv && SDL_strcmp(aEnv, "0") != 0;
		}();
		return sFinish;
	}

	GLuint CompileShader(GLenum theType)
	{
		// GLSL ES 1.00 for ES contexts (ES 3 runs it too), GLSL 1.20 for the desktop fallback, as the engine does
		const char* aVersion = gDesktopGLFallback ? "#version 120\n" :
			"#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n";
		const char* aStage = theType == GL_VERTEX_SHADER ? "#define VERTEX\n" : "#define FRAGMENT\n";
		const GLchar* aStrings[3] = { aVersion, aStage, SHADER_CODE };
		GLuint aShader = glCreateShader(theType);
		glShaderSource(aShader, 3, aStrings, nullptr);
		glCompileShader(aShader);
		GLint aOk = 0;
		glGetShaderiv(aShader, GL_COMPILE_STATUS, &aOk);
		if (!aOk)
		{
			char aLog[1024] = {};
			glGetShaderInfoLog(aShader, sizeof(aLog), nullptr, aLog);
			PvzpLogLn("ARDiorama: shader error: {}", aLog);
			glDeleteShader(aShader);
			return 0;
		}
		return aShader;
	}

	GLuint MakeTexture(int theWidth, int theHeight, bool theMipmaps)
	{
		GLuint aTexture = 0;
		glGenTextures(1, &aTexture);
		glBindTexture(GL_TEXTURE_2D, aTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, theWidth, theHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, theMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return aTexture;
	}

	bool EnsureGL()
	{
		if (gGLReady || gGLFailed)
			return gGLReady;
		GLuint aVert = CompileShader(GL_VERTEX_SHADER), aFrag = CompileShader(GL_FRAGMENT_SHADER);
		if (!aVert || !aFrag)
		{
			gGLFailed = true;
			return false;
		}
		gProgram = glCreateProgram();
		glAttachShader(gProgram, aVert);
		glAttachShader(gProgram, aFrag);
		glBindAttribLocation(gProgram, 0, "aPos");
		glBindAttribLocation(gProgram, 1, "aColor");
		glBindAttribLocation(gProgram, 2, "aUv");
		glLinkProgram(gProgram);
		glDeleteShader(aVert);
		glDeleteShader(aFrag);
		GLint aLinked = 0;
		glGetProgramiv(gProgram, GL_LINK_STATUS, &aLinked);
		if (!aLinked)
		{
			PvzpLogLn("ARDiorama: the shaders don't link");
			gGLFailed = true;
			return false;
		}
		gUfMvp = glGetUniformLocation(gProgram, "uMvp");
		gUfTex = glGetUniformLocation(gProgram, "uTex");
		gUfMode = glGetUniformLocation(gProgram, "uMode");

		GLint aOldTexture = 0, aOldArray = 0, aOldElements = 0, aOldActive = 0;
		glGetIntegerv(GL_ACTIVE_TEXTURE, &aOldActive);
		glActiveTexture(GL_TEXTURE0);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &aOldTexture);
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &aOldArray);
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &aOldElements);

		// ES 2 has no mipmaps for textures that aren't a power of two; ES 3 and desktop GL do
		const char* aVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
		gMipmaps = gDesktopGLFallback || (aVersion && SDL_strstr(aVersion, "OpenGL ES 3"));
		gAtlasTex = MakeTexture(ATLAS_WIDTH, ATLAS_HEIGHT, false);
		gBackdropTex = MakeTexture(ARScene::BACKDROP_WIDTH, ARScene::BACKDROP_HEIGHT, gMipmaps);
		const char* aExtensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
		if (aExtensions && SDL_strstr(aExtensions, "GL_EXT_texture_filter_anisotropic"))
		{
			GLfloat aMax = 1;
			glGetFloatv(0x84FF, &aMax);	// GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: the lawn is seen at a slant
			glTexParameterf(GL_TEXTURE_2D, 0x84FE, std::min(8.0f, aMax));
		}

		glGenBuffers(1, &gVbo);
		glGenBuffers(1, &gIbo);
		std::vector<GLushort> aIndices(MAX_QUADS * 6);
		for (int i = 0; i < MAX_QUADS; i++)
		{
			const GLushort aQuad[6] = { 0, 1, 2, 2, 1, 3 };
			for (int j = 0; j < 6; j++)
				aIndices[i * 6 + j] = static_cast<GLushort>(i * 4 + aQuad[j]);
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gIbo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(aIndices.size() * sizeof(GLushort)), aIndices.data(), GL_STATIC_DRAW);

		glBindTexture(GL_TEXTURE_2D, aOldTexture);
		glBindBuffer(GL_ARRAY_BUFFER, aOldArray);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, aOldElements);
		glActiveTexture(aOldActive);
		GLint aDepthBits = 0;
		glGetIntegerv(GL_DEPTH_BITS, &aDepthBits);
		glGetError();	// GL_DEPTH_BITS is gone from core profiles
		PvzpLogLn("ARDiorama: ready ({}, mipmaps {}, depth bits {})", aVersion ? aVersion : "?", gMipmaps, aDepthBits);
		gGLReady = true;
		return true;
	}

	// The GL state Draw touches, kept to put back: the game sets its vertex arrays once and never again.
	struct SavedState
	{
		GLint		mProgram, mArray, mElements, mActive, mTexture;
		GLboolean	mBlend, mDepthTest, mDepthMask, mCull, mScissor, mOffset;
		GLint		mBlendSrcRgb, mBlendDstRgb, mBlendSrcAlpha, mBlendDstAlpha, mBlendEqRgb, mBlendEqAlpha, mDepthFunc;
		GLfloat		mOffsetFactor, mOffsetUnits;
		struct Attrib
		{
			GLint	mEnabled, mSize, mType, mNormalized, mStride, mBuffer;
			void*	mPointer;
		} mAttribs[3];

		SavedState()
		{
			glGetIntegerv(GL_CURRENT_PROGRAM, &mProgram);
			glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &mArray);
			glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &mElements);
			glGetIntegerv(GL_ACTIVE_TEXTURE, &mActive);
			glActiveTexture(GL_TEXTURE0);
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &mTexture);
			mBlend = glIsEnabled(GL_BLEND);
			mDepthTest = glIsEnabled(GL_DEPTH_TEST);
			mCull = glIsEnabled(GL_CULL_FACE);
			mScissor = glIsEnabled(GL_SCISSOR_TEST);
			mOffset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
			glGetBooleanv(GL_DEPTH_WRITEMASK, &mDepthMask);
			glGetIntegerv(GL_BLEND_SRC_RGB, &mBlendSrcRgb);
			glGetIntegerv(GL_BLEND_DST_RGB, &mBlendDstRgb);
			glGetIntegerv(GL_BLEND_SRC_ALPHA, &mBlendSrcAlpha);
			glGetIntegerv(GL_BLEND_DST_ALPHA, &mBlendDstAlpha);
			glGetIntegerv(GL_BLEND_EQUATION_RGB, &mBlendEqRgb);
			glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &mBlendEqAlpha);
			glGetIntegerv(GL_DEPTH_FUNC, &mDepthFunc);
			glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &mOffsetFactor);
			glGetFloatv(GL_POLYGON_OFFSET_UNITS, &mOffsetUnits);
			for (GLuint i = 0; i < 3; i++)
			{
				Attrib& a = mAttribs[i];
				glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a.mEnabled);
				glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a.mSize);
				glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a.mType);
				glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a.mNormalized);
				glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a.mStride);
				glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a.mBuffer);
				glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a.mPointer);
			}
		}

		~SavedState()
		{
			for (GLuint i = 0; i < 3; i++)
			{
				const Attrib& a = mAttribs[i];
				glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(a.mBuffer));
				glVertexAttribPointer(i, a.mSize, static_cast<GLenum>(a.mType), a.mNormalized ? GL_TRUE : GL_FALSE, a.mStride, a.mPointer);
				if (a.mEnabled)
					glEnableVertexAttribArray(i);
				else
					glDisableVertexAttribArray(i);
			}
			glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(mArray));
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(mElements));
			glUseProgram(static_cast<GLuint>(mProgram));
			glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(mTexture));
			glActiveTexture(static_cast<GLenum>(mActive));
			auto Set = [](GLenum theCap, GLboolean theOn) { theOn ? glEnable(theCap) : glDisable(theCap); };
			Set(GL_BLEND, mBlend);
			Set(GL_DEPTH_TEST, mDepthTest);
			Set(GL_CULL_FACE, mCull);
			Set(GL_SCISSOR_TEST, mScissor);
			Set(GL_POLYGON_OFFSET_FILL, mOffset);
			glDepthMask(mDepthMask);
			glBlendFuncSeparate(static_cast<GLenum>(mBlendSrcRgb), static_cast<GLenum>(mBlendDstRgb), static_cast<GLenum>(mBlendSrcAlpha), static_cast<GLenum>(mBlendDstAlpha));
			glBlendEquationSeparate(static_cast<GLenum>(mBlendEqRgb), static_cast<GLenum>(mBlendEqAlpha));
			glDepthFunc(static_cast<GLenum>(mDepthFunc));
			glPolygonOffset(mOffsetFactor, mOffsetUnits);
		}
	};

	// Our program, buffers and vertex layout, the same as the engine's (position, color bytes, uv)
	void BindProgram(const float theMvp[16])
	{
		glUseProgram(gProgram);
		glUniformMatrix4fv(gUfMvp, 1, GL_FALSE, theMvp);
		glUniform1i(gUfTex, 0);
		glBindBuffer(GL_ARRAY_BUFFER, gVbo);
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(gVertices.size() * sizeof(Vertex)), gVertices.data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gIbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, mX)));
		glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, mColor)));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, mU)));
		for (GLuint i = 0; i < 3; i++)
			glEnableVertexAttribArray(i);
		glActiveTexture(GL_TEXTURE0);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);	// premultiplied, the target's alpha stays
	}

	int QuadCount()
	{
		return static_cast<int>(gVertices.size() / 4);
	}

	// Corners: bottom-left, bottom-right, top-left, top-right
	void AddQuad(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, uint32_t theColor, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1)
	{
		if (QuadCount() >= MAX_QUADS)
			return;
		gVertices.push_back({ p0.x, p0.y, p0.z, theColor, u0, v0 });
		gVertices.push_back({ p1.x, p1.y, p1.z, theColor, u1, v0 });
		gVertices.push_back({ p2.x, p2.y, p2.z, theColor, u0, v1 });
		gVertices.push_back({ p3.x, p3.y, p3.z, theColor, u1, v1 });
	}

	void AddQuadUv(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, const float theUv[8])
	{
		if (QuadCount() >= MAX_QUADS)
			return;
		uint32_t aWhite = Rgba(255, 255, 255);
		gVertices.push_back({ p0.x, p0.y, p0.z, aWhite, theUv[0], theUv[1] });
		gVertices.push_back({ p1.x, p1.y, p1.z, aWhite, theUv[2], theUv[3] });
		gVertices.push_back({ p2.x, p2.y, p2.z, aWhite, theUv[4], theUv[5] });
		gVertices.push_back({ p3.x, p3.y, p3.z, aWhite, theUv[6], theUv[7] });
	}

	void DrawQuads(int theFirst, int theEnd, Mode theMode, GLuint theTexture)
	{
		if (theEnd <= theFirst)
			return;
		glUniform1f(gUfMode, static_cast<float>(theMode));
		if (theTexture)
			glBindTexture(GL_TEXTURE_2D, theTexture);
		glDrawElements(GL_TRIANGLES, (theEnd - theFirst) * 6, GL_UNSIGNED_SHORT, reinterpret_cast<const void*>(static_cast<uintptr_t>(theFirst) * 6 * sizeof(GLushort)));
	}

	// An atlas rectangle's texture coordinates, half a texel in: left, top, right, bottom
	void AtlasUv(const Sexy::Rect& theRect, float& theLeft, float& theTop, float& theRight, float& theBottom)
	{
		theLeft = (theRect.mX + 0.5f) / ATLAS_WIDTH;
		theRight = (theRect.mX + theRect.mWidth - 0.5f) / ATLAS_WIDTH;
		theTop = 1 - (theRect.mY + 0.5f) / ATLAS_HEIGHT;
		theBottom = 1 - (theRect.mY + theRect.mHeight - 0.5f) / ATLAS_HEIGHT;
	}

	void AddAtlasQuad(Vec3 theOrigin, Vec3 theRight, Vec3 theUp, const Sexy::Rect& theRect)
	{
		float l, t, r, b;
		AtlasUv(theRect, l, t, r, b);
		Vec3 aAcross = theRight * static_cast<float>(theRect.mWidth), aUp = theUp * static_cast<float>(theRect.mHeight);
		AddQuad(theOrigin, theOrigin + aAcross, theOrigin + aUp, theOrigin + aAcross + aUp, Rgba(255, 255, 255), l, b, r, t);
	}

	// The backdrop picture at a game point
	void BackdropUv(float theGameX, float theGameY, float* theUv)
	{
		theUv[0] = (theGameX - BACKDROP_X) / ARScene::BACKDROP_WIDTH;
		theUv[1] = 1 - theGameY / ARScene::BACKDROP_HEIGHT;
	}

	Vec3 PanelUp(float theTilt)
	{
		return { 0, std::cos(theTilt), std::sin(theTilt) };
	}

	// The seed bank and the menu stand above the back edge, leaning back
	Vec3 PanelOrigin()
	{
		Vec3 a = Local(gLayout.mX0, 0, gLayout.mBack);
		return { gScene.mHud.mGameX - BOARD_WIDTH / 2.0f, BASE + gLayout.mBack + 6, a.z - 3 };
	}

	uint32_t TrackKey(const ARScene::Figure& theFigure)
	{
		return static_cast<uint32_t>(static_cast<unsigned char>(theFigure.mKind)) << 24 | (static_cast<uint32_t>(theFigure.mIndex) & 0xFFFFFF);
	}

	void UpdateTracks(uint32_t theNow, uint32_t theTicks)
	{
		float aDuration = gLastUpdate ? std::clamp(static_cast<float>(theNow - gLastUpdate), 40.0f, 250.0f) : 80.0f;
		std::unordered_map<uint32_t, Track> aNext;
		aNext.reserve(gScene.mFigures.size());
		for (const ARScene::Figure& aFigure : gScene.mFigures)
		{
			Track aTrack;
			aTrack.mFigure = aFigure;
			aTrack.mBox = FigureBox(aFigure);
			aTrack.mStart = theTicks;
			aTrack.mDuration = aDuration;
			aTrack.mX0 = aFigure.mFeetX;
			aTrack.mY0 = aFigure.mFeetY;
			aTrack.mH0 = aTrack.mBox.b;
			auto aOld = gTracks.find(TrackKey(aFigure));
			if (aOld != gTracks.end())
			{
				const Track& o = aOld->second;
				float a = std::clamp(static_cast<float>(theTicks - o.mStart) / o.mDuration, 0.0f, 1.0f);
				float x = o.mX0 + (o.mFigure.mFeetX - o.mX0) * a, y = o.mY0 + (o.mFigure.mFeetY - o.mY0) * a, h = o.mH0 + (o.mBox.b - o.mH0) * a;
				if (std::fabs(x - aFigure.mFeetX) + std::fabs(y - aFigure.mFeetY) < 160)
				{
					aTrack.mX0 = x;
					aTrack.mY0 = y;
					aTrack.mH0 = h;
				}
			}
			aNext[TrackKey(aFigure)] = aTrack;
		}
		gTracks.swap(aNext);
	}

	// What stands on the base now, far to near from theCam so the cards blend over each other in order. Zombies wait
	// on the street out of sight, as off the game's screen.
	void ShownNow(uint32_t theTicks, Vec3 theCam)
	{
		gShown.clear();
		for (const auto& aEntry : gTracks)
		{
			const Track& tr = aEntry.second;
			float a = std::clamp(static_cast<float>(theTicks - tr.mStart) / tr.mDuration, 0.0f, 1.0f);
			float x = tr.mX0 + (tr.mFigure.mFeetX - tr.mX0) * a, y = tr.mY0 + (tr.mFigure.mFeetY - tr.mY0) * a;
			if (tr.mFigure.mKind == 'm')
				y = std::min(y, gLayout.mFront - 8);	// the roof's lowest mower would park on the wood
			float dh = tr.mH0 + (tr.mBox.b - tr.mH0) * a - tr.mBox.b;
			if (x < gLayout.mX0 - 30 || x > gLayout.mX1 + 10)
				continue;
			Shown s;
			s.mTrack = &tr;
			s.mFeet = Local(x, BASE, y);
			s.mBox = { tr.mBox.l, tr.mBox.r, tr.mBox.b + dh, tr.mBox.t + dh };
			Vec3 d = s.mFeet + Vec3{ 0, (s.mBox.b + s.mBox.t) / 2, 0 } - theCam;
			s.mDistance = Dot(d, d);
			gShown.push_back(s);
		}
		std::sort(gShown.begin(), gShown.end(), [](const Shown& a, const Shown& b) {
			if (a.mDistance != b.mDistance)
				return a.mDistance > b.mDistance;
			return a.mTrack->mFigure.mOrder < b.mTrack->mFigure.mOrder;
		});
		if (gShown.size() > MAX_FIGURES)
			gShown.erase(gShown.begin(), gShown.end() - MAX_FIGURES);	// the nearest ones
	}

	// The across direction of a card standing at theFeet, turned toward theCam around the vertical
	Vec3 CardRight(Vec3 theFeet, Vec3 theCam)
	{
		float dx = theCam.x - theFeet.x, dz = theCam.z - theFeet.z, l = std::hypot(dx, dz);
		if (l < 1e-3f)
		{
			dx = 0;
			dz = 1;
		}
		else
		{
			dx /= l;
			dz /= l;
		}
		return { dz, 0, -dx };
	}

	// A ray (the page's space) against a card: the ray parameter, or -1
	float RayOnCard(Vec3 o, Vec3 d, const Shown& s, Vec3 theCam)
	{
		Vec3 aRight = CardRight(s.mFeet, theCam), n = { -aRight.z, 0, aRight.x };
		float aDen = Dot(d, n);
		if (std::fabs(aDen) < 1e-9f)
			return -1;
		float t = Dot(s.mFeet - o, n) / aDen;
		if (t <= 0)
			return -1;
		Vec3 p = o + d * t - s.mFeet;
		float aAcross = Dot(p, aRight), aUp = p.y;
		return aAcross >= s.mBox.l && aAcross <= s.mBox.r && aUp >= s.mBox.b && aUp <= s.mBox.t ? t : -1;
	}

	// A ray against the seed bank panel: the ray parameter and the game point, or -1
	float RayOnPanel(Vec3 o, Vec3 d, int& theGameX, int& theGameY)
	{
		if (!gScene.mHasHud)
			return -1;
		const Sexy::Rect& r = gScene.mHud.mAtlas;
		Vec3 p = PanelOrigin(), aUp = PanelUp(PANEL_TILT), aRight = { 1, 0, 0 }, n = { 0, -aUp.z, aUp.y };
		float aDen = Dot(d, n);
		if (std::fabs(aDen) < 1e-9f)
			return -1;
		float t = Dot(p - o, n) / aDen;
		if (t <= 0)
			return -1;
		Vec3 q = o + d * t - p;
		float u = Dot(q, aRight), v = Dot(q, aUp);
		if (u < 0 || u > r.mWidth || v < 0 || v > r.mHeight)
			return -1;
		theGameX = static_cast<int>(std::lround(gScene.mHud.mGameX + u));
		theGameY = static_cast<int>(std::lround(gScene.mHud.mGameY + r.mHeight - v));
		return t;
	}

	void BuildBase()
	{
		const Layout& L = gLayout;
		Vec3 a = Local(L.mX0, 0, L.mBack), b = Local(L.mX1, 0, L.mFront);
		float x0 = a.x - RIM, x1 = b.x + RIM, z0 = a.z - RIM, z1 = b.z + RIM + LIP;
		// Flat shades of the page's lit wood
		uint32_t aTop = Rgba(126, 82, 47), aFront = Rgba(98, 61, 33), aRight = Rgba(86, 54, 29), aSide = Rgba(70, 44, 24);
		AddQuad({ x0, 0, z1 }, { x1, 0, z1 }, { x0, BASE, z1 }, { x1, BASE, z1 }, aFront);
		AddQuad({ x1, 0, z1 }, { x1, 0, z0 }, { x1, BASE, z1 }, { x1, BASE, z0 }, aRight);
		AddQuad({ x0, 0, z0 }, { x0, 0, z1 }, { x0, BASE, z0 }, { x0, BASE, z1 }, aSide);
		AddQuad({ x1, 0, z0 }, { x0, 0, z0 }, { x1, BASE, z0 }, { x0, BASE, z0 }, aSide);
		// the top around the lawn, never under it (a 16-bit depth buffer can't tell them apart)
		AddQuad({ x0, BASE, z1 }, { x1, BASE, z1 }, { x0, BASE, b.z }, { x1, BASE, b.z }, aTop);
		AddQuad({ x0, BASE, a.z }, { x1, BASE, a.z }, { x0, BASE, z0 }, { x1, BASE, z0 }, aTop);
		AddQuad({ x0, BASE, b.z }, { a.x, BASE, b.z }, { x0, BASE, a.z }, { a.x, BASE, a.z }, aTop);
		AddQuad({ b.x, BASE, b.z }, { x1, BASE, b.z }, { b.x, BASE, a.z }, { x1, BASE, a.z }, aTop);
	}

	void BuildBackdrop()
	{
		const Layout& L = gLayout;
		Vec3 a = Local(L.mX0, 0, L.mBack), b = Local(L.mX1, 0, L.mFront);
		float aUv[8];
		// the lawn, lying on the base
		BackdropUv(L.mX0, L.mFront, aUv);
		BackdropUv(L.mX1, L.mFront, aUv + 2);
		BackdropUv(L.mX0, L.mBack, aUv + 4);
		BackdropUv(L.mX1, L.mBack, aUv + 6);
		AddQuadUv({ a.x, BASE, b.z }, { b.x, BASE, b.z }, { a.x, BASE, a.z }, { b.x, BASE, a.z }, aUv);
		// the fence or the sky, standing at the back
		float aTop = BASE + L.mBack;
		BackdropUv(L.mX0, L.mBack, aUv);
		BackdropUv(L.mX1, L.mBack, aUv + 2);
		BackdropUv(L.mX0, 0, aUv + 4);
		BackdropUv(L.mX1, 0, aUv + 6);
		AddQuadUv({ a.x, BASE, a.z }, { b.x, BASE, a.z }, { a.x, aTop, a.z }, { b.x, aTop, a.z }, aUv);
		// the house: what the picture has left of the lawn, folded up along its left edge
		float aHouse = BASE + L.mX0 - HOUSE;
		BackdropUv(L.mX0, L.mFront, aUv);
		BackdropUv(L.mX0, L.mBack, aUv + 2);
		BackdropUv(HOUSE, L.mFront, aUv + 4);
		BackdropUv(HOUSE, L.mBack, aUv + 6);
		AddQuadUv({ a.x, BASE, b.z }, { a.x, BASE, a.z }, { a.x, aHouse, b.z }, { a.x, aHouse, a.z }, aUv);
	}

	void BuildBoardShadow()
	{
		const Layout& L = gLayout;
		Vec3 a = Local(L.mX0, 0, L.mBack), b = Local(L.mX1, 0, L.mFront);
		float aMargin = 40;
		float x0 = a.x - RIM - aMargin, x1 = b.x + RIM + aMargin, z0 = a.z - RIM - aMargin, z1 = b.z + RIM + LIP + aMargin;
		AddQuad({ x0, 0, z1 }, { x1, 0, z1 }, { x0, 0, z0 }, { x1, 0, z0 }, Rgba(0, 0, 0, 140), -1, -1, 1, 1);
	}

	// A soft shadow under each figure, lighter the higher it is
	void BuildShadows()
	{
		for (const Shown& s : gShown)
		{
			const Box& b = s.mBox;
			char k = s.mTrack->mFigure.mKind;
			float w = b.r - b.l, h = std::max(0.0f, b.b);
			float aRadius = k == 'z' ? std::min(46.0f, w * 0.32f) : k == 'p' ? std::min(40.0f, w * 0.42f) : k == 'c' ? 22.0f : k == 'b' ? std::min(14.0f, w * 0.3f) :
				k == 'm' ? 36.0f : std::min(36.0f, w * 0.4f);
			float aAlpha = 0.42f * std::max(0.0f, 1 - h / 260);
			if (aAlpha < 0.02f)
				continue;
			float dz = aRadius * 0.62f;
			Vec3 f = s.mFeet;
			AddQuad({ f.x - aRadius, BASE, f.z + dz }, { f.x + aRadius, BASE, f.z + dz }, { f.x - aRadius, BASE, f.z - dz }, { f.x + aRadius, BASE, f.z - dz },
				Rgba(0, 0, 0, static_cast<int>(aAlpha * 255)), -1, -1, 1, 1);
		}
	}

	// The panels, every card and the message, in that order: one draw from the atlas
	void BuildAtlasQuads(Vec3 theCamRight, Vec3 theCamUp)
	{
		Vec3 aX = { 1, 0, 0 };
		if (gScene.mHasHud)
			AddAtlasQuad(PanelOrigin(), aX, PanelUp(PANEL_TILT), gScene.mHud.mAtlas);
		if (gScene.mHasMeter)
		{
			Vec3 b = Local(gLayout.mX1, 0, gLayout.mFront);
			AddAtlasQuad({ gScene.mMeter.mGameX - BOARD_WIDTH / 2.0f, BASE + 1, b.z + RIM + LIP - 6 }, aX, PanelUp(METER_TILT), gScene.mMeter.mAtlas);
		}
		for (const Shown& s : gShown)
		{
			Vec3 aRight = CardRight(s.mFeet, gCam), aUp = { 0, 1, 0 };
			float l, t, r, b;
			AtlasUv(s.mTrack->mFigure.mAtlas, l, t, r, b);
			Vec3 aBottom = s.mFeet + aUp * s.mBox.b, aTop = s.mFeet + aUp * s.mBox.t;
			AddQuad(aBottom + aRight * s.mBox.l, aBottom + aRight * s.mBox.r, aTop + aRight * s.mBox.l, aTop + aRight * s.mBox.r, Rgba(255, 255, 255), l, b, r, t);
		}
		if (gScene.mHasMessage)
		{
			const Sexy::Rect& r = gScene.mMessage.mAtlas;
			Vec3 c = { gScene.mMessage.mGameX + r.mWidth / 2.0f - BOARD_WIDTH / 2.0f, BASE + 190, 0 };
			AddAtlasQuad(c - theCamRight * (r.mWidth / 2.0f) - theCamUp * (r.mHeight / 2.0f), theCamRight, theCamUp, r);
		}
	}

	void LogStats(uint32_t theTicks)
	{
		if (gStatSince == 0)
		{
			gStatSince = theTicks;
			return;
		}
		if (theTicks - gStatSince < 10000)
			return;
		double aFrequency = static_cast<double>(SDL_GetPerformanceFrequency()) / 1000.0;
		double aSeconds = (theTicks - gStatSince) / 1000.0;
		PvzpLogLn("ARDiorama: {:.1f} updates/s at {:.2f} ms, {:.1f} draws/s at {:.2f} ms, {} figures{}", gStatUpdates / aSeconds,
			gStatUpdates ? gStatUpdateTicks / aFrequency / gStatUpdates : 0.0, gStatDraws / aSeconds, gStatDraws ? gStatDrawTicks / aFrequency / gStatDraws : 0.0,
			gScene.mFigures.size(), FinishForStats() ? " (with glFinish)" : "");
		gStatUpdateTicks = gStatDrawTicks = 0;
		gStatUpdates = gStatDraws = 0;
		gStatSince = theTicks;
	}
}

void ARDiorama::Update(LawnApp* theApp, uint32_t theNow, uint32_t theIntervalMs)
{
	Board* aBoard = theApp ? ARScene::SceneBoard() : nullptr;
	if (!aBoard)
	{
		if (gHasScene)
		{
			gHasScene = false;
			gHaveCam = false;
			gTracks.clear();
			gLastUpdate = 0;
		}
		return;
	}
	if (gHasScene && aBoard == gSceneBoard && theNow - gLastUpdate < theIntervalMs)
		return;
	if (!EnsureGL())
		return;
	uint64_t aStart = SDL_GetPerformanceCounter();
	GLInterface* aGL = theApp->mGLInterface.get();
	int aType = static_cast<int>(aBoard->mBackground);
	bool aNewBoard = aBoard != gBackdropBoard || aType != gBackdropType;
	if (aNewBoard || theNow - gLastBackdrop >= BACKDROP_INTERVAL_MS)
	{
		if (ARScene::PaintBackdrop(aGL, aBoard, nullptr, gBackdropTex))
		{
			if (gMipmaps)
			{
				GLint aOld = 0;
				glActiveTexture(GL_TEXTURE0);
				glGetIntegerv(GL_TEXTURE_BINDING_2D, &aOld);
				glBindTexture(GL_TEXTURE_2D, gBackdropTex);
				glGenerateMipmap(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(aOld));
			}
			gBackdropBoard = aBoard;
			gBackdropType = aType;
			gLastBackdrop = theNow;
		}
	}
	if (gBackdropBoard != aBoard)
		return;
	if (!ARScene::PaintFigures(aGL, aBoard, gScene, gAtlasTex))
		return;
	aGL->EndOffscreen(nullptr, 0, 0);
	if (gScene.mSkipped > 0)
		PvzpLogLn("ARDiorama: the atlas is full, {} figures left out", gScene.mSkipped);
	if (aBoard != gSceneBoard)
	{
		gTracks.clear();
		gLastUpdate = 0;
		gSceneBoard = aBoard;
	}
	uint32_t aTicks = SDL_GetTicks();
	UpdateTracks(theNow, aTicks);
	gLayout = LawnLayout(gScene.mBackground);
	gLastUpdate = theNow;
	gLastSceneTicks = aTicks;
	gHasScene = true;
	if (FinishForStats())
		glFinish();
	gStatUpdateTicks += SDL_GetPerformanceCounter() - aStart;
	gStatUpdates++;
}

bool ARDiorama::HasScene()
{
	return gHasScene && ARScene::SceneBoard() != nullptr;
}

float ARDiorama::BoardWidth()
{
	return BASE_WIDTH * PIXEL;
}

float ARDiorama::BaseHeight()
{
	return BASE * PIXEL;
}

void ARDiorama::Draw(const float theModel[16], const float theView[16], const float theProj[16])
{
	if (!HasScene() || !gGLReady)
		return;
	uint64_t aStart = SDL_GetPerformanceCounter();
	float aToWorld[16], aModelView[16], aMvp[16], aInverse[16];
	Mul(theModel, PAGE_TO_BOARD, aToWorld);
	Mul(theView, aToWorld, aModelView);
	Mul(theProj, aModelView, aMvp);
	if (!Invert(aModelView, aInverse))
		return;
	gCam = TransformPoint(aInverse, { 0, 0, 0 });
	gHaveCam = true;
	Vec3 aCamRight = Normalized(TransformDir(aInverse, { 1, 0, 0 })), aCamUp = Normalized(TransformDir(aInverse, { 0, 1, 0 }));
	uint32_t aTicks = SDL_GetTicks();
	ShownNow(aTicks, gCam);

	gVertices.clear();
	BuildBase();
	int aBackdropStart = QuadCount();
	BuildBackdrop();
	int aBoardShadowStart = QuadCount();
	BuildBoardShadow();
	int aShadowStart = QuadCount();
	BuildShadows();
	int aAtlasStart = QuadCount();
	BuildAtlasQuads(aCamRight, aCamUp);
	int aEnd = QuadCount();

	{
		SavedState aSaved;
		BindProgram(aMvp);
		// The wood and the pictures, opaque
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		glDisable(GL_POLYGON_OFFSET_FILL);
		DrawQuads(0, aBackdropStart, MODE_COLOR, 0);
		DrawQuads(aBackdropStart, aBoardShadowStart, MODE_PICTURE, gBackdropTex);
		// The shadows lie on the table and the lawn, pulled toward the eye instead of lifted
		glEnable(GL_BLEND);
		glDepthMask(GL_FALSE);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -4.0f);
		DrawQuads(aBoardShadowStart, aShadowStart, MODE_BOARD_SHADOW, 0);
		DrawQuads(aShadowStart, aAtlasStart, MODE_SHADOW, 0);
		glDisable(GL_POLYGON_OFFSET_FILL);
		// The panels, the cards far to near and the message
		DrawQuads(aAtlasStart, aEnd, MODE_ATLAS, gAtlasTex);
	}
	if (FinishForStats())
		glFinish();
	gStatDrawTicks += SDL_GetPerformanceCounter() - aStart;
	gStatDraws++;
	LogStats(aTicks);
}

bool ARDiorama::Pick(const float theModel[16], const float theOrigin[3], const float theDir[3], int* theGameX, int* theGameY)
{
	if (!HasScene())
		return false;
	float aToWorld[16], aInverse[16];
	Mul(theModel, PAGE_TO_BOARD, aToWorld);
	if (!Invert(aToWorld, aInverse))
		return false;
	Vec3 o = TransformPoint(aInverse, { theOrigin[0], theOrigin[1], theOrigin[2] });
	Vec3 d = TransformDir(aInverse, { theDir[0], theDir[1], theDir[2] });
	Vec3 aCam = gHaveCam ? gCam : o;
	ShownNow(SDL_GetTicks(), aCam);

	// A sun or coin first, even behind a plant: in the game they are drawn over everything
	float aBest = -1;
	int aX = 0, aY = 0;
	for (const Shown& s : gShown)
	{
		const ARScene::Figure& f = s.mTrack->mFigure;
		if (f.mKind != 'c' || f.mClickX < 0)
			continue;
		float t = RayOnCard(o, d, s, aCam);
		if (t > 0 && (aBest < 0 || t < aBest))
		{
			aBest = t;
			aX = f.mClickX;
			aY = f.mClickY;
		}
	}
	if (aBest < 0)
	{
		// Otherwise the nearest of the seed bank, a card that takes a tap (a plant for the shovel, a grave) and the lawn
		int aPanelX, aPanelY;
		float t = RayOnPanel(o, d, aPanelX, aPanelY);
		if (t > 0)
		{
			aBest = t;
			aX = aPanelX;
			aY = aPanelY;
		}
		for (const Shown& s : gShown)
		{
			const ARScene::Figure& f = s.mTrack->mFigure;
			if (f.mClickX < 0)
				continue;
			t = RayOnCard(o, d, s, aCam);
			if (t > 0 && (aBest < 0 || t < aBest))
			{
				aBest = t;
				aX = f.mClickX;
				aY = f.mClickY;
			}
		}
		if (d.y < -1e-9f)
		{
			t = (BASE - o.y) / d.y;
			Vec3 p = o + d * t;
			float gx = p.x + BOARD_WIDTH / 2, gy = p.z + BOARD_HEIGHT / 2;
			if (t > 0 && gx >= gLayout.mX0 && gx <= gLayout.mX1 && gy >= gLayout.mBack && gy <= gLayout.mFront && (aBest < 0 || t < aBest))
			{
				aBest = t;
				aX = static_cast<int>(std::lround(std::clamp(gx, 0.0f, BOARD_WIDTH - 1.0f)));
				aY = static_cast<int>(std::lround(std::clamp(gy, 0.0f, BOARD_HEIGHT - 1.0f)));
			}
		}
	}
	if (aBest < 0)
		return false;
	*theGameX = aX;
	*theGameY = aY;
	return true;
}

void ARDiorama::DrawTable(const float theView[16], const float theProj[16], float theRadius)
{
	if (!gGLReady)
		return;
	float aMvp[16];
	Mul(theProj, theView, aMvp);
	gVertices.clear();
	constexpr int SEGMENTS = 64;
	for (int i = 0; i < SEGMENTS; i++)
	{
		float a0 = i * 6.2831853f / SEGMENTS, a1 = (i + 1) * 6.2831853f / SEGMENTS;
		Vec3 c = { 0, 0, 0 }, p0 = { theRadius * std::cos(a0), 0, theRadius * std::sin(a0) }, p1 = { theRadius * std::cos(a1), 0, theRadius * std::sin(a1) };
		AddQuad(c, c, p0, p1, Rgba(43, 36, 31));
	}
	SavedState aSaved;
	BindProgram(aMvp);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	DrawQuads(0, QuadCount(), MODE_COLOR, 0);
}

bool ARDiorama::LawnPoint(int theGameX, int theGameY, float thePoint[3])
{
	if (!HasScene())
		return false;
	Vec3 p = TransformPoint(PAGE_TO_BOARD, Local(static_cast<float>(theGameX), BASE, static_cast<float>(theGameY)));
	thePoint[0] = p.x;
	thePoint[1] = p.y;
	thePoint[2] = p.z;
	return true;
}

bool ARDiorama::PanelPoint(int theGameX, int theGameY, float thePoint[3])
{
	if (!HasScene() || !gScene.mHasHud)
		return false;
	const Sexy::Rect& r = gScene.mHud.mAtlas;
	float u = static_cast<float>(theGameX - gScene.mHud.mGameX), v = static_cast<float>(gScene.mHud.mGameY + r.mHeight - theGameY);
	if (u < 0 || u > r.mWidth || v < 0 || v > r.mHeight)
		return false;
	Vec3 p = TransformPoint(PAGE_TO_BOARD, PanelOrigin() + Vec3{ u, 0, 0 } + PanelUp(PANEL_TILT) * v);
	thePoint[0] = p.x;
	thePoint[1] = p.y;
	thePoint[2] = p.z;
	return true;
}

bool ARDiorama::CoinPoint(float thePoint[3], int* theClickX, int* theClickY)
{
	if (!HasScene() || !gHaveCam)
		return false;
	ShownNow(SDL_GetTicks(), gCam);
	for (const Shown& s : gShown)
	{
		if (s.mTrack->mFigure.mKind != 'c' || s.mTrack->mFigure.mClickX < 0)
			continue;
		Vec3 p = s.mFeet + CardRight(s.mFeet, gCam) * ((s.mBox.l + s.mBox.r) / 2) + Vec3{ 0, (s.mBox.b + s.mBox.t) / 2, 0 };
		p = TransformPoint(PAGE_TO_BOARD, p);
		thePoint[0] = p.x;
		thePoint[1] = p.y;
		thePoint[2] = p.z;
		if (theClickX)
			*theClickX = s.mTrack->mFigure.mClickX;
		if (theClickY)
			*theClickY = s.mTrack->mFigure.mClickY;
		return true;
	}
	return false;
}
