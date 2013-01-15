/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/TextureOGL.h"
#include "mozilla/layers/CompositorOGL.h"
#include "mozilla/layers/ImageHost.h"
#include "mozilla/layers/ContentHost.h"
#include "mozilla/layers/CompositingRenderTargetOGL.h"
#include "mozilla/Preferences.h"
#include "mozilla/layers/ShadowLayers.h"
#include "mozilla/layers/PLayer.h"
#include "mozilla/layers/Effects.h"

#include "gfxUtils.h"

#include "GLContextProvider.h"

#include "nsIServiceManager.h"
#include "nsIConsoleService.h"

#include "gfxCrashReporterUtils.h"

#include "sampler.h"

namespace mozilla {
namespace layers {

using namespace mozilla::gl;

static inline gfx::IntSize ns2gfxSize(const nsIntSize& s) {
  return gfx::IntSize(s.width, s.height);
}

static const double kFpsWindowMs = 250.0;
static const size_t kNumFrameTimeStamps = 16;
struct FPSCounter {
  FPSCounter() : mCurrentFrameIndex(0) {}

  // We keep a circular buffer of the time points at which the last K
  // frames were drawn.  To estimate FPS, we count the number of
  // frames we've drawn within the last kFPSWindowMs milliseconds and
  // divide by the amount time since the first of those frames.
  TimeStamp mFrames[kNumFrameTimeStamps];
  size_t mCurrentFrameIndex;

  void AddFrame(TimeStamp aNewFrame) {
    mFrames[mCurrentFrameIndex] = aNewFrame;
    mCurrentFrameIndex = (mCurrentFrameIndex + 1) % kNumFrameTimeStamps;
  }

  double AddFrameAndGetFps(TimeStamp aCurrentFrame) {
    AddFrame(aCurrentFrame);
    return EstimateFps(aCurrentFrame);
  }

  double GetFpsAt(TimeStamp aNow) {
    return EstimateFps(aNow);
  }

private:
  double EstimateFps(TimeStamp aNow) {
    TimeStamp beginningOfWindow =
      (aNow - TimeDuration::FromMilliseconds(kFpsWindowMs));
    TimeStamp earliestFrameInWindow = aNow;
    size_t numFramesDrawnInWindow = 0;
    for (size_t i = 0; i < kNumFrameTimeStamps; ++i) {
      const TimeStamp& frame = mFrames[i];
      if (!frame.IsNull() && frame > beginningOfWindow) {
        ++numFramesDrawnInWindow;
        earliestFrameInWindow = NS_MIN(earliestFrameInWindow, frame);
      }
    }
    double realWindowSecs = (aNow - earliestFrameInWindow).ToSeconds();
    if (realWindowSecs == 0.0 || numFramesDrawnInWindow == 1) {
      return 0.0;
    }
    return double(numFramesDrawnInWindow - 1) / realWindowSecs;
  }
};

struct FPSState {
  GLuint mTexture;
  FPSCounter mCompositionFps;
  FPSCounter mTransactionFps;

  FPSState() : mTexture(0) { }

  void DrawFPS(TimeStamp, GLContext*, ShaderProgramOGL*);

  static void DrawFrameCounter(GLContext* context);

  void NotifyShadowTreeTransaction() {
    mTransactionFps.AddFrame(TimeStamp::Now());
  }
};


void
FPSState::DrawFPS(TimeStamp aNow,
                  GLContext* context, ShaderProgramOGL* copyprog)
{
  int fps = int(mCompositionFps.AddFrameAndGetFps(aNow));
  int txnFps = int(mTransactionFps.GetFpsAt(aNow));

  GLint viewport[4];
  context->fGetIntegerv(LOCAL_GL_VIEWPORT, viewport);

  if (!mTexture) {
    // Bind the number of textures we need, in this case one.
    context->fGenTextures(1, &mTexture);
    context->fBindTexture(LOCAL_GL_TEXTURE_2D, mTexture);
    context->fTexParameteri(LOCAL_GL_TEXTURE_2D,LOCAL_GL_TEXTURE_MIN_FILTER,LOCAL_GL_NEAREST);
    context->fTexParameteri(LOCAL_GL_TEXTURE_2D,LOCAL_GL_TEXTURE_MAG_FILTER,LOCAL_GL_NEAREST);

    unsigned char text[] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0, 255, 255, 255,   0, 255, 255,   0,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255,   0, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0,
      0, 255,   0, 255,   0,   0, 255,   0,   0,   0,   0, 255,   0,   0,   0, 255,   0, 255,   0, 255,   0, 255,   0,   0,   0, 255,   0,   0,   0,   0,   0, 255,   0, 255,   0, 255,   0, 255,   0, 255,   0,
      0, 255,   0, 255,   0,   0, 255,   0,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0,   0,   0, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0,
      0, 255,   0, 255,   0,   0, 255,   0,   0, 255,   0,   0,   0,   0,   0, 255,   0,   0,   0, 255,   0,   0,   0, 255,   0, 255,   0, 255,   0,   0,   0, 255,   0, 255,   0, 255,   0,   0,   0, 255,   0,
      0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0,   0,   0, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0,   0,   0, 255,   0, 255, 255, 255,   0,   0,   0, 255,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    };

    // convert from 8 bit to 32 bit so that don't have to write the text above out in 32 bit format
    // we rely on int being 32 bits
    unsigned int* buf = (unsigned int*)malloc(64 * 8 * 4);
    for (int i = 0; i < 7; i++) {
      for (int j = 0; j < 41; j++) {
        unsigned int purple = 0xfff000ff;
        unsigned int white  = 0xffffffff;
        buf[i * 64 + j] = (text[i * 41 + j] == 0) ? purple : white;
      }
    }
    context->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_RGBA, 64, 8, 0, LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE, buf);
    free(buf);
  }

  struct Vertex2D {
    float x,y;
  };
  const Vertex2D vertices[] = {
    { -1.0f, 1.0f - 42.f / viewport[3] },
    { -1.0f, 1.0f},
    { -1.0f + 22.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 22.f / viewport[2], 1.0f },

    {  -1.0f + 22.f / viewport[2], 1.0f - 42.f / viewport[3] },
    {  -1.0f + 22.f / viewport[2], 1.0f },
    {  -1.0f + 44.f / viewport[2], 1.0f - 42.f / viewport[3] },
    {  -1.0f + 44.f / viewport[2], 1.0f },

    { -1.0f + 44.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 44.f / viewport[2], 1.0f },
    { -1.0f + 66.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 66.f / viewport[2], 1.0f }
  };

  const Vertex2D vertices2[] = {
    { -1.0f + 80.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 80.f / viewport[2], 1.0f },
    { -1.0f + 102.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 102.f / viewport[2], 1.0f },
    
    { -1.0f + 102.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 102.f / viewport[2], 1.0f },
    { -1.0f + 124.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 124.f / viewport[2], 1.0f },
    
    { -1.0f + 124.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 124.f / viewport[2], 1.0f },
    { -1.0f + 146.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 146.f / viewport[2], 1.0f },
  };

  int v1   = fps % 10;
  int v10  = (fps % 100) / 10;
  int v100 = (fps % 1000) / 100;

  int txn1 = txnFps % 10;
  int txn10  = (txnFps % 100) / 10;
  int txn100 = (txnFps % 1000) / 100;

  // Feel free to comment these texture coordinates out and use one
  // of the ones below instead, or play around with your own values.
  const GLfloat texCoords[] = {
    (v100 * 4.f) / 64, 7.f / 8,
    (v100 * 4.f) / 64, 0.0f,
    (v100 * 4.f + 4) / 64, 7.f / 8,
    (v100 * 4.f + 4) / 64, 0.0f,

    (v10 * 4.f) / 64, 7.f / 8,
    (v10 * 4.f) / 64, 0.0f,
    (v10 * 4.f + 4) / 64, 7.f / 8,
    (v10 * 4.f + 4) / 64, 0.0f,

    (v1 * 4.f) / 64, 7.f / 8,
    (v1 * 4.f) / 64, 0.0f,
    (v1 * 4.f + 4) / 64, 7.f / 8,
    (v1 * 4.f + 4) / 64, 0.0f,
  };

