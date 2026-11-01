// Temporary stand-in for the 3D diorama (ARDiorama.h), so the phone's AR can be placed and played before the real
// one lands: the game's 2D picture on a flat board lying on the table. Goes away when the diorama is merged.
#include "ARDiorama.h"
#include "LawnApp.h"
#include "PvzpLib/PvzpDebug.h"
#include "SexyAppFramework/graphics/GLInterface.h"
#include "SexyAppFramework/graphics/GLPlatform.h"
#include <algorithm>
#include <cmath>

namespace
{
	constexpr float	WIDTH = 0.40f;
	GLuint			gTexture = 0;
	int				gTexWidth = 0;
	int				gTexHeight = 0;
	uint32_t		gLast = 0;
	bool			gHasScene = false;
	GLuint			gProgram = 0;
	GLint			gMvp = -1;

	const char* VS = R"(
attribute vec3 aPos;
attribute vec2 aUv;
uniform mat4 uMvp;
varying vec2 vUv;
void main(){
	vUv=aUv;
	gl_Position=uMvp*vec4(aPos,1.0);
})";
	const char* FS = R"(
precision mediump float;
uniform sampler2D uTex;
varying vec2 vUv;
void main(){
	gl_FragColor=vec4(texture2D(uTex,vUv).rgb,1.0);
})";

	GLuint Compile(GLenum theType, const char* theSource)
	{
		GLuint aShader = glCreateShader(theType);
		glShaderSource(aShader, 1, &theSource, nullptr);
		glCompileShader(aShader);
		return aShader;
	}

	void Mul(const float a[16], const float b[16], float out[16])
	{
		float r[16];
		for (int c = 0; c < 4; c++)
			for (int k = 0; k < 4; k++)
				r[c * 4 + k] = a[k] * b[c * 4] + a[4 + k] * b[c * 4 + 1] + a[8 + k] * b[c * 4 + 2] + a[12 + k] * b[c * 4 + 3];
		std::copy(r, r + 16, out);
	}

	// Inverse of a rotation (around y), uniform scale and translation
	void InvertModel(const float m[16], float out[16])
	{
		float s2 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
		s2 = s2 > 1e-12f ? s2 : 1.0f;
		for (int c = 0; c < 3; c++)
			for (int r = 0; r < 3; r++)
				out[c * 4 + r] = m[r * 4 + c] / s2;
		for (int r = 0; r < 3; r++)
			out[12 + r] = -(out[r] * m[12] + out[4 + r] * m[13] + out[8 + r] * m[14]);
		out[3] = out[7] = out[11] = 0;
		out[15] = 1;
	}
}

void ARDiorama::Update(LawnApp* theApp, uint32_t theNow, uint32_t theIntervalMs)
{
	gHasScene = theApp && theApp->mBoard && theApp->mGameScene == GameScenes::SCENE_PLAYING && theApp->GetDialogCount() == 0;
	if (!gHasScene || (gTexture && theNow - gLast < theIntervalMs))
		return;
	gLast = theNow;
	// The game's picture, as GLInterface letterboxes it, straight from the screen it was just drawn on
	const Sexy::Rect& aRect = theApp->mGLInterface->mPresentationRect;
	GLint aBound = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &aBound);
	if (!gTexture)
		glGenTextures(1, &gTexture);
	glBindTexture(GL_TEXTURE_2D, gTexture);
	if (aRect.mWidth != gTexWidth || aRect.mHeight != gTexHeight)
	{
		gTexWidth = aRect.mWidth;
		gTexHeight = aRect.mHeight;
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, gTexWidth, gTexHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, aRect.mX, aRect.mY, gTexWidth, gTexHeight);
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(aBound));
}

bool ARDiorama::HasScene()
{
	return gHasScene;
}

float ARDiorama::BoardWidth()
{
	return WIDTH;
}

void ARDiorama::Draw(const float theModel[16], const float theView[16], const float theProj[16])
{
	if (!gTexture)
		return;
	if (!gProgram)
	{
		gProgram = glCreateProgram();
		glAttachShader(gProgram, Compile(GL_VERTEX_SHADER, VS));
		glAttachShader(gProgram, Compile(GL_FRAGMENT_SHADER, FS));
		glBindAttribLocation(gProgram, 0, "aPos");
		glBindAttribLocation(gProgram, 1, "aUv");
		glLinkProgram(gProgram);
		gMvp = glGetUniformLocation(gProgram, "uMvp");
		glUseProgram(gProgram);
		glUniform1i(glGetUniformLocation(gProgram, "uTex"), 0);
	}
	float aMvp[16], aViewModel[16];
	Mul(theView, theModel, aViewModel);
	Mul(theProj, aViewModel, aMvp);
	float w = WIDTH / 2, d = WIDTH * 0.75f / 2;
	// x, y, z, u, v: the far edge (-z) is the top of the game's picture (the texture's last rows)
	const float aQuad[] = {
		-w, 0, d, 0, 0,
		w, 0, d, 1, 0,
		-w, 0, -d, 0, 1,
		w, 0, -d, 1, 1,
	};
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(gProgram);
	glUniformMatrix4fv(gMvp, 1, GL_FALSE, aMvp);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gTexture);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_CULL_FACE);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), aQuad);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), aQuad + 3);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisable(GL_DEPTH_TEST);
}

bool ARDiorama::Pick(const float theModel[16], const float theOrigin[3], const float theDir[3], int* theGameX, int* theGameY)
{
	float aInv[16];
	InvertModel(theModel, aInv);
	float o[3], d[3];
	for (int r = 0; r < 3; r++)
	{
		o[r] = aInv[r] * theOrigin[0] + aInv[4 + r] * theOrigin[1] + aInv[8 + r] * theOrigin[2] + aInv[12 + r];
		d[r] = aInv[r] * theDir[0] + aInv[4 + r] * theDir[1] + aInv[8 + r] * theDir[2];
	}
	if (std::fabs(d[1]) < 1e-9f)
		return false;
	float t = -o[1] / d[1];
	if (t <= 0)
		return false;
	float x = o[0] + d[0] * t, z = o[2] + d[2] * t;
	float u = x / WIDTH + 0.5f, v = z / (WIDTH * 0.75f) + 0.5f;
	if (u < 0 || u > 1 || v < 0 || v > 1)
		return false;
	*theGameX = std::clamp(static_cast<int>(u * 800), 0, 799);
	*theGameY = std::clamp(static_cast<int>(v * 600), 0, 599);
	return true;
}
