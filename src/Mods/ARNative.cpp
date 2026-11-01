#include "ARNative.h"
#include "ARDiorama.h"
#include "ARView.h"
#include "Mods.h"
#include "LawnApp.h"
#include "PvzpLib/PvzpDebug.h"
#include "SexyAppFramework/graphics/GLInterface.h"
#include "SexyAppFramework/graphics/GLPlatform.h"
#include "imgui.h"
#include <SDL.h>
#include <SDL_system.h>
#include <arcore_c_api.h>
#include <jni.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

// The few EGL calls needed, declared here: EGL/egl.h clashes with glad's Khronos platform header
extern "C"
{
	void*			eglGetCurrentDisplay();
	void*			eglGetCurrentContext();
	void*			eglGetCurrentSurface(int32_t theReadDraw);
	unsigned int	eglMakeCurrent(void* theDisplay, void* theDraw, void* theRead, void* theContext);
}

namespace
{
	constexpr float		PI = 3.14159265f;
	constexpr uint32_t	SCENE_INTERVAL_MS = 55;		// the diorama's figures, about 18 times a second
	constexpr uint32_t	ROTATION_CHECK_MS = 400;
	constexpr uint32_t	TAP_DELAY_MS = 90;			// a finger waits this long in case more follow (the menu's 3 fingers)
	constexpr uint32_t	NOTICE_MS = 8000;
	constexpr uint32_t	IDLE_PAUSE_MS = 90000;		// this long outside a lawn and the camera stops until the next one
	constexpr float		DEFAULT_WIDTH = 0.40f;		// the board's width on the table, meters
	constexpr float		MIN_WIDTH = 0.15f;
	constexpr float		MAX_WIDTH = 1.60f;
	constexpr float		LAWN_LIFT = 0.016f;			// the lawn stands this high on a 40 cm board (the diorama's wooden base)
	constexpr float		TAP_SLOP = 0.02f;			// of the screen's height: a finger that moves less is a tap
	constexpr int		CAMERA_PERMISSION_CODE = 0x5241;

	enum class State
	{
		OFF,
		CHECKING,		// ArCoreApk_checkAvailability
		INSTALLING,		// Google Play Services for AR is being installed
		ASKING,			// the camera permission dialog
		RUNNING,
		STOPPED			// unsupported, denied or failed: gStatus says why, the game stays 2D
	};

	enum class Mode
	{
		PLACE,			// looking for a surface, a tap puts the board
		ADJUST,			// drag, turn and size it
		PLAY			// taps are clicks in the game
	};

	State				gState = State::OFF;
	bool				gForcedOff = false;			// "Salir del AR" while PVZ_AR_NATIVE forces it on
	std::string			gStatus;
	uint32_t			gStateSince = 0;
	bool				gInstallAsked = false;
	bool				gPermissionAsked = false;
	bool				gPermissionLostFocus = false;
	std::string			gNotice;
	uint32_t			gNoticeUntil = 0;

	ArSession*			gSession = nullptr;
	ArFrame*			gFrame = nullptr;
	GLuint				gCameraTexture = 0;
	bool				gInBackground = false;
	bool				gResumeWanted = false;
	bool				gWatching = false;
	int					gRotation = -1;
	int					gViewW = 0;
	int					gViewH = 0;
	uint32_t			gLastRotationCheck = 0;
	float				gCameraUv[8] = {};
	bool				gCameraUvValid = false;
	bool				gCameraLive = false;		// a camera image has arrived
	uint32_t			gCameraSince = 0;
	ArStatus			gLastUpdateError = AR_SUCCESS;

	float				gView[16];
	float				gProj[16];
	float				gViewProj[16];
	float				gEye[3] = {};
	bool				gTracking = false;
	ArTrackingFailureReason	gFailure = AR_TRACKING_FAILURE_REASON_NONE;
	bool				gShowing = false;			// the AR view covers the screen this frame
	uint32_t			gHiddenSince = 0;			// when the last lawn ended
	bool				gIdlePaused = false;

	Mode				gMode = Mode::PLACE;
	bool				gPlaced = false;
	ArAnchor*			gAnchor = nullptr;
	float				gBoardPos[3] = {};
	float				gYaw = 0.0f;
	float				gWidth = DEFAULT_WIDTH;
	bool				gFloating = false;			// "Sin mesa" without tracking: it stays in front of the camera
	float				gFloatAxes[9] = {};			// floating without tracking: the board's axes, from the camera's
	bool				gReticleOk = false;
	float				gReticle[3] = {};
	bool				gHasPlane = false;
	bool				gTestPlaced = false;

	// Touches on the AR view. Up to two fingers: one drags or plays, two turn and size the board.
	struct Finger
	{
		SDL_FingerID	mId = 0;
		float			mX = 0;		// pixels
		float			mY = 0;
		float			mStartX = 0;
		float			mStartY = 0;
		bool			mUsed = false;
	};
	std::vector<Finger>	gFingers;
	bool				gGestureSpoiled = false;	// more fingers than the gesture wants, until they all lift
	uint32_t			gPressAt = 0;				// play: when the first finger went down
	bool				gPressPending = false;		// play: the click waits TAP_DELAY_MS
	bool				gPressSent = false;			// play: the game has the mouse down
	int					gPressX = 0;
	int					gPressY = 0;
	uint32_t			gPressMoveAt = 0;
	float				gDragOffset[2] = {};
	bool				gDragging = false;
	float				gPinchDistance = 0;
	float				gPinchAngle = 0;

	// GL
	GLuint				gCameraProgram = 0;
	GLint				gCameraDim = -1;
	GLuint				gColorProgram = 0;
	GLint				gColorViewProj = -1;
	GLint				gColorPointSize = -1;
	GLint				gColorRound = -1;
	GLuint				gDotsProgram = 0;
	GLint				gDotsViewProj = -1;
	bool				gGLReady = false;
	GLint				gDepthBits = 0;

	struct ColorVertex
	{
		float			mX, mY, mZ;
		float			mR, mG, mB, mA;	// premultiplied
	};

	// ---- small matrix helpers, column-major like GL and ARCore

	void MatMul(const float a[16], const float b[16], float out[16])
	{
		float r[16];
		for (int c = 0; c < 4; c++)
			for (int rr = 0; rr < 4; rr++)
				r[c * 4 + rr] = a[0 * 4 + rr] * b[c * 4 + 0] + a[1 * 4 + rr] * b[c * 4 + 1] + a[2 * 4 + rr] * b[c * 4 + 2] + a[3 * 4 + rr] * b[c * 4 + 3];
		std::memcpy(out, r, sizeof(r));
	}