  const GLfloat texCoords2[] = {
    (txn100 * 4.f) / 64, 7.f / 8,
    (txn100 * 4.f) / 64, 0.0f,
    (txn100 * 4.f + 4) / 64, 7.f / 8,
    (txn100 * 4.f + 4) / 64, 0.0f,

    (txn10 * 4.f) / 64, 7.f / 8,
    (txn10 * 4.f) / 64, 0.0f,
    (txn10 * 4.f + 4) / 64, 7.f / 8,
    (txn10 * 4.f + 4) / 64, 0.0f,

    (txn1 * 4.f) / 64, 7.f / 8,
    (txn1 * 4.f) / 64, 0.0f,
    (txn1 * 4.f + 4) / 64, 7.f / 8,
    (txn1 * 4.f + 4) / 64, 0.0f,
  };

  // Turn necessary features on
  context->fEnable(LOCAL_GL_BLEND);
  context->fBlendFunc(LOCAL_GL_ONE, LOCAL_GL_SRC_COLOR);

  context->fActiveTexture(LOCAL_GL_TEXTURE0);
  context->fBindTexture(LOCAL_GL_TEXTURE_2D, mTexture);

  copyprog->Activate();
  copyprog->SetTextureUnit(0);

  // we're going to use client-side vertex arrays for this.
  context->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);

  // "COPY"
  context->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ZERO,
                              LOCAL_GL_ONE, LOCAL_GL_ZERO);

  // enable our vertex attribs; we'll call glVertexPointer below
  // to fill with the correct data.
  GLint vcattr = copyprog->AttribLocation(ShaderProgramOGL::VertexCoordAttrib);
  GLint tcattr = copyprog->AttribLocation(ShaderProgramOGL::TexCoordAttrib);

  context->fEnableVertexAttribArray(vcattr);
  context->fEnableVertexAttribArray(tcattr);

  context->fVertexAttribPointer(vcattr,
                                2, LOCAL_GL_FLOAT,
                                LOCAL_GL_FALSE,
                                0, vertices);

  context->fVertexAttribPointer(tcattr,
                                2, LOCAL_GL_FLOAT,
                                LOCAL_GL_FALSE,
                                0, texCoords);

  context->fDrawArrays(LOCAL_GL_TRIANGLE_STRIP, 0, 12);

  context->fVertexAttribPointer(vcattr,
                                2, LOCAL_GL_FLOAT,
                                LOCAL_GL_FALSE,
                                0, vertices2);

  context->fVertexAttribPointer(tcattr,
                                2, LOCAL_GL_FLOAT,
                                LOCAL_GL_FALSE,
                                0, texCoords2);

  context->fDrawArrays(LOCAL_GL_TRIANGLE_STRIP, 0, 12);
}






#ifdef CHECK_CURRENT_PROGRAM
int ShaderProgramOGL::sCurrentProgramKey = 0;
#endif

CompositorOGL::CompositorOGL(nsIWidget *aWidget, int aSurfaceWidth,
                             int aSurfaceHeight, bool aIsRenderingToEGLSurface)
  : mWidget(aWidget)
  , mWidgetSize(-1, -1)
  , mSurfaceSize(aSurfaceWidth, aSurfaceHeight)
  , mBoundFBO(0)
  , mHasBGRA(0)
  , mIsRenderingToEGLSurface(aIsRenderingToEGLSurface)
  , mFrameInProgress(false)
  , mDestroyed(false)
{
}

CompositorOGL::~CompositorOGL()
{
  Destroy();
}

already_AddRefed<mozilla::gl::GLContext>
CompositorOGL::CreateContext()
{
  nsRefPtr<GLContext> context;

#ifdef XP_WIN
  if (PR_GetEnv("MOZ_LAYERS_PREFER_EGL")) {
    printf_stderr("Trying GL layers...\n");
    context = gl::GLContextProviderEGL::CreateForWindow(mWidget);
  }
#endif

  if (!context)
    context = gl::GLContextProvider::CreateForWindow(mWidget);

  if (!context) {
    NS_WARNING("Failed to create CompositorOGL context");
  }
  return context.forget();
}

void
CompositorOGL::AddPrograms(ShaderProgramType aType)
{
  for (PRUint32 maskType = MaskNone; maskType < NumMaskTypes; ++maskType) {
    if (ProgramProfileOGL::ProgramExists(aType, static_cast<MaskType>(maskType))) {
      mPrograms[aType].mVariations[maskType] = new ShaderProgramOGL(this->gl(),
        ProgramProfileOGL::GetProfileFor(aType, static_cast<MaskType>(maskType)));
    } else {
      mPrograms[aType].mVariations[maskType] = nullptr;
    }
  }
}

void
CompositorOGL::Destroy()
{
  if (!mDestroyed) {
    mDestroyed = true;
    CleanupResources();
  }
}

void
CompositorOGL::CleanupResources()
{
  if (!mGLContext)
    return;

  nsRefPtr<GLContext> ctx = mGLContext->GetSharedContext();
  if (!ctx) {
    ctx = mGLContext;
  }

  ctx->MakeCurrent();

  for (PRUint32 i = 0; i < mPrograms.Length(); ++i) {
    for (PRUint32 type = MaskNone; type < NumMaskTypes; ++type) {
      delete mPrograms[i].mVariations[type];
    }
  }
  mPrograms.Clear();

  ctx->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);

  if (mQuadVBO) {
    ctx->fDeleteBuffers(1, &mQuadVBO);
    mQuadVBO = 0;
  }

  mGLContext = nullptr;
}

bool
CompositorOGL::Initialize(bool force, nsRefPtr<GLContext> aContext)
{
  ScopedGfxFeatureReporter reporter("GL Layers", true);

  // Do not allow double initialization
  NS_ABORT_IF_FALSE(mGLContext == nullptr, "Don't reinitialize CompositorOGL");

  if (aContext) {
    mGLContext = aContext;
  } else {
    mGLContext = CreateContext();
  }

#ifdef MOZ_WIDGET_ANDROID
  if (!mGLContext)
    NS_RUNTIMEABORT("We need a context on Android");
#endif

  if (!mGLContext)
    return false;

  mGLContext->SetFlipped(true);

  MakeCurrent();

  mHasBGRA =
    mGLContext->IsExtensionSupported(gl::GLContext::EXT_texture_format_BGRA8888) ||
    mGLContext->IsExtensionSupported(gl::GLContext::EXT_bgra);

  mGLContext->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                 LOCAL_GL_ONE, LOCAL_GL_ONE);
  mGLContext->fEnable(LOCAL_GL_BLEND);

  mPrograms.AppendElements(NumProgramTypes);
  for (int type = 0; type < NumProgramTypes; ++type) {
    AddPrograms(static_cast<ShaderProgramType>(type));
  }

  // initialise a common shader to check that we can actually compile a shader
  if (!mPrograms[gl::RGBALayerProgramType].mVariations[MaskNone]->Initialize()) {
    return false;
  }

  if (mGLContext->WorkAroundDriverBugs()) {
    /**
    * We'll test the ability here to bind NPOT textures to a framebuffer, if
    * this fails we'll try ARB_texture_rectangle.
    */

    GLenum textureTargets[] = {
      LOCAL_GL_TEXTURE_2D,
      LOCAL_GL_NONE
    };

    if (mGLContext->IsGLES2()) {
        textureTargets[1] = LOCAL_GL_TEXTURE_RECTANGLE_ARB;
    }

    mFBOTextureTarget = LOCAL_GL_NONE;

    GLuint testFBO = 0;
    mGLContext->fGenFramebuffers(1, &testFBO);
    GLuint testTexture = 0;

    for (PRUint32 i = 0; i < ArrayLength(textureTargets); i++) {
      GLenum target = textureTargets[i];
      if (!target)
          continue;

      mGLContext->fGenTextures(1, &testTexture);
      mGLContext->fBindTexture(target, testTexture);
      mGLContext->fTexParameteri(target,
                                LOCAL_GL_TEXTURE_MIN_FILTER,
                                LOCAL_GL_NEAREST);
      mGLContext->fTexParameteri(target,
                                LOCAL_GL_TEXTURE_MAG_FILTER,
                                LOCAL_GL_NEAREST);
      mGLContext->fTexImage2D(target,
                              0,
                              LOCAL_GL_RGBA,
                              5, 3, /* sufficiently NPOT */
                              0,
                              LOCAL_GL_RGBA,
                              LOCAL_GL_UNSIGNED_BYTE,
                              NULL);

      // unbind this texture, in preparation for binding it to the FBO
      mGLContext->fBindTexture(target, 0);

      mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, testFBO);
      mGLContext->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                                        LOCAL_GL_COLOR_ATTACHMENT0,
                                        target,
                                        testTexture,
                                        0);

      if (mGLContext->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER) ==
          LOCAL_GL_FRAMEBUFFER_COMPLETE)
      {
        mFBOTextureTarget = target;
        mGLContext->fDeleteTextures(1, &testTexture);
        break;
      }

      mGLContext->fDeleteTextures(1, &testTexture);
    }

    if (testFBO) {
      mGLContext->fDeleteFramebuffers(1, &testFBO);
    }

    if (mFBOTextureTarget == LOCAL_GL_NONE) {
      /* Unable to find a texture target that works with FBOs and NPOT textures */
      return false;
    }
  } else {
    // not trying to work around driver bugs, so TEXTURE_2D should just work
    mFBOTextureTarget = LOCAL_GL_TEXTURE_2D;
  }

  // back to default framebuffer, to avoid confusion
  mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);

  if (mFBOTextureTarget == LOCAL_GL_TEXTURE_RECTANGLE_ARB) {
    /* If we're using TEXTURE_RECTANGLE, then we must have the ARB
     * extension -- the EXT variant does not provide support for
     * texture rectangle access inside GLSL (sampler2DRect,
     * texture2DRect).
     */
    if (!mGLContext->IsExtensionSupported(gl::GLContext::ARB_texture_rectangle))
      return false;
  }

  /* Create a simple quad VBO */

  mGLContext->fGenBuffers(1, &mQuadVBO);
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mQuadVBO);

  GLfloat vertices[] = {
    /* First quad vertices */
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
    /* Then quad texcoords */
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
    /* Then flipped quad texcoords */
    0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
  };
  mGLContext->fBufferData(LOCAL_GL_ARRAY_BUFFER, sizeof(vertices), vertices, LOCAL_GL_STATIC_DRAW);
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);

  nsCOMPtr<nsIConsoleService>
    console(do_GetService(NS_CONSOLESERVICE_CONTRACTID));

  if (console) {
    nsString msg;
    msg +=
      NS_LITERAL_STRING("OpenGL LayerManager Initialized Succesfully.\nVersion: ");
    msg += NS_ConvertUTF8toUTF16(
      nsDependentCString((const char*)mGLContext->fGetString(LOCAL_GL_VERSION)));
    msg += NS_LITERAL_STRING("\nVendor: ");
    msg += NS_ConvertUTF8toUTF16(
      nsDependentCString((const char*)mGLContext->fGetString(LOCAL_GL_VENDOR)));
    msg += NS_LITERAL_STRING("\nRenderer: ");
    msg += NS_ConvertUTF8toUTF16(
      nsDependentCString((const char*)mGLContext->fGetString(LOCAL_GL_RENDERER)));
    msg += NS_LITERAL_STRING("\nFBO Texture Target: ");
    if (mFBOTextureTarget == LOCAL_GL_TEXTURE_2D)
      msg += NS_LITERAL_STRING("TEXTURE_2D");
    else
      msg += NS_LITERAL_STRING("TEXTURE_RECTANGLE");
    console->LogStringMessage(msg.get());
  }

  if (NS_IsMainThread()) {
    Preferences::AddBoolVarCache(&sDrawFPS, "layers.acceleration.draw-fps");
    Preferences::AddBoolVarCache(&sFrameCounter, "layers.acceleration.frame-counter");
  } else {
    // We have to dispatch an event to the main thread to read the pref.
    class ReadDrawFPSPref : public nsRunnable {
    public:
      NS_IMETHOD Run()
      {
        Preferences::AddBoolVarCache(&sDrawFPS, "layers.acceleration.draw-fps");
        Preferences::AddBoolVarCache(&sFrameCounter, "layers.acceleration.frame-counter");
        return NS_OK;
      }
    };
    NS_DispatchToMainThread(new ReadDrawFPSPref());
  }

  reporter.SetSuccessful();
  return true;
}

// |aTexCoordRect| is the rectangle from the texture that we want to
// draw using the given program.  The program already has a necessary
// offset and scale, so the geometry that needs to be drawn is a unit
// square from 0,0 to 1,1.
//
// |aTexSize| is the actual size of the texture, as it can be larger
// than the rectangle given by |aTexCoordRect|.
void 
CompositorOGL::BindAndDrawQuadWithTextureRect(ShaderProgramOGL *aProg,
                                              const gfx::IntRect& aTexCoordRect,
                                              const gfx::IntSize& aTexSize,
                                              GLenum aWrapMode /* = LOCAL_GL_REPEAT */,
                                              bool aFlipped /* = false */)
{
  NS_ASSERTION(aProg->HasInitialized(), "Shader program not correctly initialized");
  GLuint vertAttribIndex =
    aProg->AttribLocation(ShaderProgramOGL::VertexCoordAttrib);
  GLuint texCoordAttribIndex =
    aProg->AttribLocation(ShaderProgramOGL::TexCoordAttrib);
  NS_ASSERTION(texCoordAttribIndex != GLuint(-1), "no texture coords?");

  // clear any bound VBO so that glVertexAttribPointer() goes back to
  // "pointer mode"
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);

  // Given what we know about these textures and coordinates, we can
  // compute fmod(t, 1.0f) to get the same texture coordinate out.  If
  // the texCoordRect dimension is < 0 or > width/height, then we have
  // wraparound that we need to deal with by drawing multiple quads,
  // because we can't rely on full non-power-of-two texture support
  // (which is required for the REPEAT wrap mode).

  GLContext::RectTriangles rects;

  gfx::IntSize realTexSize = aTexSize;
  if (!mGLContext->CanUploadNonPowerOfTwo()) {
    realTexSize = gfx::IntSize(gfx::NextPowerOfTwo(aTexSize.width),
                               gfx::NextPowerOfTwo(aTexSize.height));
  }

  if (aWrapMode == LOCAL_GL_REPEAT) {
    rects.addRect(/* dest rectangle */
                  0.0f, 0.0f, 1.0f, 1.0f,
                  /* tex coords */
                  aTexCoordRect.x / GLfloat(realTexSize.width),
                  aTexCoordRect.y / GLfloat(realTexSize.height),
                  aTexCoordRect.XMost() / GLfloat(realTexSize.width),
                  aTexCoordRect.YMost() / GLfloat(realTexSize.height),
                  aFlipped);
  } else {
    nsIntRect tcRect(aTexCoordRect.x, aTexCoordRect.y,
                     aTexCoordRect.width, aTexCoordRect.height);
    GLContext::DecomposeIntoNoRepeatTriangles(tcRect,
                                              nsIntSize(realTexSize.width, realTexSize.height),
                                              rects, aFlipped);
  }

  mGLContext->fVertexAttribPointer(vertAttribIndex, 2,
                                   LOCAL_GL_FLOAT, LOCAL_GL_FALSE, 0,
                                   rects.vertexPointer());

  mGLContext->fVertexAttribPointer(texCoordAttribIndex, 2,
                                   LOCAL_GL_FLOAT, LOCAL_GL_FALSE, 0,
                                   rects.texCoordPointer());

  {
    mGLContext->fEnableVertexAttribArray(texCoordAttribIndex);
    {
      mGLContext->fEnableVertexAttribArray(vertAttribIndex);

      mGLContext->fDrawArrays(LOCAL_GL_TRIANGLES, 0, rects.elements());

      mGLContext->fDisableVertexAttribArray(vertAttribIndex);
    }
    mGLContext->fDisableVertexAttribArray(texCoordAttribIndex);
  }
}