	bool MatInvert(const float m[16], float out[16])
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
		if (std::fabs(aDet) < 1e-12f)
			return false;
		for (int i = 0; i < 16; i++)
			out[i] = inv[i] / aDet;
		return true;
	}

	// Board to world: on its spot, turned around the vertical and scaled from the diorama's own width
	void BoardModel(float out[16])
	{
		float k = gWidth / std::max(0.01f, ARDiorama::BoardWidth());
		float c = std::cos(gYaw), s = std::sin(gYaw);
		float aModel[16] = {
			c * k, 0, -s * k, 0,
			0, k, 0, 0,
			s * k, 0, c * k, 0,
			gBoardPos[0], gBoardPos[1] + LAWN_LIFT * gWidth / DEFAULT_WIDTH, gBoardPos[2], 1,
		};
		if (gFloating && !gTracking)
			for (int i = 0; i < 9; i++)
				aModel[(i / 3) * 4 + i % 3] = gFloatAxes[i] * k;
		std::memcpy(out, aModel, sizeof(aModel));
	}

	// A world ray through a point of the screen (pixels, top-left origin)
	bool ScreenRay(float theX, float theY, float theOrigin[3], float theDir[3])
	{
		float aInv[16];
		if (gViewW <= 0 || gViewH <= 0 || !MatInvert(gViewProj, aInv))
			return false;
		float aNdcX = theX / gViewW * 2.0f - 1.0f, aNdcY = 1.0f - theY / gViewH * 2.0f;
		float aPoints[2][3];
		for (int i = 0; i < 2; i++)
		{
			float z = i == 0 ? -1.0f : 1.0f;
			float v[4];
			for (int r = 0; r < 4; r++)
				v[r] = aInv[0 * 4 + r] * aNdcX + aInv[1 * 4 + r] * aNdcY + aInv[2 * 4 + r] * z + aInv[3 * 4 + r];
			if (std::fabs(v[3]) < 1e-9f)
				return false;
			for (int r = 0; r < 3; r++)
				aPoints[i][r] = v[r] / v[3];
		}
		float d[3] = { aPoints[1][0] - aPoints[0][0], aPoints[1][1] - aPoints[0][1], aPoints[1][2] - aPoints[0][2] };
		float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (l < 1e-9f)
			return false;
		for (int r = 0; r < 3; r++)
		{
			theOrigin[r] = aPoints[0][r];
			theDir[r] = d[r] / l;
		}
		return true;
	}

	// The ray against the horizontal plane at height theY
	bool RayOnPlaneY(const float theOrigin[3], const float theDir[3], float theY, float theHit[3])
	{
		if (std::fabs(theDir[1]) < 1e-4f)
			return false;
		float t = (theY - theOrigin[1]) / theDir[1];
		if (t <= 0 || t > 20)
			return false;
		theHit[0] = theOrigin[0] + theDir[0] * t;
		theHit[1] = theY;
		theHit[2] = theOrigin[2] + theDir[2] * t;
		return true;
	}

	// ---- JNI

	struct Jni
	{
		JNIEnv*	mEnv;
		jobject	mActivity;

		Jni() : mEnv(static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv())), mActivity(static_cast<jobject>(SDL_AndroidGetActivity())) {}
		~Jni()
		{
			if (mEnv && mActivity)
				mEnv->DeleteLocalRef(mActivity);
		}
		bool Ok() const { return mEnv && mActivity; }
		bool Failed()
		{
			if (!mEnv->ExceptionCheck())
				return false;
			mEnv->ExceptionDescribe();
			mEnv->ExceptionClear();
			return true;
		}
	};

	// Surface.ROTATION_0..270 of the screen the activity is on, -1 if it can't be read
	int DisplayRotation()
	{
		Jni j;
		if (!j.Ok())
			return -1;
		JNIEnv* e = j.mEnv;
		int aRotation = -1;
		jclass aActivityClass = e->GetObjectClass(j.mActivity);
		jmethodID aGetManager = e->GetMethodID(aActivityClass, "getWindowManager", "()Landroid/view/WindowManager;");
		jobject aManager = aGetManager ? e->CallObjectMethod(j.mActivity, aGetManager) : nullptr;
		if (!j.Failed() && aManager)
		{
			jclass aManagerClass = e->FindClass("android/view/WindowManager");
			jmethodID aGetDisplay = e->GetMethodID(aManagerClass, "getDefaultDisplay", "()Landroid/view/Display;");
			jobject aDisplay = aGetDisplay ? e->CallObjectMethod(aManager, aGetDisplay) : nullptr;
			if (!j.Failed() && aDisplay)
			{
				jclass aDisplayClass = e->FindClass("android/view/Display");
				jmethodID aGetRotation = e->GetMethodID(aDisplayClass, "getRotation", "()I");
				if (aGetRotation)
				{
					aRotation = e->CallIntMethod(aDisplay, aGetRotation);
					if (j.Failed())
						aRotation = -1;
				}
				e->DeleteLocalRef(aDisplayClass);
				e->DeleteLocalRef(aDisplay);
			}
			e->DeleteLocalRef(aManagerClass);
			e->DeleteLocalRef(aManager);
		}
		j.Failed();
		e->DeleteLocalRef(aActivityClass);
		return aRotation;
	}

	bool HasCameraPermission()
	{
		Jni j;
		if (!j.Ok())
			return false;
		JNIEnv* e = j.mEnv;
		jclass aClass = e->GetObjectClass(j.mActivity);
		jmethodID aCheck = e->GetMethodID(aClass, "checkSelfPermission", "(Ljava/lang/String;)I");
		jstring aName = e->NewStringUTF("android.permission.CAMERA");
		bool aGranted = aCheck && e->CallIntMethod(j.mActivity, aCheck, aName) == 0;
		if (j.Failed())
			aGranted = false;
		e->DeleteLocalRef(aName);
		e->DeleteLocalRef(aClass);
		return aGranted;
	}

	// Shows the system dialog without blocking the game (SDLActivity.requestPermission; SDL_AndroidRequestPermission waits)
	void RequestCameraPermission()
	{
		Jni j;
		if (!j.Ok())
			return;
		JNIEnv* e = j.mEnv;
		jclass aClass = e->GetObjectClass(j.mActivity);
		jmethodID aRequest = e->GetStaticMethodID(aClass, "requestPermission", "(Ljava/lang/String;I)V");
		jstring aName = e->NewStringUTF("android.permission.CAMERA");
		if (aRequest)
			e->CallStaticVoidMethod(aClass, aRequest, aName, CAMERA_PERMISSION_CODE);
		j.Failed();
		e->DeleteLocalRef(aName);
		e->DeleteLocalRef(aClass);
	}

	// ---- GL

	GLuint Shader(GLenum theType, const char* theSource)
	{
		GLuint aShader = glCreateShader(theType);
		glShaderSource(aShader, 1, &theSource, nullptr);
		glCompileShader(aShader);
		GLint aOk = 0;
		glGetShaderiv(aShader, GL_COMPILE_STATUS, &aOk);
		if (!aOk)
		{
			char aLog[1024] = {};
			glGetShaderInfoLog(aShader, sizeof(aLog) - 1, nullptr, aLog);
			PvzpLogLn("ARNative: shader failed: {}", aLog);
			glDeleteShader(aShader);
			return 0;
		}
		return aShader;
	}

	GLuint Program(const char* theVertex, const char* theFragment, std::initializer_list<const char*> theAttributes)
	{
		GLuint aVertex = Shader(GL_VERTEX_SHADER, theVertex), aFragment = Shader(GL_FRAGMENT_SHADER, theFragment);
		if (!aVertex || !aFragment)
			return 0;
		GLuint aProgram = glCreateProgram();
		glAttachShader(aProgram, aVertex);
		glAttachShader(aProgram, aFragment);
		GLuint aIndex = 0;
		for (const char* aName : theAttributes)
			glBindAttribLocation(aProgram, aIndex++, aName);
		glLinkProgram(aProgram);
		glDeleteShader(aVertex);
		glDeleteShader(aFragment);
		GLint aOk = 0;
		glGetProgramiv(aProgram, GL_LINK_STATUS, &aOk);
		if (!aOk)
		{
			char aLog[1024] = {};
			glGetProgramInfoLog(aProgram, sizeof(aLog) - 1, nullptr, aLog);
			PvzpLogLn("ARNative: program failed: {}", aLog);
			glDeleteProgram(aProgram);
			return 0;
		}
		return aProgram;
	}

	const char* CAMERA_VS = R"(
attribute vec2 aPos;
attribute vec2 aUv;
varying vec2 vUv;
void main(){
	vUv=aUv;
	gl_Position=vec4(aPos,0.0,1.0);
})";
	const char* CAMERA_FS = R"(#extension GL_OES_EGL_image_external : require
precision mediump float;
uniform samplerExternalOES uTex;
uniform float uDim;
varying vec2 vUv;
void main(){
	gl_FragColor=vec4(texture2D(uTex,vUv).rgb*uDim,1.0);
})";
	const char* COLOR_VS = R"(
attribute vec3 aPos;
attribute vec4 aColor;
uniform mat4 uViewProj;
uniform float uPointSize;
varying vec4 vColor;
void main(){
	vColor=aColor;
	gl_Position=uViewProj*vec4(aPos,1.0);
	gl_PointSize=uPointSize;
})";
	const char* COLOR_FS = R"(
precision mediump float;
uniform float uRound;
varying vec4 vColor;
void main(){
	if(uRound>0.5&&length(gl_PointCoord-vec2(0.5))>0.5)discard;
	gl_FragColor=vColor;
})";
	// Detected surfaces: a faint fill with a dot every 5 cm, in world coordinates
	const char* DOTS_VS = R"(
attribute vec3 aPos;
attribute float aEdge;
uniform mat4 uViewProj;
varying vec2 vW;
varying float vEdge;
void main(){
	vW=aPos.xz;
	vEdge=aEdge;
	gl_Position=uViewProj*vec4(aPos,1.0);
})";
	const char* DOTS_FS = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 vW;