void
CompositorOGL::PrepareViewport(int aWidth, int aHeight, const gfxMatrix& aWorldTransform)
{
  // Set the viewport correctly.
  mGLContext->fViewport(0, 0, aWidth, aHeight);

  // We flip the view matrix around so that everything is right-side up; we're
  // drawing directly into the window's back buffer, so this keeps things
  // looking correct.
  // XXX: We keep track of whether the window size changed, so we could skip
  // this update if it hadn't changed since the last call. We will need to
  // track changes to aTransformPolicy and aWorldTransform for this to work
  // though.

  // Matrix to transform (0, 0, aWidth, aHeight) to viewport space (-1.0, 1.0,
  // 2, 2) and flip the contents.
  gfxMatrix viewMatrix;
  viewMatrix.Translate(-gfxPoint(1.0, -1.0));
  viewMatrix.Scale(2.0f / float(aWidth), 2.0f / float(aHeight));
  viewMatrix.Scale(1.0f, -1.0f);

  viewMatrix = aWorldTransform * viewMatrix;

  gfx3DMatrix matrix3d = gfx3DMatrix::From2D(viewMatrix);
  matrix3d._33 = 0.0f;

  SetLayerProgramProjectionMatrix(matrix3d);
}

void
CompositorOGL::SetLayerProgramProjectionMatrix(const gfx3DMatrix& aMatrix)
{
  for (unsigned int i = 0; i < mPrograms.Length(); ++i) {
    for (PRUint32 mask = MaskNone; mask < NumMaskTypes; ++mask) {
      if (mPrograms[i].mVariations[mask]) {
        mPrograms[i].mVariations[mask]->CheckAndSetProjectionMatrix(aMatrix);
      }
    }
  }
}

TemporaryRef<CompositableHost>
CompositorOGL::CreateCompositableHost(BufferType aType)
{
  RefPtr<CompositableHost> result;
  switch (aType) {
  case BUFFER_YCBCR:
    result = new YCbCrImageHost(this);
    return result.forget();
#ifdef MOZ_WIDGET_GONK
  case BUFFER_DIRECT_EXTERNAL:
#endif
  case BUFFER_SHARED:
  case BUFFER_TEXTURE:
  case BUFFER_DIRECT: //TODO[nrc] fuck up - should be using Texture id and we used buffer id :-(
    result = new ImageHostSingle(this, aType);
    return result.forget();
  case BUFFER_BRIDGE:
    result = new ImageHostBridge(this);
    return result.forget();
  case BUFFER_CONTENT:
    result = new ContentHostTexture(this);
    return result.forget();
  case BUFFER_CONTENT_DIRECT:
    result = new ContentHostDirect(this);
    return result.forget();
  default:
    NS_ERROR("Unknown BufferType");
    return nullptr;
  }
}

void
CompositorOGL::FallbackTextureInfo(TextureInfo& aId)
{
  //TODO[nrc] change this when you fix the fuck up
  if (aId.imageType == BUFFER_DIRECT) {
    aId.imageType = BUFFER_TEXTURE;
  }
}

TemporaryRef<TextureHost>
CompositorOGL::CreateTextureHost(BufferType aImageType,
                                 TextureHostType aMemoryType,
                                 uint32_t aTextureFlags,
                                 ISurfaceDeallocator* aDeAllocator)
{
  RefPtr<TextureHost> result = nullptr;
  switch (aMemoryType) {
  case TEXTURE_SHARED:
    result = new TextureHostOGLShared(mGLContext);
    break;
  case TEXTURE_SHARED_BUFFERED:
    result = new TextureHostOGLShared(mGLContext, BUFFER_BUFFERED, aDeAllocator);
    break;
  case TEXTURE_SHMEM_YCBCR:
    result = new YCbCrTextureHostOGL(mGLContext);
    break;
  case TEXTURE_SHMEM:
    //TODO[nrc] fuck up
    if (aImageType == BUFFER_YCBCR) {
      result = new YCbCrTextureHostOGL(mGLContext);
    } else if (aImageType == BUFFER_CONTENT_DIRECT) {
      //TODO[nrc] should probably use the below path with fallback, but check
      result = new TextureImageAsTextureHostOGL(mGLContext, nullptr, BUFFER_BUFFERED, aDeAllocator);
    } else if (aImageType == BUFFER_DIRECT) {
      if (ShadowLayerManager::SupportsDirectTexturing()) {
        result = new TextureImageAsTextureHostOGL(mGLContext, nullptr, BUFFER_BUFFERED, aDeAllocator);
      } else {
        result = new TextureImageAsTextureHostOGL(mGLContext, nullptr, BUFFER_NONE);
      }
#ifdef MOZ_WIDGET_GONK
    } else if (aImageType == BUFFER_DIRECT_EXTERNAL) {
      result = new DirectExternalTextureHost(mGLContext);
#endif
    } else {
      result = new TextureImageAsTextureHostOGL(mGLContext, nullptr, BUFFER_NONE);
    }
    break;
  case TEXTURE_UNKNOWN:
  default:
    NS_WARNING("Unknown texture type");
    return nullptr;
  }

  NS_ASSERTION(result, "Result should have been created.");
 
  result->SetFlags(aTextureFlags );
  return result.forget();
}

TemporaryRef<CompositingRenderTarget>
CompositorOGL::CreateRenderTarget(const gfx::IntRect &aRect, SurfaceInitMode aInit)
{
  GLuint tex = 0;
  GLuint fbo = 0;
  CreateFBOWithTexture(aRect, aInit, 0, &fbo, &tex);
  RefPtr<CompositingRenderTargetOGL> rt = new CompositingRenderTargetOGL(mGLContext, tex, fbo);
  return rt.forget();
}

TemporaryRef<CompositingRenderTarget>
CompositorOGL::CreateRenderTargetFromSource(const gfx::IntRect &aRect,
                                            const CompositingRenderTarget *aSource)
{
  GLuint tex = 0;
  GLuint fbo = 0;
  const CompositingRenderTargetOGL* sourceSurface
    = static_cast<const CompositingRenderTargetOGL*>(aSource);
  if (aSource) {
    CreateFBOWithTexture(aRect, INIT_MODE_COPY, sourceSurface->GetFBO(),
                         &fbo, &tex);
  } else {
    CreateFBOWithTexture(aRect, INIT_MODE_COPY, 0,
                         &fbo, &tex);
  }

  RefPtr<CompositingRenderTargetOGL> surface
    = new CompositingRenderTargetOGL(mGLContext, tex, fbo);
  return surface.forget();
}

void
CompositorOGL::SetRenderTarget(CompositingRenderTarget *aSurface)
{
  if (aSurface) {
    CompositingRenderTargetOGL* surface
      = static_cast<CompositingRenderTargetOGL*>(aSurface);
    if (mBoundFBO != surface->mFBO) {
      mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, surface->mFBO);
      mBoundFBO = surface->mFBO;
    }
  } else if (mBoundFBO != 0) {
    mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);
    mBoundFBO = 0;
  }
}

static GLenum
GetFrameBufferInternalFormat(GLContext* gl,
                             GLuint aFrameBuffer,
                             nsIWidget* aWidget)
{
  if (aFrameBuffer == 0) { // default framebuffer
    return aWidget->GetGLFrameBufferFormat();
  }
  return LOCAL_GL_RGBA;
}