varying float vEdge;
void main(){
	vec2 g=fract(vW*20.0)-0.5;
	float dot=1.0-smoothstep(0.11,0.16,length(g));
	float a=(0.10+0.70*dot)*smoothstep(0.0,0.6,vEdge);
	gl_FragColor=vec4(a,a,a,a);
})";

	bool InitGL()
	{
		if (gGLReady)
			return true;
		gCameraProgram = Program(CAMERA_VS, CAMERA_FS, { "aPos", "aUv" });
		gColorProgram = Program(COLOR_VS, COLOR_FS, { "aPos", "aColor" });
		gDotsProgram = Program(DOTS_VS, DOTS_FS, { "aPos", "aEdge" });
		if (!gCameraProgram || !gColorProgram || !gDotsProgram)
			return false;
		GLint aProgram = 0;	// the game draws with whatever program is current
		glGetIntegerv(GL_CURRENT_PROGRAM, &aProgram);
		glUseProgram(gCameraProgram);
		glUniform1i(glGetUniformLocation(gCameraProgram, "uTex"), 0);
		glUseProgram(static_cast<GLuint>(aProgram));
		gCameraDim = glGetUniformLocation(gCameraProgram, "uDim");
		gColorViewProj = glGetUniformLocation(gColorProgram, "uViewProj");
		gColorPointSize = glGetUniformLocation(gColorProgram, "uPointSize");
		gColorRound = glGetUniformLocation(gColorProgram, "uRound");
		gDotsViewProj = glGetUniformLocation(gDotsProgram, "uViewProj");
		glGetIntegerv(GL_DEPTH_BITS, &gDepthBits);
		PvzpLogLn("ARNative: GL {} ({}), depth bits {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
			reinterpret_cast<const char*>(glGetString(GL_RENDERER)), gDepthBits);
		gGLReady = true;
		return true;
	}

	// The game's attribute pointers live in the default vertex array object (it is GLES 2 code); ARCore's GLES 3
	// work must not leave another one bound.
	using BindVertexArrayProc = void (*)(GLuint);
	BindVertexArrayProc BindVertexArray()
	{
		static BindVertexArrayProc sProc = [] {
			auto aProc = reinterpret_cast<BindVertexArrayProc>(SDL_GL_GetProcAddress("glBindVertexArray"));
			if (!aProc)
				aProc = reinterpret_cast<BindVertexArrayProc>(SDL_GL_GetProcAddress("glBindVertexArrayOES"));
			return aProc;
		}();
		return sProc;
	}

	// ARCore's update runs GLES 3 work of its own in the game's context (the camera image for its depth model). GLES 3
	// state the game never sets goes back to its defaults in case it is left behind: a pixel unpack buffer still
	// bound, for one, would make every texture the game uploads come out empty.
	void ResetAfterARCore()
	{
		glBindBuffer(0x88EC, 0);	// GL_PIXEL_UNPACK_BUFFER
		glBindBuffer(0x88EB, 0);	// GL_PIXEL_PACK_BUFFER
		for (GLenum aName : { 0x0CF2, 0x0CF3, 0x0CF4, 0x0D02, 0x0D03, 0x0D04 })	// row lengths and skips
			glPixelStorei(aName, 0);
		using BindSamplerProc = void (*)(GLuint, GLuint);
		static BindSamplerProc sBindSampler = reinterpret_cast<BindSamplerProc>(SDL_GL_GetProcAddress("glBindSampler"));
		if (sBindSampler)
			sBindSampler(0, 0);
		glDisable(GL_STENCIL_TEST);
		glDisable(0x8C89);	// GL_RASTERIZER_DISCARD
		glDisable(GL_POLYGON_OFFSET_FILL);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glGetError();	// a GLES 2 context refuses the GLES 3 names
	}

	// The game sets its program, buffer and attribute pointers once and relies on them, like ImGui's backend does
	// for its own: everything touched here is put back.
	struct GLBackup
	{
		GLint		mVertexArray = 0;
		GLint		mProgram = 0, mArrayBuffer = 0, mElementBuffer = 0, mActiveTexture = 0, mTexture = 0, mFramebuffer = 0;
		GLint		mViewport[4] = {}, mScissor[4] = {};
		GLint		mBlendSrcRgb = 0, mBlendDstRgb = 0, mBlendSrcAlpha = 0, mBlendDstAlpha = 0;
		GLboolean	mBlend = 0, mDepthTest = 0, mCull = 0, mScissorTest = 0, mDepthMask = 0;
		struct Attrib
		{
			GLint	mEnabled = 0, mSize = 0, mType = 0, mNormalized = 0, mStride = 0, mBuffer = 0;
			void*	mPointer = nullptr;
		} mAttribs[3];

		GLBackup()
		{
			if (BindVertexArray())
				glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &mVertexArray);
			glGetIntegerv(GL_CURRENT_PROGRAM, &mProgram);
			glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &mArrayBuffer);
			glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &mElementBuffer);
			glGetIntegerv(GL_ACTIVE_TEXTURE, &mActiveTexture);
			glActiveTexture(GL_TEXTURE0);
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &mTexture);
			glGetIntegerv(GL_FRAMEBUFFER_BINDING, &mFramebuffer);
			glGetIntegerv(GL_VIEWPORT, mViewport);
			glGetIntegerv(GL_SCISSOR_BOX, mScissor);
			glGetIntegerv(GL_BLEND_SRC_RGB, &mBlendSrcRgb);
			glGetIntegerv(GL_BLEND_DST_RGB, &mBlendDstRgb);
			glGetIntegerv(GL_BLEND_SRC_ALPHA, &mBlendSrcAlpha);
			glGetIntegerv(GL_BLEND_DST_ALPHA, &mBlendDstAlpha);
			mBlend = glIsEnabled(GL_BLEND);
			mDepthTest = glIsEnabled(GL_DEPTH_TEST);
			mCull = glIsEnabled(GL_CULL_FACE);
			mScissorTest = glIsEnabled(GL_SCISSOR_TEST);
			glGetBooleanv(GL_DEPTH_WRITEMASK, &mDepthMask);
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

		void RestoreVertexArray() const
		{
			if (BindVertexArray())
				BindVertexArray()(static_cast<GLuint>(mVertexArray));
		}

		~GLBackup()
		{
			RestoreVertexArray();
			for (GLuint i = 0; i < 3; i++)
			{
				const Attrib& a = mAttribs[i];
				glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(a.mBuffer));
				if (a.mSize > 0)
					glVertexAttribPointer(i, a.mSize, static_cast<GLenum>(a.mType), static_cast<GLboolean>(a.mNormalized), a.mStride, a.mPointer);
				if (a.mEnabled)
					glEnableVertexAttribArray(i);
				else
					glDisableVertexAttribArray(i);
			}
			glUseProgram(static_cast<GLuint>(mProgram));
			glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(mArrayBuffer));
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(mElementBuffer));
			glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(mFramebuffer));
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
			glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(mTexture));
			glActiveTexture(static_cast<GLenum>(mActiveTexture));
			glViewport(mViewport[0], mViewport[1], mViewport[2], mViewport[3]);
			glScissor(mScissor[0], mScissor[1], mScissor[2], mScissor[3]);
			glBlendFuncSeparate(static_cast<GLenum>(mBlendSrcRgb), static_cast<GLenum>(mBlendDstRgb), static_cast<GLenum>(mBlendSrcAlpha), static_cast<GLenum>(mBlendDstAlpha));
			mBlend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
			mDepthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
			mCull ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
			mScissorTest ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
			glDepthMask(mDepthMask);
		}
	};

	void ClientArrays()
	{
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glDisableVertexAttribArray(2);
	}

	// The camera picture over the whole screen, or only in the bars beside the game's 4:3 picture (theBars)
	void DrawCamera(bool theBars)
	{
		if (!gCameraLive || !gCameraUvValid)
			return;
		static const float sQuad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
		ClientArrays();
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glUseProgram(gCameraProgram);
		glUniform1f(gCameraDim, theBars ? 0.55f : 1.0f);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, gCameraTexture);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, sQuad);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, gCameraUv);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		if (!theBars)
		{
			glDisable(GL_SCISSOR_TEST);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
		else
		{
			const Sexy::Rect& aGame = gLawnApp->mGLInterface->mPresentationRect;
			glEnable(GL_SCISSOR_TEST);
			if (aGame.mX > 0)
			{
				glScissor(0, 0, aGame.mX, gViewH);
				glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
				glScissor(aGame.mX + aGame.mWidth, 0, gViewW - aGame.mX - aGame.mWidth, gViewH);
				glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			}
			if (aGame.mY > 0)
			{
				glScissor(0, 0, gViewW, aGame.mY);
				glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
				glScissor(0, aGame.mY + aGame.mHeight, gViewW, gViewH - aGame.mY - aGame.mHeight);
				glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			}
			glDisable(GL_SCISSOR_TEST);
		}
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
	}

	void DrawColor(const std::vector<ColorVertex>& theVertices, GLenum theMode, float thePointSize = 1.0f, const std::vector<unsigned short>* theIndices = nullptr)
	{
		if (theVertices.empty())
			return;
		ClientArrays();
		glUseProgram(gColorProgram);
		glUniformMatrix4fv(gColorViewProj, 1, GL_FALSE, gViewProj);
		glUniform1f(gColorPointSize, thePointSize);
		glUniform1f(gColorRound, theMode == GL_POINTS ? 1.0f : 0.0f);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), &theVertices[0].mX);
		glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), &theVertices[0].mR);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		if (theIndices)
			glDrawElements(theMode, static_cast<GLsizei>(theIndices->size()), GL_UNSIGNED_SHORT, theIndices->data());
		else
			glDrawArrays(theMode, 0, static_cast<GLsizei>(theVertices.size()));
	}

	// Every horizontal surface ARCore found, as a dotted area fading at its edge
	void DrawPlanes()
	{
		ArTrackableList* aList = nullptr;
		ArTrackableList_create(gSession, &aList);
		ArSession_getAllTrackables(gSession, AR_TRACKABLE_PLANE, aList);
		int32_t aCount = 0;
		ArTrackableList_getSize(gSession, aList, &aCount);
		std::vector<float> aVertices;	// x, y, z, edge
		std::vector<unsigned short> aIndices;
		ArPose* aPose = nullptr;
		ArPose_create(gSession, nullptr, &aPose);
		std::vector<float> aPolygon;
		gHasPlane = false;
		for (int32_t i = 0; i < aCount; i++)
		{
			ArTrackable* aTrackable = nullptr;
			ArTrackableList_acquireItem(gSession, aList, i, &aTrackable);
			ArPlane* aPlane = ArAsPlane(aTrackable);
			ArTrackingState aState = AR_TRACKING_STATE_STOPPED;
			ArTrackable_getTrackingState(gSession, aTrackable, &aState);
			ArPlane* aSubsumer = nullptr;
			ArPlane_acquireSubsumedBy(gSession, aPlane, &aSubsumer);
			ArPlaneType aType = AR_PLANE_VERTICAL;
			ArPlane_getType(gSession, aPlane, &aType);
			if (aSubsumer)
				ArTrackable_release(ArAsTrackable(aSubsumer));
			else if (aState == AR_TRACKING_STATE_TRACKING && aType == AR_PLANE_HORIZONTAL_UPWARD_FACING)
			{
				int32_t aSize = 0;
				ArPlane_getPolygonSize(gSession, aPlane, &aSize);
				if (aSize >= 6 && aVertices.size() / 4 + aSize / 2 + 1 < 60000)
				{
					aPolygon.resize(static_cast<size_t>(aSize));
					ArPlane_getPolygon(gSession, aPlane, aPolygon.data());
					ArPlane_getCenterPose(gSession, aPlane, aPose);
					float m[16];
					ArPose_getMatrix(gSession, aPose, m);
					gHasPlane = true;
					// A fan from the center (full) to the edge (faded)
					unsigned short aCenter = static_cast<unsigned short>(aVertices.size() / 4);
					aVertices.insert(aVertices.end(), { m[12], m[13], m[14], 1.0f });
					int aPoints = aSize / 2;
					for (int p = 0; p < aPoints; p++)
					{
						float x = aPolygon[p * 2], z = aPolygon[p * 2 + 1];
						aVertices.insert(aVertices.end(), { m[0] * x + m[8] * z + m[12], m[1] * x + m[9] * z + m[13], m[2] * x + m[10] * z + m[14], 0.0f });
						unsigned short a = static_cast<unsigned short>(aCenter + 1 + p), b = static_cast<unsigned short>(aCenter + 1 + (p + 1) % aPoints);
						aIndices.insert(aIndices.end(), { aCenter, a, b });
					}
				}
			}
			ArTrackable_release(aTrackable);
		}
		ArPose_destroy(aPose);
		ArTrackableList_destroy(aList);
		if (aIndices.empty())
			return;
		ClientArrays();
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		glUseProgram(gDotsProgram);
		glUniformMatrix4fv(gDotsViewProj, 1, GL_FALSE, gViewProj);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), aVertices.data());
		glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), aVertices.data() + 3);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(aIndices.size()), GL_UNSIGNED_SHORT, aIndices.data());
	}

	// While no surface is found yet, the feature points ARCore tracks: it is working, keep moving
	void DrawPoints()
	{
		ArPointCloud* aCloud = nullptr;
		if (ArFrame_acquirePointCloud(gSession, gFrame, &aCloud) != AR_SUCCESS || !aCloud)
			return;
		int32_t aCount = 0;
		ArPointCloud_getNumberOfPoints(gSession, aCloud, &aCount);
		const float* aData = nullptr;
		ArPointCloud_getData(gSession, aCloud, &aData);
		std::vector<ColorVertex> aPoints;
		for (int32_t i = 0; aData && i < aCount && i < 2000; i++)
		{
			float c = std::clamp(aData[i * 4 + 3], 0.3f, 1.0f);
			aPoints.push_back({ aData[i * 4], aData[i * 4 + 1], aData[i * 4 + 2], 0.58f * c, 0.83f * c, 0.42f * c, c });
		}
		ArPointCloud_release(aCloud);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		DrawColor(aPoints, GL_POINTS, std::max(6.0f, gViewH / 140.0f));
	}

	// A ring with a dot lying on the surface where the board would go
	void DrawReticle()
	{
		std::vector<ColorVertex> aVertices;
		std::vector<unsigned short> aIndices;
		const int SEGMENTS = 48;
		float x = gReticle[0], y = gReticle[1] + 0.001f, z = gReticle[2];
		for (int i = 0; i < SEGMENTS; i++)
		{
			float a = i * 2 * PI / SEGMENTS, c = std::cos(a), s = std::sin(a);
			aVertices.push_back({ x + c * 0.045f, y, z + s * 0.045f, 0.95f, 0.95f, 0.95f, 0.95f });
			aVertices.push_back({ x + c * 0.058f, y, z + s * 0.058f, 0.95f, 0.95f, 0.95f, 0.95f });
			unsigned short i0 = static_cast<unsigned short>(i * 2), i1 = static_cast<unsigned short>(((i + 1) % SEGMENTS) * 2);
			aIndices.insert(aIndices.end(), { i0, static_cast<unsigned short>(i0 + 1), i1, i1, static_cast<unsigned short>(i0 + 1), static_cast<unsigned short>(i1 + 1) });
		}
		unsigned short aCenter = static_cast<unsigned short>(aVertices.size());
		aVertices.push_back({ x, y, z, 0.58f, 0.83f, 0.42f, 1.0f });
		for (int i = 0; i < SEGMENTS; i++)
		{
			float a = i * 2 * PI / SEGMENTS;
			aVertices.push_back({ x + std::cos(a) * 0.008f, y, z + std::sin(a) * 0.008f, 0.58f, 0.83f, 0.42f, 1.0f });
			aIndices.insert(aIndices.end(), { aCenter, static_cast<unsigned short>(aCenter + 1 + i), static_cast<unsigned short>(aCenter + 1 + (i + 1) % SEGMENTS) });
		}
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		DrawColor(aVertices, GL_TRIANGLES, 1.0f, &aIndices);
	}

	// ---- the session

	void SetNotice(const std::string& theText)
	{
		gNotice = theText;
		gNoticeUntil = std::max<uint32_t>(1, SDL_GetTicks() + NOTICE_MS);
		PvzpLogLn("ARNative: {}", theText);
	}

	void Stop(State theState, const std::string& theWhy)
	{
		gState = theState;
		gStatus = theWhy;
		gStateSince = SDL_GetTicks();
		if (!theWhy.empty())
			SetNotice("Ver en AR: " + theWhy);
	}

	void ReleaseAnchor()
	{
		if (gAnchor)
		{
			ArAnchor_detach(gSession, gAnchor);
			ArAnchor_release(gAnchor);
			gAnchor = nullptr;
		}
	}

	void EndTouches();

	void DestroySession()
	{
		EndTouches();
		if (gSession)
		{
			ReleaseAnchor();
			ArSession_pause(gSession);
			if (gFrame)
				ArFrame_destroy(gFrame);
			ArSession_destroy(gSession);
			PvzpLogLn("ARNative: session closed");
		}
		gSession = nullptr;
		gFrame = nullptr;
		if (gCameraTexture)
		{
			glDeleteTextures(1, &gCameraTexture);
			gCameraTexture = 0;
		}
		gCameraLive = false;
		gCameraUvValid = false;
		gRotation = -1;
		gViewW = gViewH = 0;
		gPlaced = false;
		gFloating = false;
		gMode = Mode::PLACE;
		gShowing = false;
		gTracking = false;
		gResumeWanted = false;
		gIdlePaused = false;
		gHiddenSince = 0;
	}

	// The game goes to the background (the home button, another app): the camera is released until it comes back.
	int SDLCALL WatchApp(void*, SDL_Event* theEvent)
	{
		if (theEvent->type == SDL_APP_WILLENTERBACKGROUND)
		{
			gInBackground = true;
			if (gSession && gState == State::RUNNING && !gIdlePaused)
			{
				ArSession_pause(gSession);
				PvzpLogLn("ARNative: paused");
			}
		}
		else if (theEvent->type == SDL_APP_DIDENTERFOREGROUND)
		{
			gInBackground = false;
			gResumeWanted = gSession != nullptr && !gIdlePaused;
		}
		return 0;
	}

	bool CreateSession()
	{
		Jni j;
		if (!j.Ok())
		{
			Stop(State::STOPPED, "no se pudo hablar con Android (JNI)");
			return false;
		}
		ArStatus aStatus = ArSession_create(j.mEnv, j.mActivity, &gSession);
		if (aStatus != AR_SUCCESS || !gSession)
		{
			gSession = nullptr;
			Stop(State::STOPPED, aStatus == AR_UNAVAILABLE_DEVICE_NOT_COMPATIBLE ? "este cel no tiene ARCore, se queda en 2D" :
				aStatus == AR_ERROR_CAMERA_PERMISSION_NOT_GRANTED ? "sin permiso de camara, se queda en 2D" :
				std::format("no se pudo abrir ARCore ({}), se queda en 2D", static_cast<int>(aStatus)));
			return false;
		}
		ArConfig* aConfig = nullptr;
		ArConfig_create(gSession, &aConfig);
		ArConfig_setUpdateMode(gSession, aConfig, AR_UPDATE_MODE_LATEST_CAMERA_IMAGE);	// the game keeps its own frame rate
		ArConfig_setPlaneFindingMode(gSession, aConfig, AR_PLANE_FINDING_MODE_HORIZONTAL);
		ArConfig_setLightEstimationMode(gSession, aConfig, AR_LIGHT_ESTIMATION_MODE_DISABLED);
		ArConfig_setFocusMode(gSession, aConfig, AR_FOCUS_MODE_AUTO);
		aStatus = ArSession_configure(gSession, aConfig);
		ArConfig_destroy(aConfig);
		if (aStatus != AR_SUCCESS)
			PvzpLogLn("ARNative: configure failed ({})", static_cast<int>(aStatus));
		ArFrame_create(gSession, &gFrame);

		glGenTextures(1, &gCameraTexture);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, gCameraTexture);
		glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
		ArSession_setCameraTextureName(gSession, gCameraTexture);

		aStatus = ArSession_resume(gSession);
		if (aStatus != AR_SUCCESS)
		{
			DestroySession();
			Stop(State::STOPPED, aStatus == AR_ERROR_CAMERA_NOT_AVAILABLE ? "la camara la esta usando otra app, se queda en 2D" :
				std::format("ARCore no arranco ({}), se queda en 2D", static_cast<int>(aStatus)));
			return false;
		}
		if (!gWatching)
		{
			SDL_AddEventWatch(WatchApp, nullptr);
			gWatching = true;
		}
		gState = State::RUNNING;
		gStatus.clear();
		gStateSince = SDL_GetTicks();
		gCameraSince = 0;
		gTestPlaced = false;
		PvzpLogLn("ARNative: session running");
		return true;
	}

	// ARCore installed, the camera allowed, then the session; each step may wait for the player
	void Advance(uint32_t theNow)
	{
		if (gState == State::OFF)
		{
			gState = State::CHECKING;
			gStateSince = theNow;
			gInstallAsked = false;
			gPermissionAsked = false;
			gStatus = "comprobando ARCore...";
		}
		if (gState == State::CHECKING || gState == State::INSTALLING)
		{
			Jni j;
			if (!j.Ok())
			{
				Stop(State::STOPPED, "no se pudo hablar con Android (JNI)");
				return;
			}
			ArAvailability aAvailability = AR_AVAILABILITY_UNKNOWN_ERROR;
			ArCoreApk_checkAvailability(j.mEnv, j.mActivity, &aAvailability);
			if (aAvailability == AR_AVAILABILITY_UNKNOWN_CHECKING)
			{
				if (theNow - gStateSince > 10000)
					Stop(State::STOPPED, "no se pudo comprobar ARCore, se queda en 2D");
				return;
			}
			if (aAvailability == AR_AVAILABILITY_UNSUPPORTED_DEVICE_NOT_CAPABLE)
			{
				Stop(State::STOPPED, "este cel no tiene ARCore, se queda en 2D");
				return;
			}
			if (aAvailability == AR_AVAILABILITY_UNKNOWN_ERROR || aAvailability == AR_AVAILABILITY_UNKNOWN_TIMED_OUT)
			{
				Stop(State::STOPPED, "no se pudo comprobar ARCore, se queda en 2D");
				return;
			}
			if (gState == State::INSTALLING && (!(SDL_GetWindowFlags(static_cast<SDL_Window*>(gLawnApp->mWindow)) & SDL_WINDOW_INPUT_FOCUS) ||
					theNow - gStateSince < 1000))
				return;	// Play's install screen is still over the game
			if (aAvailability != AR_AVAILABILITY_SUPPORTED_INSTALLED || gState == State::INSTALLING)
			{
				// Opens Play's install screen once; when the game comes back the same call says how it went
				ArInstallStatus aInstall = AR_INSTALL_STATUS_INSTALLED;
				ArStatus aStatus = ArCoreApk_requestInstall(j.mEnv, j.mActivity, !gInstallAsked, &aInstall);
				gInstallAsked = true;
				if (aStatus != AR_SUCCESS)
				{
					Stop(State::STOPPED, aStatus == AR_UNAVAILABLE_USER_DECLINED_INSTALLATION ? "falta instalar Servicios de Google Play para RA, se queda en 2D" :
						std::format("no se pudo instalar ARCore ({}), se queda en 2D", static_cast<int>(aStatus)));
					return;
				}
				if (aInstall == AR_INSTALL_STATUS_INSTALL_REQUESTED)
				{
					gState = State::INSTALLING;
					gStateSince = theNow;
					gStatus = "instala Servicios de Google Play para RA y vuelve al juego";
					return;
				}
			}
			gState = State::ASKING;
			gStateSince = theNow;
		}
		if (gState == State::ASKING)
		{
			if (HasCameraPermission())
			{
				if (InitGL())
					CreateSession();
				else
					Stop(State::STOPPED, "los shaders de la AR no compilaron, se queda en 2D");
				return;
			}
			bool aFocused = (SDL_GetWindowFlags(static_cast<SDL_Window*>(gLawnApp->mWindow)) & SDL_WINDOW_INPUT_FOCUS) != 0;
			if (!gPermissionAsked)
			{
				RequestCameraPermission();
				gPermissionAsked = true;
				gPermissionLostFocus = false;
				gStateSince = theNow;
				gStatus = "permite la camara en el aviso de Android";
				PvzpLogLn("ARNative: asking for the camera");
				return;
			}
			if (!aFocused)
				gPermissionLostFocus = true;	// the dialog is up
			else if (gPermissionLostFocus || theNow - gStateSince > 2500)	// answered, or Android didn't even ask
				Stop(State::STOPPED, "sin permiso de camara, se queda en 2D (activalo en Ajustes > Apps > PvZ > Permisos)");
		}
	}

	// ---- placement

	void Anchor(const float thePos[3])
	{
		ReleaseAnchor();
		if (!gTracking)
			return;
		const float aRaw[7] = { 0, 0, 0, 1, thePos[0], thePos[1], thePos[2] };
		ArPose* aPose = nullptr;
		ArPose_create(gSession, aRaw, &aPose);
		if (ArSession_acquireNewAnchor(gSession, aPose, &gAnchor) != AR_SUCCESS)
			gAnchor = nullptr;
		ArPose_destroy(aPose);
	}

	void FaceThePlayer()
	{
		gYaw = std::atan2(gEye[0] - gBoardPos[0], gEye[2] - gBoardPos[2]);	// the near edge (+z) toward the camera
	}

	void SetMode(Mode theMode)
	{
		gMode = theMode;
		EndTouches();
		if (theMode == Mode::PLACE)
		{
			ReleaseAnchor();
			gPlaced = false;
			gFloating = false;
		}
	}

	// On a horizontal surface at the screen point, or at the reticle
	void PlaceAt(float theX, float theY)
	{
		float aPos[3];
		bool aFound = false;
		ArHitResultList* aList = nullptr;
		ArHitResultList_create(gSession, &aList);
		ArFrame_hitTest(gSession, gFrame, theX, theY, aList);
		int32_t aCount = 0;
		ArHitResultList_getSize(gSession, aList, &aCount);
		ArHitResult* aHit = nullptr;
		ArHitResult_create(gSession, &aHit);
		ArPose* aPose = nullptr;
		ArPose_create(gSession, nullptr, &aPose);
		for (int32_t i = 0; i < aCount && !aFound; i++)
		{
			ArHitResultList_getItem(gSession, aList, i, aHit);
			ArTrackable* aTrackable = nullptr;
			ArHitResult_acquireTrackable(gSession, aHit, &aTrackable);
			ArTrackableType aType = AR_TRACKABLE_NOT_VALID;
			ArTrackable_getType(gSession, aTrackable, &aType);
			if (aType == AR_TRACKABLE_PLANE)
			{
				ArPlaneType aPlaneType = AR_PLANE_VERTICAL;
				ArPlane_getType(gSession, ArAsPlane(aTrackable), &aPlaneType);
				ArHitResult_getHitPose(gSession, aHit, aPose);
				int32_t aInside = 0;
				ArPlane_isPoseInPolygon(gSession, ArAsPlane(aTrackable), aPose, &aInside);
				if (aInside && aPlaneType == AR_PLANE_HORIZONTAL_UPWARD_FACING)
				{
					float aRaw[7];
					ArPose_getPoseRaw(gSession, aPose, aRaw);
					aPos[0] = aRaw[4];
					aPos[1] = aRaw[5];
					aPos[2] = aRaw[6];
					aFound = true;
					ReleaseAnchor();
					if (ArHitResult_acquireNewAnchor(gSession, aHit, &gAnchor) != AR_SUCCESS)
						gAnchor = nullptr;
				}
			}
			ArTrackable_release(aTrackable);
		}
		ArPose_destroy(aPose);
		ArHitResult_destroy(aHit);
		ArHitResultList_destroy(aList);
		if (!aFound && gReticleOk)
		{
			std::memcpy(aPos, gReticle, sizeof(aPos));
			Anchor(aPos);
			aFound = true;
		}
		if (!aFound)
			return;
		std::memcpy(gBoardPos, aPos, sizeof(aPos));
		FaceThePlayer();
		gPlaced = true;
		gFloating = false;
		gMode = Mode::ADJUST;
		PvzpLogLn("ARNative: placed at {:.2f},{:.2f},{:.2f}{}", aPos[0], aPos[1], aPos[2], gAnchor ? " (anchored)" : "");
	}

	// "Sin mesa": half a meter ahead and a little below the camera, facing it. Anchored when tracking; otherwise it
	// floats with the camera, which also shows it in a test with the phone lying still.
	void FloatingPose(float thePos[3])
	{
		float aCam[16];
		MatInvert(gView, aCam);
		const float f[3] = { -aCam[8], -aCam[9], -aCam[10] };	// forward
		float aFlat = std::sqrt(f[0] * f[0] + f[2] * f[2]);
		float fx = aFlat > 1e-3f ? f[0] / aFlat : 0.0f, fz = aFlat > 1e-3f ? f[2] / aFlat : -1.0f;
		// Lying flat a little below the camera while ARCore tracks
		thePos[0] = gEye[0] + fx * 0.5f;
		thePos[1] = gEye[1] - 0.25f;
		thePos[2] = gEye[2] + fz * 0.5f;
		if (gTracking)
			return;
		// Without tracking the view stays put (a phone lying still): right ahead of the lens, seen from 55 degrees
		// above as a table would be, its near edge at the bottom of the screen
		const float r[3] = { aCam[0], aCam[1], aCam[2] }, u[3] = { aCam[4], aCam[5], aCam[6] }, b[3] = { aCam[8], aCam[9], aCam[10] };
		const float e = 55.0f * PI / 180.0f;
		float y[3], z[3];
		for (int i = 0; i < 3; i++)
		{
			y[i] = b[i] * std::sin(e) + u[i] * std::cos(e);
			thePos[i] = gEye[i] - b[i] * 0.42f;
		}
		z[0] = r[1] * y[2] - r[2] * y[1];
		z[1] = r[2] * y[0] - r[0] * y[2];
		z[2] = r[0] * y[1] - r[1] * y[0];
		for (int i = 0; i < 3; i++)
		{
			gFloatAxes[i] = r[i];
			gFloatAxes[3 + i] = y[i];
			gFloatAxes[6 + i] = z[i];
		}
	}

	void PlaceInFront()
	{
		FloatingPose(gBoardPos);
		FaceThePlayer();
		Anchor(gBoardPos);
		gFloating = !gAnchor;
		gPlaced = true;
		gMode = Mode::ADJUST;
		PvzpLogLn("ARNative: placed in front of the camera ({})", gAnchor ? "anchored" : "floating, not tracking");
	}

	void UpdateReticle()
	{
		gReticleOk = false;
		if (gMode != Mode::PLACE || !gTracking)
			return;
		ArHitResultList* aList = nullptr;
		ArHitResultList_create(gSession, &aList);
		ArFrame_hitTest(gSession, gFrame, gViewW * 0.5f, gViewH * 0.5f, aList);
		int32_t aCount = 0;
		ArHitResultList_getSize(gSession, aList, &aCount);
		ArHitResult* aHit = nullptr;
		ArHitResult_create(gSession, &aHit);
		ArPose* aPose = nullptr;
		ArPose_create(gSession, nullptr, &aPose);
		for (int32_t i = 0; i < aCount && !gReticleOk; i++)
		{
			ArHitResultList_getItem(gSession, aList, i, aHit);
			ArTrackable* aTrackable = nullptr;
			ArHitResult_acquireTrackable(gSession, aHit, &aTrackable);
			ArTrackableType aType = AR_TRACKABLE_NOT_VALID;
			ArTrackable_getType(gSession, aTrackable, &aType);
			if (aType == AR_TRACKABLE_PLANE)
			{
				ArPlaneType aPlaneType = AR_PLANE_VERTICAL;
				ArPlane_getType(gSession, ArAsPlane(aTrackable), &aPlaneType);
				ArHitResult_getHitPose(gSession, aHit, aPose);
				int32_t aInside = 0;
				ArPlane_isPoseInPolygon(gSession, ArAsPlane(aTrackable), aPose, &aInside);
				if (aInside && aPlaneType == AR_PLANE_HORIZONTAL_UPWARD_FACING)
				{
					float aRaw[7];
					ArPose_getPoseRaw(gSession, aPose, aRaw);
					gReticle[0] = aRaw[4];
					gReticle[1] = aRaw[5];
					gReticle[2] = aRaw[6];
					gReticleOk = true;
				}
			}
			ArTrackable_release(aTrackable);
		}
		ArPose_destroy(aPose);
		ArHitResult_destroy(aHit);
		ArHitResultList_destroy(aList);
	}

	// The board stays on its anchor, which ARCore moves as it learns the room
	void UpdateAnchor()
	{
		if (gFloating && gPlaced)
		{
			if (gTracking && gCameraLive)
			{
				FloatingPose(gBoardPos);
				FaceThePlayer();
				Anchor(gBoardPos);
				gFloating = !gAnchor;
			}
			else
				FloatingPose(gBoardPos);
		}
		if (!gAnchor || gDragging)
			return;
		ArTrackingState aState = AR_TRACKING_STATE_STOPPED;
		ArAnchor_getTrackingState(gSession, gAnchor, &aState);
		if (aState == AR_TRACKING_STATE_STOPPED)
		{
			PvzpLogLn("ARNative: the anchor stopped tracking, place the board again");
			SetMode(Mode::PLACE);
			return;
		}
		if (aState != AR_TRACKING_STATE_TRACKING)
			return;
		ArPose* aPose = nullptr;
		ArPose_create(gSession, nullptr, &aPose);
		ArAnchor_getPose(gSession, gAnchor, aPose);
		float aRaw[7];
		ArPose_getPoseRaw(gSession, aPose, aRaw);
		ArPose_destroy(aPose);
		gBoardPos[0] = aRaw[4];
		gBoardPos[1] = aRaw[5];
		gBoardPos[2] = aRaw[6];
	}

	// ---- touches

	void SendMouse(ARView::Mouse theAction, int theX, int theY)
	{
		if (theAction == ARView::Mouse::DOWN)
			PvzpLogLn("ARNative: click at {},{}", theX, theY);
		ARView::QueueMouse(theAction, theX, theY);
	}

	void EndTouches()
	{
		if (gPressSent)
			SendMouse(ARView::Mouse::UP, gPressX, gPressY);
		gPressSent = false;
		gPressPending = false;
		if (gDragging && gPlaced)
			Anchor(gBoardPos);
		gDragging = false;
		gPinchDistance = 0;
		gFingers.clear();
		gGestureSpoiled = false;
	}

	bool GameAt(float theX, float theY, int& theGameX, int& theGameY)
	{
		float o[3], d[3], aModel[16];
		if (!ScreenRay(theX, theY, o, d))
			return false;
		BoardModel(aModel);
		return ARDiorama::Pick(aModel, o, d, &theGameX, &theGameY);
	}

	Finger* FindFinger(SDL_FingerID theId)
	{
		for (Finger& f : gFingers)
			if (f.mId == theId)
				return &f;
		return nullptr;
	}

	void TwoFingers(float& theDistance, float& theAngle)
	{
		float dx = gFingers[1].mX - gFingers[0].mX, dy = gFingers[1].mY - gFingers[0].mY;
		theDistance = std::sqrt(dx * dx + dy * dy);
		theAngle = std::atan2(dy, dx);
	}

	void FingerDown(const Finger& theFinger)
	{
		if (gMode == Mode::PLAY && gFingers.size() == 1)
		{
			int x, y;
			if (GameAt(theFinger.mX, theFinger.mY, x, y))
			{
				gPressPending = true;
				gPressAt = SDL_GetTicks();
				gPressX = x;
				gPressY = y;
			}
		}
		else if (gMode == Mode::PLAY)
		{
			// a second finger: not a click after all
			if (gPressSent)
				SendMouse(ARView::Mouse::UP, gPressX, gPressY);
			gPressSent = false;
			gPressPending = false;
			gGestureSpoiled = true;
		}
		else if (gMode == Mode::ADJUST && gFingers.size() == 1)
		{
			float o[3], d[3], p[3];
			if (ScreenRay(theFinger.mX, theFinger.mY, o, d) && RayOnPlaneY(o, d, gBoardPos[1], p))
			{
				gDragOffset[0] = gBoardPos[0] - p[0];
				gDragOffset[1] = gBoardPos[2] - p[2];
				gDragging = true;
				ReleaseAnchor();
			}
		}
		else if (gMode == Mode::ADJUST && gFingers.size() == 2)
		{
			gDragging = true;
			ReleaseAnchor();
			TwoFingers(gPinchDistance, gPinchAngle);
		}
	}

	void FingerMoved()
	{
		uint32_t aNow = SDL_GetTicks();
		if (gMode == Mode::PLAY && gPressSent && gFingers.size() == 1)
		{
			int x, y;
			if (aNow - gPressMoveAt >= 50 && GameAt(gFingers[0].mX, gFingers[0].mY, x, y) && std::abs(x - gPressX) + std::abs(y - gPressY) >= 3)
			{
				gPressX = x;
				gPressY = y;
				gPressMoveAt = aNow;
				SendMouse(ARView::Mouse::MOVE, x, y);
			}
		}
		else if (gMode == Mode::ADJUST && gDragging && gFingers.size() == 1 && gPinchDistance == 0)
		{
			float o[3], d[3], p[3];
			if (ScreenRay(gFingers[0].mX, gFingers[0].mY, o, d) && RayOnPlaneY(o, d, gBoardPos[1], p))
			{
				gBoardPos[0] = p[0] + gDragOffset[0];
				gBoardPos[2] = p[2] + gDragOffset[1];
			}
		}
		else if (gMode == Mode::ADJUST && gFingers.size() == 2 && gPinchDistance > 0)
		{
			float aDistance, aAngle;
			TwoFingers(aDistance, aAngle);
			if (aDistance > 1)
				gWidth = std::clamp(gWidth * aDistance / gPinchDistance, MIN_WIDTH, MAX_WIDTH);
			float aTurn = aAngle - gPinchAngle;
			if (aTurn > PI)
				aTurn -= 2 * PI;
			else if (aTurn < -PI)
				aTurn += 2 * PI;
			gYaw -= aTurn;	// turning the fingers clockwise on screen turns the board clockwise seen from above
			gPinchDistance = std::max(1.0f, aDistance);
			gPinchAngle = aAngle;
		}
	}

	void FingerUp(const Finger& theFinger, size_t theLeft)
	{
		if (gMode == Mode::PLACE)
		{
			float aSlop = TAP_SLOP * gViewH;
			if (theLeft == 0 && !gGestureSpoiled && std::fabs(theFinger.mX - theFinger.mStartX) + std::fabs(theFinger.mY - theFinger.mStartY) < aSlop * 2 && gTracking)
				PlaceAt(theFinger.mX, theFinger.mY);
		}
		else if (gMode == Mode::PLAY)
		{
			if (gPressPending && !gGestureSpoiled)
			{
				// lifted before the click went out: a quick tap
				SendMouse(ARView::Mouse::DOWN, gPressX, gPressY);
				gPressSent = true;
			}
			gPressPending = false;
			if (gPressSent)
				SendMouse(ARView::Mouse::UP, gPressX, gPressY);
			gPressSent = false;
		}
		else if (gMode == Mode::ADJUST)
		{
			if (theLeft == 1)
			{
				// from two fingers back to one: it drags from where it is now
				gPinchDistance = 0;
				float o[3], d[3], p[3];
				const Finger& aLeft = gFingers[0].mId == theFinger.mId ? gFingers[1] : gFingers[0];
				if (ScreenRay(aLeft.mX, aLeft.mY, o, d) && RayOnPlaneY(o, d, gBoardPos[1], p))
				{
					gDragOffset[0] = gBoardPos[0] - p[0];
					gDragOffset[1] = gBoardPos[2] - p[2];
				}
			}
			else if (theLeft == 0 && gDragging)
			{
				gDragging = false;
				gPinchDistance = 0;
				Anchor(gBoardPos);	// it stays where it was left
			}
		}
	}

	// A held finger in play mode clicks once nobody else joined it for a moment
	void FlushPress(uint32_t theNow)
	{
		if (gPressPending && !gGestureSpoiled && theNow - gPressAt >= TAP_DELAY_MS)
		{
			gPressPending = false;
			gPressSent = true;
			gPressMoveAt = theNow;
			SendMouse(ARView::Mouse::DOWN, gPressX, gPressY);
		}
	}

	// ---- per frame

	void LogGLError(const char* theWhere)
	{
		static int sLogged = 0;
		GLenum aError = glGetError();
		if (aError != GL_NO_ERROR && sLogged < 20)
		{
			sLogged++;
			PvzpLogLn("ARNative: GL error 0x{:x} after {}", static_cast<unsigned>(aError), theWhere);
		}
	}

	void UpdateGeometry(uint32_t theNow)
	{
		int aWidth = 0, aHeight = 0;
		SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(gLawnApp->mWindow), &aWidth, &aHeight);
		int aRotation = gRotation;
		if (gRotation < 0 || theNow - gLastRotationCheck >= ROTATION_CHECK_MS)
		{
			gLastRotationCheck = theNow;
			int r = DisplayRotation();
			if (r >= 0)
				aRotation = r;
		}
		if (aRotation < 0)
			aRotation = 1;	// the activity is landscape
		if (aRotation == gRotation && aWidth == gViewW && aHeight == gViewH)
			return;
		gRotation = aRotation;
		gViewW = aWidth;
		gViewH = aHeight;
		ArSession_setDisplayGeometry(gSession, gRotation, gViewW, gViewH);
		int aWinW = 0, aWinH = 0;
		SDL_GetWindowSize(static_cast<SDL_Window*>(gLawnApp->mWindow), &aWinW, &aWinH);
		PvzpLogLn("ARNative: display rotation {} ({} degrees), view {}x{} px, window {}x{}, SDL orientation {}", gRotation, gRotation * 90,
			gViewW, gViewH, aWinW, aWinH, static_cast<int>(SDL_GetDisplayOrientation(SDL_GetWindowDisplayIndex(static_cast<SDL_Window*>(gLawnApp->mWindow)))));
	}

	void UpdateCamera()
	{
		int32_t aChanged = 0;
		ArFrame_getDisplayGeometryChanged(gSession, gFrame, &aChanged);
		if (aChanged || !gCameraUvValid)
		{
			static const float sQuad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
			ArFrame_transformCoordinates2d(gSession, gFrame, AR_COORDINATES_2D_OPENGL_NORMALIZED_DEVICE_COORDINATES, 4, sQuad,
				AR_COORDINATES_2D_TEXTURE_NORMALIZED, gCameraUv);
			gCameraUvValid = true;
			PvzpLogLn("ARNative: camera uv {:.3f},{:.3f} {:.3f},{:.3f} {:.3f},{:.3f} {:.3f},{:.3f}", gCameraUv[0], gCameraUv[1], gCameraUv[2], gCameraUv[3],
				gCameraUv[4], gCameraUv[5], gCameraUv[6], gCameraUv[7]);
		}
		int64_t aTimestamp = 0;
		ArFrame_getTimestamp(gSession, gFrame, &aTimestamp);
		if (aTimestamp != 0 && !gCameraLive)
		{
			gCameraLive = true;
			gCameraSince = SDL_GetTicks();
			PvzpLogLn("ARNative: first camera image");
		}

		ArCamera* aCamera = nullptr;
		ArFrame_acquireCamera(gSession, gFrame, &aCamera);
		ArCamera_getViewMatrix(gSession, aCamera, gView);
		ArCamera_getProjectionMatrix(gSession, aCamera, 0.03f, 30.0f, gProj);
		ArTrackingState aState = AR_TRACKING_STATE_STOPPED;
		ArCamera_getTrackingState(gSession, aCamera, &aState);
		bool aTracking = aState == AR_TRACKING_STATE_TRACKING;
		ArTrackingFailureReason aFailure = AR_TRACKING_FAILURE_REASON_NONE;
		ArCamera_getTrackingFailureReason(gSession, aCamera, &aFailure);
		ArCamera_release(aCamera);
		if (aTracking != gTracking)
			PvzpLogLn("ARNative: {}", aTracking ? "tracking" : std::format("not tracking (reason {})", static_cast<int>(aFailure)));
		gTracking = aTracking;
		gFailure = aFailure;
		MatMul(gProj, gView, gViewProj);
		float aCam[16];
		if (MatInvert(gView, aCam))
		{
			gEye[0] = aCam[12];
			gEye[1] = aCam[13];
			gEye[2] = aCam[14];
		}
	}

	void Draw()
	{
		DrawCamera(false);
		if (!gTracking && !gFloating)
			return;
		if (gMode == Mode::PLACE || gMode == Mode::ADJUST)
			DrawPlanes();
		if (gMode == Mode::PLACE)
		{
			if (!gHasPlane)
				DrawPoints();
			if (gReticleOk)
				DrawReticle();
		}
		if (gPlaced)
		{
			float aModel[16];
			BoardModel(aModel);
			glDepthMask(GL_TRUE);
			glClear(GL_DEPTH_BUFFER_BIT);
			ARDiorama::Draw(aModel, gView, gProj);
		}
	}

	const char* FailureText()
	{
		switch (gFailure)
		{
		case AR_TRACKING_FAILURE_REASON_BAD_STATE:				return "Se perdio el seguimiento: espera un momento";
		case AR_TRACKING_FAILURE_REASON_INSUFFICIENT_LIGHT:		return "Hay muy poca luz: enciende una lampara";
		case AR_TRACKING_FAILURE_REASON_EXCESSIVE_MOTION:		return "Mueve el cel mas despacio";
		case AR_TRACKING_FAILURE_REASON_INSUFFICIENT_FEATURES:	return "Apunta a una superficie con mas textura (no lisa ni brillante)";
		case AR_TRACKING_FAILURE_REASON_CAMERA_UNAVAILABLE:		return "La camara no esta disponible (otra app la usa)";
		default:												return "Mueve el cel despacio para que la AR se ubique";
		}
	}

	std::string Hint()
	{
		if (!gCameraLive)
			return "Abriendo la camara...";
		if (!gTracking && !gFloating)
			return FailureText();
		switch (gMode)
		{
		case Mode::PLACE:
			if (!gHasPlane)
				return "Busca la mesa: mueve el cel despacio sobre ella";
			return gReticleOk ? "Toca para poner el jardin donde esta el circulo" : "Apunta a la mesa hasta ver el circulo";
		case Mode::ADJUST:
			return "Ajustar: arrastra con un dedo, gira y cambia el tamano con dos. Luego Fijar";
		default:
			return "Jugar: toca el jardin para hacer clic";
		}
	}
}