bool CompositorOGL::sDrawFPS = false;
bool CompositorOGL::sFrameCounter = false;

static uint16_t sFrameCount = 0;
void
FPSState::DrawFrameCounter(GLContext* context)
{
  SAMPLER_FRAME_NUMBER(sFrameCount);

  context->fEnable(LOCAL_GL_SCISSOR_TEST);

  uint16_t frameNumber = sFrameCount;
  for (size_t i = 0; i < 16; i++) {
    context->fScissor(3*i, 0, 3, 3);

    // We should do this using a single draw call
    // instead of 16 glClear()
    if ((frameNumber >> i) & 0x1) {
      context->fClearColor(0.0, 0.0, 0.0, 0.0);
    } else {
      context->fClearColor(1.0, 1.0, 1.0, 0.0);
    }
    context->fClear(LOCAL_GL_COLOR_BUFFER_BIT);
  }
  // We intentionally overflow at 2^16.
  sFrameCount++;
}

/*
 * Returns a size that is larger than and closest to aSize where both
 * width and height are powers of two.
 * If the OpenGL setup is capable of using non-POT textures, then it
 * will just return aSize.
 */
static gfx::IntSize
CalculatePOTSize(const gfx::IntSize& aSize, GLContext* gl)
{
  if (gl->CanUploadNonPowerOfTwo())
    return aSize;

  return gfx::IntSize(gfx::NextPowerOfTwo(aSize.width), gfx::NextPowerOfTwo(aSize.height));
}

void
CompositorOGL::BeginFrame(const gfx::Rect *aClipRectIn, const gfxMatrix& aTransform,
                          gfx::Rect *aClipRectOut)
{
  if (mFrameInProgress) {
    EndFrame(aTransform);
  }
  mFrameInProgress = true;
  gfxRect rect;
  if (mIsRenderingToEGLSurface) {
    rect = gfxRect(0, 0, mSurfaceSize.width, mSurfaceSize.height);
  } else {
    nsIntRect intRect;
    // FIXME/bug XXXXXX this races with rotation changes on the main
    // thread, and undoes all the care we take with layers txns being
    // sent atomically with rotation changes
    mWidget->GetClientBounds(intRect);
    rect = gfxRect(intRect);
  }

  rect = aTransform.TransformBounds(rect);

  GLint width = rect.width;
  GLint height = rect.height;

  // We can't draw anything to something with no area
  // so just return
  if (width == 0 || height == 0)
    return;

  // If the widget size changed, we have to force a MakeCurrent
  // to make sure that GL sees the updated widget size.
  if (mWidgetSize.width != width ||
      mWidgetSize.height != height)
  {
    MakeCurrent(true);

    mWidgetSize.width = width;
    mWidgetSize.height = height;
  } else {
    MakeCurrent();
  }

#if MOZ_ANDROID_OMTC
  TexturePoolOGL::Fill(gl());
#endif

  mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);
  PrepareViewport(width, height, aTransform);

  // Default blend function implements "OVER"
  mGLContext->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                 LOCAL_GL_ONE, LOCAL_GL_ONE);
  mGLContext->fEnable(LOCAL_GL_BLEND);

  if (!aClipRectIn) {
    mGLContext->fScissor(0, 0, width, height);
    if (aClipRectOut) {
      aClipRectOut->SetRect(0, 0, width, height);
    }
  } else {
    mGLContext->fScissor(aClipRectIn->x, aClipRectIn->y, aClipRectIn->width, aClipRectIn->height);
  }

  mGLContext->fEnable(LOCAL_GL_SCISSOR_TEST);

  // If the Android compositor is being used, this clear will be done in
  // DrawWindowUnderlay. Make sure the bits used here match up with those used
  // in mobile/android/base/gfx/LayerRenderer.java
#ifndef MOZ_ANDROID_OMTC
  mGLContext->fClearColor(0.0, 0.0, 0.0, 0.0);
  mGLContext->fClear(LOCAL_GL_COLOR_BUFFER_BIT | LOCAL_GL_DEPTH_BUFFER_BIT);
#endif

  // Allow widget to render a custom background.
  mWidget->DrawWindowUnderlay();
}

void
CompositorOGL::CreateFBOWithTexture(const gfx::IntRect& aRect, SurfaceInitMode aInit,
                                    GLuint aSourceFrameBuffer,
                                    GLuint *aFBO, GLuint *aTexture)
{
  GLuint tex, fbo;

  mGLContext->fActiveTexture(LOCAL_GL_TEXTURE0);
  mGLContext->fGenTextures(1, &tex);
  mGLContext->fBindTexture(mFBOTextureTarget, tex);

  if (aInit == INIT_MODE_COPY) {

    if (mBoundFBO != aSourceFrameBuffer) {
      mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, aSourceFrameBuffer);
    }

    // We're going to create an RGBA temporary fbo.  But to
    // CopyTexImage() from the current framebuffer, the framebuffer's
    // format has to be compatible with the new texture's.  So we
    // check the format of the framebuffer here and take a slow path
    // if it's incompatible.
    GLenum format =
      GetFrameBufferInternalFormat(gl(), aSourceFrameBuffer, mWidget);

    bool isFormatCompatibleWithRGBA
        = gl()->IsGLES2() ? (format == LOCAL_GL_RGBA)
                          : true;

    if (isFormatCompatibleWithRGBA) {
      mGLContext->fCopyTexImage2D(mFBOTextureTarget,
                                  0,
                                  LOCAL_GL_RGBA,
                                  aRect.x, aRect.y,
                                  aRect.width, aRect.height,
                                  0);
    } else {
      // Curses, incompatible formats.  Take a slow path.

      // RGBA
      size_t bufferSize = aRect.width * aRect.height * 4;
      nsAutoArrayPtr<uint8_t> buf(new uint8_t[bufferSize]);

      mGLContext->fReadPixels(aRect.x, aRect.y,
                              aRect.width, aRect.height,
                              LOCAL_GL_RGBA,
                              LOCAL_GL_UNSIGNED_BYTE,
                              buf);
      mGLContext->fTexImage2D(mFBOTextureTarget,
                              0,
                              LOCAL_GL_RGBA,
                              aRect.width, aRect.height,
                              0,
                              LOCAL_GL_RGBA,
                              LOCAL_GL_UNSIGNED_BYTE,
                              buf);
    }
  } else {
    mGLContext->fTexImage2D(mFBOTextureTarget,
                            0,
                            LOCAL_GL_RGBA,
                            aRect.width, aRect.height,
                            0,
                            LOCAL_GL_RGBA,
                            LOCAL_GL_UNSIGNED_BYTE,
                            NULL);
  }
  mGLContext->fTexParameteri(mFBOTextureTarget, LOCAL_GL_TEXTURE_MIN_FILTER,
                             LOCAL_GL_LINEAR);
  mGLContext->fTexParameteri(mFBOTextureTarget, LOCAL_GL_TEXTURE_MAG_FILTER,
                             LOCAL_GL_LINEAR);
  mGLContext->fTexParameteri(mFBOTextureTarget, LOCAL_GL_TEXTURE_WRAP_S, 
                             LOCAL_GL_CLAMP_TO_EDGE);
  mGLContext->fTexParameteri(mFBOTextureTarget, LOCAL_GL_TEXTURE_WRAP_T, 
                             LOCAL_GL_CLAMP_TO_EDGE);
  mGLContext->fBindTexture(mFBOTextureTarget, 0);

  mGLContext->fGenFramebuffers(1, &fbo);
  mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, fbo);
  mGLContext->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                                    LOCAL_GL_COLOR_ATTACHMENT0,
                                    mFBOTextureTarget,
                                    tex,
                                    0);

  // Making this call to fCheckFramebufferStatus prevents a crash on
  // PowerVR. See bug 695246.
  GLenum result = mGLContext->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);
  if (result != LOCAL_GL_FRAMEBUFFER_COMPLETE) {
    nsAutoCString msg;
    msg.Append("Framebuffer not complete -- error 0x");
    msg.AppendInt(result, 16);
    msg.Append(", mFBOTextureTarget 0x");
    msg.AppendInt(mFBOTextureTarget, 16);
    msg.Append(", aRect.width ");
    msg.AppendInt(aRect.width);
    msg.Append(", aRect.height ");
    msg.AppendInt(aRect.height);
    NS_RUNTIMEABORT(msg.get());
  }

  PrepareViewport(aRect.width, aRect.height, gfxMatrix());
  mGLContext->fScissor(0, 0, aRect.width, aRect.height);

  if (aInit == INIT_MODE_CLEAR) {
    mGLContext->fClearColor(0.0, 0.0, 0.0, 0.0);
    mGLContext->fClear(LOCAL_GL_COLOR_BUFFER_BIT);
  }

  mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mBoundFBO);

  *aFBO = fbo;
  *aTexture = tex;
}


void
CompositorOGL::DrawQuad(const gfx::Rect &aRect, const gfx::Rect *aSourceRect,
                        const gfx::Rect *aTextureRect, const gfx::Rect *aClipRect,
                        const EffectChain &aEffectChain,
                        gfx::Float aOpacity, const gfx::Matrix4x4 &aTransform,
                        const gfx::Point &aOffset)
{
  if (!mFrameInProgress) {
    BeginFrame(aClipRect, gfxMatrix());
  }

  gfx::IntRect intSourceRect;
  if (aSourceRect) {
    aSourceRect->ToIntRect(&intSourceRect);
  }

  gfx::IntRect intTextureRect;
  if (aTextureRect) {
    aTextureRect->ToIntRect(&intTextureRect);
  } else {
    aRect.ToIntRect(&intTextureRect);
  }

  gfx::IntRect intClipRect;
  if (aClipRect) {
    aClipRect->ToIntRect(&intClipRect);
    mGLContext->fScissor(intClipRect.x, intClipRect.y,
                         intClipRect.width, intClipRect.height);
  }

  MaskType maskType;
  EffectMask* effectMask;
  TextureSourceOGL* sourceMask;
  gfx::Matrix4x4 maskQuadTransform;
  if (aEffectChain.mEffects[EFFECT_MASK]) {
    effectMask = static_cast<EffectMask*>(aEffectChain.mEffects[EFFECT_MASK].get());
    sourceMask = effectMask->mMaskTexture->AsSourceOGL();

    // NS_ASSERTION(textureMask->IsAlpha(),
    //              "OpenGL mask layers must be backed by alpha surfaces");

    // We're assuming that the gl backend won't cheat and use NPOT
    // textures when glContext says it can't (which seems to happen
    // on a mac when you force POT textures)
    gfx::IntSize maskSize = CalculatePOTSize(effectMask->mSize, mGLContext);

    const gfx::Matrix4x4& maskTransform = effectMask->mMaskTransform;
    NS_ASSERTION(maskTransform.Is2D(), "How did we end up with a 3D transform here?!");
    gfx::Rect bounds = gfx::Rect(gfx::Point(), gfx::Size(maskSize));
    bounds = maskTransform.As2D().TransformBounds(bounds);

    maskQuadTransform._11 = 1.0f/bounds.width;
    maskQuadTransform._22 = 1.0f/bounds.height;
    maskQuadTransform._41 = float(-bounds.x)/bounds.width;
    maskQuadTransform._42 = float(-bounds.y)/bounds.height;

    maskType = effectMask->mIs3D
                 ? Mask3d
                 : Mask2d;
  } else {
    maskType = MaskNone;
  }

  if (aEffectChain.mEffects[EFFECT_SOLID_COLOR]) {
    EffectSolidColor* effectSolidColor =
      static_cast<EffectSolidColor*>(aEffectChain.mEffects[EFFECT_SOLID_COLOR].get());

    gfx::Color color = effectSolidColor->mColor;

    /* Multiply color by the layer opacity, as the shader
     * ignores layer opacity and expects a final color to
     * write to the color buffer.  This saves a needless
     * multiply in the fragment shader.
     */
    gfx::Float opacity = aOpacity * color.a;
    color.r *= opacity;
    color.g *= opacity;
    color.b *= opacity;
    color.a = opacity;
    ShaderProgramOGL *program = GetProgram(gl::ColorLayerProgramType, maskType);
    program->Activate();
    program->SetLayerQuadRect(aRect);
    program->SetRenderColor(effectSolidColor->mColor);
    program->SetLayerTransform(aTransform);
    program->SetRenderOffset(aOffset.x, aOffset.y);
    if (maskType != MaskNone) {
      sourceMask->BindTexture(LOCAL_GL_TEXTURE0);
      program->SetMaskTextureUnit(0);
      program->SetMaskLayerTransform(maskQuadTransform);
    }
    BindAndDrawQuad(program);

  } else if (aEffectChain.mEffects[EFFECT_BGRA] || aEffectChain.mEffects[EFFECT_BGRX]) {
    TextureSourceOGL* source;
    bool premultiplied;
    gfxPattern::GraphicsFilter filter;
    ShaderProgramOGL *program;
    bool flipped;

    if (aEffectChain.mEffects[EFFECT_BGRA]) {
      EffectBGRA* effectBGRA =
        static_cast<EffectBGRA*>(aEffectChain.mEffects[EFFECT_BGRA].get());
      source = effectBGRA->mBGRATexture->AsSourceOGL();
      premultiplied = effectBGRA->mPremultiplied;
      flipped = effectBGRA->mFlipped;
      filter = gfx::ThebesFilter(effectBGRA->mFilter);
      program = GetProgram(gl::BGRALayerProgramType, maskType);
    } else if (aEffectChain.mEffects[EFFECT_BGRX]) {
      EffectBGRX* effectBGRX =
        static_cast<EffectBGRX*>(aEffectChain.mEffects[EFFECT_BGRX].get());
      source = effectBGRX->mBGRXTexture->AsSourceOGL();
      premultiplied = effectBGRX->mPremultiplied;
      flipped = effectBGRX->mFlipped;
      filter = gfx::ThebesFilter(effectBGRX->mFilter);
      program = GetProgram(gl::BGRXLayerProgramType, maskType);
    }

    if (!premultiplied) {
      mGLContext->fBlendFuncSeparate(LOCAL_GL_SRC_ALPHA, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                     LOCAL_GL_ONE, LOCAL_GL_ONE);
    }

    source->BindTexture(LOCAL_GL_TEXTURE0);
    mGLContext->ApplyFilterToBoundTexture(filter);

    program->Activate();
    program->SetTextureUnit(0);
    program->SetLayerOpacity(aOpacity);
    program->SetLayerTransform(aTransform);
    program->SetRenderOffset(aOffset.x, aOffset.y);
    program->SetLayerQuadRect(aRect);
    if (maskType != MaskNone) {
      mGLContext->fActiveTexture(LOCAL_GL_TEXTURE1);
      sourceMask->BindTexture(LOCAL_GL_TEXTURE1);
      program->SetMaskTextureUnit(1);
      program->SetMaskLayerTransform(maskQuadTransform);
    }

    BindAndDrawQuadWithTextureRect(program, intSourceRect, intTextureRect.Size(),
                                   source->GetWrapMode(), flipped);

    if (!premultiplied) {
      mGLContext->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                     LOCAL_GL_ONE, LOCAL_GL_ONE);
    }

  } else if (aEffectChain.mEffects[EFFECT_RGB]) {
    TextureSourceOGL* source;
    bool premultiplied;
    gfxPattern::GraphicsFilter filter;
    ShaderProgramOGL *program;
    bool flipped;

    EffectRGB* effectRGB =
      static_cast<EffectRGB*>(aEffectChain.mEffects[EFFECT_RGB].get());
    source = effectRGB->mRGBTexture->AsSourceOGL();
    premultiplied = effectRGB->mPremultiplied;
    flipped = effectRGB->mFlipped;
    filter = gfx::ThebesFilter(effectRGB->mFilter);
    program = GetProgram(gl::RGBALayerProgramType, maskType);

    if (!premultiplied) {
      mGLContext->fBlendFuncSeparate(LOCAL_GL_SRC_ALPHA, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                     LOCAL_GL_ONE, LOCAL_GL_ONE);
    }

    source->BindTexture(LOCAL_GL_TEXTURE0);
    
    mGLContext->ApplyFilterToBoundTexture(filter);

    program->Activate();
    program->SetTextureUnit(0);
    program->SetLayerOpacity(aOpacity);
    program->SetLayerTransform(aTransform);
    program->SetRenderOffset(aOffset.x, aOffset.y);
    program->SetLayerQuadRect(aRect);
    if (maskType != MaskNone) {
      mGLContext->fActiveTexture(LOCAL_GL_TEXTURE1);
      sourceMask->BindTexture(LOCAL_GL_TEXTURE1);
      program->SetMaskTextureUnit(1);
      program->SetMaskLayerTransform(maskQuadTransform);
    }

    BindAndDrawQuadWithTextureRect(program, intSourceRect, intTextureRect.Size(),
                                   source->GetWrapMode(), flipped);

    if (!premultiplied) {
      mGLContext->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                     LOCAL_GL_ONE, LOCAL_GL_ONE);
    }

  } else if (aEffectChain.mEffects[EFFECT_RGBA] || aEffectChain.mEffects[EFFECT_RGBX] ||
             aEffectChain.mEffects[EFFECT_RGBA_EXTERNAL]) {
    TextureSourceOGL* source;
    bool premultiplied;
    gfxPattern::GraphicsFilter filter;
    ShaderProgramOGL *program;
    bool flipped;

    if (aEffectChain.mEffects[EFFECT_RGBA]) {
      EffectRGBA* effectRGBA =
        static_cast<EffectRGBA*>(aEffectChain.mEffects[EFFECT_RGBA].get());
      source = effectRGBA->mRGBATexture->AsSourceOGL();
      premultiplied = effectRGBA->mPremultiplied;
      flipped = effectRGBA->mFlipped;
      filter = gfx::ThebesFilter(effectRGBA->mFilter);
      program = GetProgram(gl::RGBALayerProgramType, maskType);
    } else if (aEffectChain.mEffects[EFFECT_RGBX]) {
      EffectRGBX* effectRGBX =
        static_cast<EffectRGBX*>(aEffectChain.mEffects[EFFECT_RGBX].get());
      source = effectRGBX->mRGBXTexture->AsSourceOGL();
      premultiplied = effectRGBX->mPremultiplied;
      flipped = effectRGBX->mFlipped;
      filter = gfx::ThebesFilter(effectRGBX->mFilter);
      program = GetProgram(gl::RGBXLayerProgramType, maskType);
    } else {
      EffectRGBAExternal* effectRGBAExternal =
        static_cast<EffectRGBAExternal*>(aEffectChain.mEffects[EFFECT_RGBA_EXTERNAL].get());
      source = effectRGBAExternal->mRGBATexture->AsSourceOGL();
      premultiplied = effectRGBAExternal->mPremultiplied;
      flipped = effectRGBAExternal->mFlipped;
      filter = gfx::ThebesFilter(effectRGBAExternal->mFilter);
      program = GetProgram(gl::RGBALayerExternalProgramType, maskType);
      program->SetTextureTransform(effectRGBAExternal->mTextureTransform);
    }

    if (!premultiplied) {
      mGLContext->fBlendFuncSeparate(LOCAL_GL_SRC_ALPHA, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                     LOCAL_GL_ONE, LOCAL_GL_ONE);
    }

    if (aEffectChain.mEffects[EFFECT_RGBA_EXTERNAL]) {
      source->BindTexture(LOCAL_GL_TEXTURE0);
    } else {
      source->BindTexture(0);
    }

    mGLContext->ApplyFilterToBoundTexture(filter);

    program->Activate();
    program->SetTextureUnit(0);
    program->SetLayerOpacity(aOpacity);
    program->SetLayerTransform(aTransform);
    program->SetRenderOffset(aOffset.x, aOffset.y);
    program->SetLayerQuadRect(aRect);
    if (maskType != MaskNone) {
      mGLContext->fActiveTexture(LOCAL_GL_TEXTURE1);
      sourceMask->BindTexture(LOCAL_GL_TEXTURE1);
      program->SetMaskTextureUnit(1);
      program->SetMaskLayerTransform(maskQuadTransform);
    }
    BindAndDrawQuad(program, flipped);

    if (!premultiplied) {
      mGLContext->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                     LOCAL_GL_ONE, LOCAL_GL_ONE);
    }

    if (aEffectChain.mEffects[EFFECT_RGBA_EXTERNAL]) {
      mGLContext->fBindTexture(LOCAL_GL_TEXTURE_EXTERNAL, 0);
    }
  } else if (aEffectChain.mEffects[EFFECT_YCBCR]) {
    EffectYCbCr* effectYCbCr =
      static_cast<EffectYCbCr*>(aEffectChain.mEffects[EFFECT_YCBCR].get());
    TextureSource* sourceYCbCr = effectYCbCr->mYCbCrTexture;
    const int Y = 0, Cb = 1, Cr = 2;
    TextureSourceOGL* sourceY =  sourceYCbCr->GetSubSource(Y)->AsSourceOGL();
    TextureSourceOGL* sourceCb = sourceYCbCr->GetSubSource(Cb)->AsSourceOGL();
    TextureSourceOGL* sourceCr = sourceYCbCr->GetSubSource(Cr)->AsSourceOGL();

    if (!sourceY && !sourceCb && !sourceCr) {
      NS_WARNING("Invalid layer texture.");
      return;
    }

    gfxPattern::GraphicsFilter filter = gfx::ThebesFilter(effectYCbCr->mFilter);

    sourceY->BindTexture(LOCAL_GL_TEXTURE0);
    mGLContext->ApplyFilterToBoundTexture(filter);
    sourceCb->BindTexture(LOCAL_GL_TEXTURE1);
    mGLContext->ApplyFilterToBoundTexture(filter);
    sourceCr->BindTexture(LOCAL_GL_TEXTURE2);
    mGLContext->ApplyFilterToBoundTexture(filter);

    ShaderProgramOGL *program = GetProgram(YCbCrLayerProgramType, maskType);

    program->Activate();
    program->SetYCbCrTextureUnits(0, 1, 2);
    program->SetLayerOpacity(aOpacity);
    program->SetLayerTransform(aTransform);
    program->SetRenderOffset(aOffset.x, aOffset.y);
    program->SetLayerQuadRect(aRect);
    if (maskType != MaskNone) {
      sourceMask->BindTexture(LOCAL_GL_TEXTURE3);
      program->SetMaskTextureUnit(3);
      program->SetMaskLayerTransform(maskQuadTransform);
    }
    BindAndDrawQuadWithTextureRect(program, intSourceRect, intTextureRect.Size());
  } else if (aEffectChain.mEffects[EFFECT_COMPONENT_ALPHA]) {
    EffectComponentAlpha* effectComponentAlpha =
      static_cast<EffectComponentAlpha*>(aEffectChain.mEffects[EFFECT_COMPONENT_ALPHA].get());
    TextureSourceOGL* sourceOnWhite = effectComponentAlpha->mOnWhite->AsSourceOGL();
    TextureSourceOGL* sourceOnBlack = effectComponentAlpha->mOnBlack->AsSourceOGL();

    if (!sourceOnBlack->IsValid() ||
        !sourceOnWhite->IsValid()) {
      NS_WARNING("Invalid layer texture for component alpha");
      return;
    }

    for (PRInt32 pass = 1; pass <=2; ++pass) {
      ShaderProgramOGL* program;
      if (pass == 1) {
        program = GetProgram(gl::ComponentAlphaPass1ProgramType, maskType);
        gl()->fBlendFuncSeparate(LOCAL_GL_ZERO, LOCAL_GL_ONE_MINUS_SRC_COLOR,
                                 LOCAL_GL_ONE, LOCAL_GL_ONE);
      } else {
        program = GetProgram(gl::ComponentAlphaPass2ProgramType, maskType);
        gl()->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ONE,
                                 LOCAL_GL_ONE, LOCAL_GL_ONE);
      }

      sourceOnBlack->BindTexture(LOCAL_GL_TEXTURE0);
      sourceOnWhite->BindTexture(LOCAL_GL_TEXTURE1);
      
      program->Activate();
      program->SetBlackTextureUnit(0);
      program->SetWhiteTextureUnit(1);
      program->SetLayerOpacity(aOpacity);
      program->SetLayerTransform(aTransform);
      program->SetRenderOffset(aOffset.x, aOffset.y);
      program->SetLayerQuadRect(aRect);
      if (maskType != MaskNone) {
        sourceMask->BindTexture(LOCAL_GL_TEXTURE2);
        program->SetMaskTextureUnit(2);
        program->SetMaskLayerTransform(maskQuadTransform);
      }

      BindAndDrawQuadWithTextureRect(program, intSourceRect, intTextureRect.Size(),
                                     sourceOnBlack->GetWrapMode());

      mGLContext->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                     LOCAL_GL_ONE, LOCAL_GL_ONE);
    }
  } else if (aEffectChain.mEffects[EFFECT_RENDER_TARGET]) {
    EffectRenderTarget* effectRenderTarget =
      static_cast<EffectRenderTarget*>(aEffectChain.mEffects[EFFECT_RENDER_TARGET].get());
    RefPtr<CompositingRenderTargetOGL> surface
      = static_cast<CompositingRenderTargetOGL*>(effectRenderTarget->mRenderTarget.get());

    ShaderProgramOGL *program = GetProgram(GetFBOLayerProgramType(), maskType);

    surface->BindTexture(LOCAL_GL_TEXTURE0);
    
    program->Activate();
    program->SetTextureUnit(0);
    program->SetLayerOpacity(aOpacity);
    program->SetLayerTransform(aTransform);
    program->SetRenderOffset(aOffset.x, aOffset.y);
    program->SetLayerQuadRect(aRect);
    if (maskType != MaskNone) {
      sourceMask->BindTexture(LOCAL_GL_TEXTURE1);
      program->SetMaskTextureUnit(1);
      program->SetMaskLayerTransform(maskQuadTransform);
    }
    if (program->GetTexCoordMultiplierUniformLocation() != -1) {
      // 2DRect case, get the multiplier right for a sampler2DRect
      program->SetTexCoordMultiplier(aRect.width, aRect.height);
    }

    // Drawing is always flipped, but when copying between surfaces we want to avoid
    // this. Pass true for the flip parameter to introduce a second flip
    // that cancels the other one out.
    BindAndDrawQuad(program, true);
  }

  mGLContext->fActiveTexture(LOCAL_GL_TEXTURE0);
  // in case rendering has used some other GL context
  MakeCurrent();
}