void ARNative::OnFrame()
{
	static const bool sForced = [] {
		const char* aEnv = SDL_getenv("PVZ_AR_NATIVE");
		return aEnv && *aEnv && SDL_strcmp(aEnv, "0") != 0;
	}();
	if (!gLawnApp || !gLawnApp->mWindow || !gLawnApp->mGLInterface)
		return;
	uint32_t aNow = SDL_GetTicks();
	bool aWanted = (sForced && !gForcedOff) || Mods::Settings().mARNative;
	if (Mods::Settings().mARNative)
		gForcedOff = false;
	if (!aWanted)
	{
		if (gState != State::OFF)
		{
			DestroySession();
			gState = State::OFF;
			gStatus.clear();
		}
		return;
	}
	if (gInBackground)
		return;
	if (gState == State::STOPPED)
		return;
	static const int sDebug = SDL_getenv("PVZ_AR_DEBUG") ? SDL_atoi(SDL_getenv("PVZ_AR_DEBUG")) : 0;
	GLBackup aBackup;	// the game's GL state, before anything here touches it
	if (gState != State::RUNNING)
	{
		Advance(aNow);
		if (gState != State::RUNNING)
			return;
	}

	// In menus for long the camera rests (battery, and Android's camera dot); the next lawn wakes it
	ARDiorama::Update(gLawnApp, aNow, SCENE_INTERVAL_MS);
	bool aShowing = ARDiorama::HasScene();
	if (aShowing)
		gHiddenSince = 0;
	else if (gHiddenSince == 0)
		gHiddenSince = std::max<uint32_t>(1, aNow);
	if (!gIdlePaused && !aShowing && aNow - gHiddenSince >= IDLE_PAUSE_MS)
	{
		ArSession_pause(gSession);
		gIdlePaused = true;
		gCameraLive = false;
		PvzpLogLn("ARNative: no lawn for a while, the camera rests");
	}
	if (gIdlePaused)
	{
		if (!aShowing)
			return;
		gIdlePaused = false;
		gResumeWanted = true;
	}

	if (gResumeWanted)
	{
		gResumeWanted = false;
		ArStatus aStatus = ArSession_resume(gSession);
		PvzpLogLn("ARNative: resumed ({})", static_cast<int>(aStatus));
		gCameraLive = false;
		gCameraUvValid = false;
		gRotation = -1;
		EndTouches();
	}
	UpdateGeometry(aNow);
	LogGLError("the game's frame");
	// ARCore may do GL work of its own in update; the game's context and surface must be current again after it
	constexpr int32_t EGL_DRAW_SURFACE = 0x3059, EGL_READ_SURFACE = 0x305A;
	void* aDisplay = eglGetCurrentDisplay();
	void* aContext = eglGetCurrentContext();
	void* aDraw = eglGetCurrentSurface(EGL_DRAW_SURFACE);
	void* aRead = eglGetCurrentSurface(EGL_READ_SURFACE);
	ArStatus aStatus = (sDebug & 1) ? AR_ERROR_NOT_TRACKING : ArSession_update(gSession, gFrame);
	aBackup.RestoreVertexArray();
	ResetAfterARCore();
	if (eglGetCurrentContext() != aContext || eglGetCurrentSurface(EGL_DRAW_SURFACE) != aDraw || eglGetCurrentSurface(EGL_READ_SURFACE) != aRead)
	{
		static int sSwitched = 0;
		if (sSwitched++ < 5)
			PvzpLogLn("ARNative: update changed the current EGL context or surface, putting the game's back");
		eglMakeCurrent(aDisplay, aDraw, aRead, aContext);
	}
	LogGLError("update");
	if (aStatus != AR_SUCCESS)
	{
		if (aStatus != gLastUpdateError)
			PvzpLogLn("ARNative: update failed ({})", static_cast<int>(aStatus));
		gLastUpdateError = aStatus;
		gShowing = false;
		return;
	}
	gLastUpdateError = AR_SUCCESS;
	UpdateCamera();

	static const bool sTestPose = SDL_getenv("PVZ_AR_TEST_POSE") != nullptr;
	if (sTestPose && !gTestPlaced && gCameraLive && !gPlaced && aNow - gCameraSince >= 1500)
	{
		gTestPlaced = true;
		PlaceInFront();
	}

	if (aShowing != gShowing)
	{
		PvzpLogLn("ARNative: {}", aShowing ? "lawn in play, AR view" : "no lawn, 2D view");
		EndTouches();
	}
	gShowing = aShowing;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, gViewW, gViewH);
	if (!gShowing)
	{
		if (!(sDebug & 2))
			DrawCamera(true);
	}
	else
	{
		UpdateReticle();
		UpdateAnchor();
		FlushPress(aNow);
		Draw();
	}
	LogGLError("draw");
}