void
CompositorOGL::EndFrame(const gfxMatrix& aTransform)
{
  // Allow widget to render a custom foreground.
  mWidget->DrawWindowOverlay();

#ifdef MOZ_DUMP_PAINTING
  if (gfxUtils::sDumpPainting) {
    nsIntRect rect;
    if (mIsRenderingToEGLSurface) {
      rect = nsIntRect(0, 0, mSurfaceSize.width, mSurfaceSize.height);
    } else {
      mWidget->GetBounds(rect);
    }
    nsRefPtr<gfxASurface> surf = gfxPlatform::GetPlatform()->CreateOffscreenSurface(rect.Size(), gfxASurface::CONTENT_COLOR_ALPHA);
    nsRefPtr<gfxContext> ctx = new gfxContext(surf);
    CopyToTarget(ctx, aTransform);

    WriteSnapshotToDumpFile(this, surf);
  }
#endif

  mFrameInProgress = false;

  if (mTarget) {
    CopyToTarget(mTarget, aTransform);
    mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);
    return;
  }

  if (sDrawFPS && !mFPS) {
    mFPS = new FPSState();
  } else if (!sDrawFPS && mFPS) {
    mFPS = nullptr;
  }

  if (mFPS) {
    mFPS->DrawFPS(TimeStamp::Now(), mGLContext, GetProgram(Copy2DProgramType));
  } else if (sFrameCounter) {
    FPSState::DrawFrameCounter(mGLContext);
  }

  mGLContext->SwapBuffers();
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);
}

void
CompositorOGL::EndFrameForExternalComposition(const gfxMatrix& aTransform)
{
  if (sDrawFPS) {
    if (!mFPS) {
      mFPS = new FPSState();
    }
    double fps = mFPS->mCompositionFps.AddFrameAndGetFps(TimeStamp::Now());
    printf_stderr("HWComposer: FPS is %g\n", fps);
  }

  // This lets us reftest and screenshot content rendered externally
  if (mTarget) {
    MakeCurrent();
    CopyToTarget(mTarget, aTransform);
    mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);
  }
}

void
CompositorOGL::AbortFrame()
{
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);
  mFrameInProgress = false;
}

void
CompositorOGL::SetRenderTargetSize(int aWidth, int aHeight)
{
  mSurfaceSize.width = aWidth;
  mSurfaceSize.height = aHeight;
}

void
CompositorOGL::CopyToTarget(gfxContext *aTarget, const gfxMatrix& aTransform)
{
  nsIntRect rect;
  if (mIsRenderingToEGLSurface) {
    rect = nsIntRect(0, 0, mSurfaceSize.width, mSurfaceSize.height);
  } else {
    rect = nsIntRect(0, 0, mWidgetSize.width, mWidgetSize.height);
  }
  GLint width = rect.width;
  GLint height = rect.height;

  if ((PRInt64(width) * PRInt64(height) * PRInt64(4)) > PR_INT32_MAX) {
    NS_ERROR("Widget size too big - integer overflow!");
    return;
  }

  nsRefPtr<gfxImageSurface> imageSurface =
    new gfxImageSurface(gfxIntSize(width, height),
                        gfxASurface::ImageFormatARGB32);

  mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);

  if (!mGLContext->IsGLES2()) {
    // GLES2 promises that binding to any custom FBO will attach
    // to GL_COLOR_ATTACHMENT0 attachment point.
    mGLContext->fReadBuffer(LOCAL_GL_BACK);
  }

  NS_ASSERTION(imageSurface->Stride() == width * 4,
               "Image Surfaces being created with weird stride!");

  mGLContext->ReadPixelsIntoImageSurface(imageSurface);

  // Map from GL space to Cairo space and reverse the world transform.
  gfxMatrix glToCairoTransform = aTransform;
  glToCairoTransform.Invert();
  glToCairoTransform.Scale(1.0, -1.0);
  glToCairoTransform.Translate(-gfxPoint(0.0, height));

  gfxContextAutoSaveRestore restore(aTarget);
  aTarget->SetOperator(gfxContext::OPERATOR_SOURCE);
  aTarget->SetMatrix(glToCairoTransform);
  aTarget->SetSource(imageSurface);
  aTarget->Paint();
}

void
CompositorOGL::NotifyShadowTreeTransaction()
{
  if (mFPS) {
    mFPS->NotifyShadowTreeTransaction();
  }
}

void
CompositorOGL::Pause()
{
#ifdef MOZ_WIDGET_ANDROID
  gl()->ReleaseSurface();
#endif
}

void
CompositorOGL::Resume()
{
#ifdef MOZ_WIDGET_ANDROID
  gl()->RenewSurface();
#endif
}


} /* layers */
} /* mozilla */