bool ARNative::ProcessEvent(const SDL_Event& theEvent, bool theTouchOnMenu)
{
	if (gState != State::RUNNING || !gShowing)
		return false;
	switch (theEvent.type)
	{
	case SDL_MOUSEMOTION:
		return theEvent.motion.which == SDL_TOUCH_MOUSEID;	// the 2D board under the AR view doesn't get the finger
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		return theEvent.button.which == SDL_TOUCH_MOUSEID;
	case SDL_FINGERDOWN:
	{
		if (theTouchOnMenu)
			return false;
		if (gFingers.size() >= 2)
		{
			gGestureSpoiled = true;
			return true;
		}
		Finger f;
		f.mId = theEvent.tfinger.fingerId;
		f.mX = f.mStartX = theEvent.tfinger.x * gViewW;
		f.mY = f.mStartY = theEvent.tfinger.y * gViewH;
		gFingers.push_back(f);
		FingerDown(gFingers.back());
		return true;
	}
	case SDL_FINGERMOTION:
	{
		Finger* f = FindFinger(theEvent.tfinger.fingerId);
		if (!f)
			return false;
		f->mX = theEvent.tfinger.x * gViewW;
		f->mY = theEvent.tfinger.y * gViewH;
		FingerMoved();
		return true;
	}
	case SDL_FINGERUP:
	{
		Finger* f = FindFinger(theEvent.tfinger.fingerId);
		if (!f)
			return false;
		f->mX = theEvent.tfinger.x * gViewW;
		f->mY = theEvent.tfinger.y * gViewH;
		Finger aGone = *f;
		FingerUp(aGone, gFingers.size() - 1);
		gFingers.erase(std::remove_if(gFingers.begin(), gFingers.end(), [&](const Finger& g) { return g.mId == aGone.mId; }), gFingers.end());
		if (gFingers.empty())
			gGestureSpoiled = false;
		return true;
	}
	default:
		return false;
	}
}

void ARNative::CancelTouch()
{
	if (gPressSent)
		SendMouse(ARView::Mouse::UP, gPressX, gPressY);
	gPressSent = false;
	gPressPending = false;
	gGestureSpoiled = !gFingers.empty();
}

bool ARNative::OverlayShown()
{
	return (gState == State::RUNNING && gShowing) || (!gNotice.empty() && SDL_GetTicks() < gNoticeUntil);
}

namespace
{
	const ImVec4 COLOR_PANEL(20 / 255.0f, 17 / 255.0f, 14 / 255.0f, 0.82f);
	const ImVec4 COLOR_LINE(77 / 255.0f, 69 / 255.0f, 61 / 255.0f, 1.0f);
	const ImVec4 COLOR_TEXT(243 / 255.0f, 238 / 255.0f, 231 / 255.0f, 1.0f);
	const ImVec4 COLOR_ACCENT(147 / 255.0f, 212 / 255.0f, 107 / 255.0f, 1.0f);
	const ImVec4 COLOR_INK(20 / 255.0f, 36 / 255.0f, 9 / 255.0f, 1.0f);

	bool BarButton(const char* theLabel, float theHeight, bool theMain = false, bool theEnabled = true)
	{
		ImGui::BeginDisabled(!theEnabled);
		if (theMain)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_ACCENT);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLOR_ACCENT);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.95f, 0.62f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Text, COLOR_INK);
		}
		bool aPressed = ImGui::Button(theLabel, ImVec2(0, theHeight));
		if (theMain)
			ImGui::PopStyleColor(4);
		ImGui::EndDisabled();
		return aPressed;
	}

	constexpr ImGuiWindowFlags BAR_FLAGS = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
}

void ARNative::DrawOverlay(float theScale)
{
	const ImGuiIO& io = ImGui::GetIO();
	float u = theScale;
	float aMargin = 10 * u;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14 * u);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12 * u);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * u, 6 * u));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * u, 6 * u));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * u, 6 * u));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, COLOR_PANEL);
	ImGui::PushStyleColor(ImGuiCol_Text, COLOR_TEXT);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.14f, 0.12f, 0.92f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.20f, 0.17f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.29f, 0.25f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, COLOR_LINE);

	uint32_t aNow = SDL_GetTicks();
	if (!gNotice.empty() && aNow < gNoticeUntil)
	{
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - aMargin), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(io.DisplaySize.x * 0.8f, io.DisplaySize.y));
		ImGui::Begin("##arnotice", nullptr, BAR_FLAGS | ImGuiWindowFlags_NoInputs);
		ImGui::PushTextWrapPos(io.DisplaySize.x * 0.78f);
		ImGui::TextUnformatted(gNotice.c_str());
		ImGui::PopTextWrapPos();
		ImGui::End();
	}

	if (gState == State::RUNNING && gShowing)
	{
		float aButton = 34 * u;
		// The hint, top left
		ImGui::SetNextWindowPos(ImVec2(aMargin, aMargin), ImGuiCond_Always);
		ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(io.DisplaySize.x * 0.62f, io.DisplaySize.y * 0.3f));
		ImGui::Begin("##arhint", nullptr, BAR_FLAGS | ImGuiWindowFlags_NoInputs);
		ImGui::PushTextWrapPos(io.DisplaySize.x * 0.6f);
		ImGui::TextUnformatted(Hint().c_str());
		ImGui::PopTextWrapPos();
		ImGui::End();

		// Leave, top right
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - aMargin, aMargin), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::Begin("##artop", nullptr, BAR_FLAGS);
		if (BarButton("Salir del AR", aButton))
		{
			Mods::Settings().mARNative = false;
			Mods::Save(gLawnApp);
			gForcedOff = true;
		}
		ImGui::End();

		// The controls, bottom center
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - aMargin), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::Begin("##arcontrols", nullptr, BAR_FLAGS);
		if (gMode == Mode::PLACE)
		{
			if (BarButton("Sin mesa", aButton, false, gCameraLive))
				PlaceInFront();
			ImGui::SetItemTooltip("Pone el jardin delante, sin buscar una superficie");
		}
		else
		{
			if (gMode == Mode::ADJUST)
			{
				if (BarButton("-", aButton))
					gWidth = std::max(MIN_WIDTH, gWidth / 1.25f);
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%d cm", static_cast<int>(std::lround(gWidth * 100)));
				ImGui::SameLine();
				if (BarButton("+", aButton))
					gWidth = std::min(MAX_WIDTH, gWidth * 1.25f);
				ImGui::SameLine();
				if (BarButton("Girar 90", aButton))
					gYaw += PI / 2;
				ImGui::SameLine();
				if (BarButton("De frente", aButton))
					FaceThePlayer();
				ImGui::SameLine();
			}
			if (BarButton("Recolocar", aButton))
				SetMode(Mode::PLACE);
			ImGui::SameLine();
			if (BarButton(gMode == Mode::PLAY ? "Ajustar" : "Fijar", aButton, true))
				SetMode(gMode == Mode::PLAY ? Mode::ADJUST : Mode::PLAY);
		}
		ImGui::End();
	}

	ImGui::PopStyleColor(6);
	ImGui::PopStyleVar(6);
}

std::string ARNative::StatusText()
{
	switch (gState)
	{
	case State::OFF:
		return "";
	case State::RUNNING:
		if (!gShowing)
			return "AR lista: se ve en el jardin al jugar un nivel";
		return gPlaced ? "AR en marcha" : "AR en marcha: buscando la mesa";
	default:
		return gStatus;
	}
}
