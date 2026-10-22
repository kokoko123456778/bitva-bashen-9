// =====================================================================
//  БИТВА БАШЕН — карточные бои на арене: собирай колоду, выставляй войска, ломай башни (3D)
//  C++ + SDL для ccdroid / C4droid. Один файл, внешних библиотек нет.
//  Если не собирается: ниже поставь SDL_MODE 1 (SDL 1.2) или 2 (SDL2).
// =====================================================================
#ifndef SDL_MODE
#define SDL_MODE 0            // 0 = авто, 1 = SDL 1.2, 2 = SDL2
#endif
#if SDL_MODE == 0
  #if defined(__has_include)
    #if __has_include(<SDL2/SDL.h>)
      #undef SDL_MODE
      #define SDL_MODE 2
    #endif
  #endif
#endif
#if SDL_MODE == 0
  #undef SDL_MODE
  #define SDL_MODE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#if defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
  #pragma GCC optimize("O2")      // если среда собирает без оптимизации, включаем её для этого файла
#endif
#include <vector>
#include <cstddef>
#include <map>
#if defined(__unix__) || defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
  #include <unistd.h>
  #define HAVE_GETCWD 1
#endif
#if SDL_MODE == 2
  #include <SDL2/SDL.h>
#else
  #include <SDL/SDL.h>
#endif

// ---------- Утилиты ----------
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline float fminv(float a, float b) { return a < b ? a : b; }
static inline float fmaxv(float a, float b) { return a > b ? a : b; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static float frand(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }

struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline V3 vnorm(V3 a) { float l = sqrtf(dot(a, a)); if (l < 1e-8f) l = 1; return a * (1.0f / l); }

typedef uint32_t u32;
// ---- видеокарта (OpenGL ES 2): 3D рисует GPU, а 2D-интерфейс кладётся поверх прозрачной картинкой ----
static bool gGL = false;          // рисуем ли 3D видеокартой
static bool gWantGL = true;       // пробовать ли видеокарту при запуске (можно выключить в меню)
static bool gOverlay = false;     // fb - прозрачный слой интерфейса поверх 3D (альфа в старшем байте)
struct GV { float x, y, z; unsigned char r, g, b, a; };
static std::vector<GV> gVerts;    // треугольники 3D за кадр (в координатах камеры)
static unsigned gSkyCol = 0;
static float gNearZ = 0.3f;   // ближе этой дистанции полигоны обрезаются (в квартире - 0.06, чтобы стены не просвечивали)
static inline u32 RGB(int r, int g, int b) {
  r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
  return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}
static inline int CR(u32 c) { return (c >> 16) & 255; }
static inline int CG(u32 c) { return (c >> 8) & 255; }
static inline int CB(u32 c) { return c & 255; }
static u32 mixc(u32 a, u32 b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  return RGB((int)(CR(a) + (CR(b) - CR(a)) * t), (int)(CG(a) + (CG(b) - CG(a)) * t),
             (int)(CB(a) + (CB(b) - CB(a)) * t));
}
static u32 shadec(u32 c, float f) { return RGB((int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f)); }

// ---------- Экран и буферы ----------
static int SW = 800, SH = 480, IW = 400, IH = 240, fs = 2;
static int LW = 480, LH = 800;      // логический размер экрана (игра вертикальная)
static bool rotMode = false;        // экран вертикальный -> картинку поворачиваем
static int rotDir = 1;              // 1 = по часовой, -1 = против
static int quality = 1;              // 0 низкая, 1 средняя, 2 высокая (разрешение картинки)
static bool aaOn = true;             // сглаживание краёв (можно выключить в меню, если телефон тормозит)
static int wheelN = 10;              // число граней у колёс и фар (растёт с уровнем графики)
static float gProf3D = 0, gProfPr = 0;   // мс за кадр: 3D-превью и вывод на экран (для счётчика FPS)
static bool soundOn = true;           // звук мотора
static bool fpsOn = true;            // счётчик FPS в углу экрана
static bool gDry = false;            // пробный проход: считаем, где фары относительно поверхности, ничего не рисуя
static bool autoPerf = true;         // автоматически упрощать картинку, если телефон не успевает
static int gPerf = 0;                // 0 - полная картинка, 1 - без сглаживания и суперсэмплинга, 2 - ещё и проще колёса, 3 - ещё и картинка на ступень мельче (только пока не хватает скорости)
static int gResDrop = 0;              // на сколько ступеней качества временно уменьшено разрешение (авто-режим, уровень 3)
static bool gApplyQ = false;
static int insL = 0, insR = 0;      // поля экрана: сколько физ. пикселей не видно с начала/конца оси X картинки
static u32 *fb = NULL;
static float *zb = NULL;
static int clipX0, clipY0, clipX1, clipY1;
static float focal = 300, vcx = 0, vcy = 0;
static u32 fogCol = 0;
static float fogA = 1e9f, fogB = 2e9f;
static float gTime = 0;

// ---------- Ввод ----------
struct Ev { int type, x, y; };   // 0 выход, 1 нажатие, 2 отпускание, 3 движение, 4 назад
struct Ptr {
  bool down, pressed, clicked;
  int x, y, sx, sy, cx, cy, dx, dy;
};
static Ptr P;

// ---------- Бэкенд SDL ----------
#if SDL_MODE == 2
static SDL_Window *gWin = NULL;
static SDL_Renderer *gRen = NULL;
static SDL_Texture *gTex = NULL;
static SDL_GLContext gCtx = NULL;

// ======================= OpenGL ES 2 =======================
typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei; typedef float GLfloat; typedef unsigned char GLboolean; typedef char GLchar; typedef unsigned int GLbitfield; typedef ptrdiff_t GLsizeiptr;
#define GLF(ret, name, args) typedef ret (*PFN_##name) args; static PFN_##name p_##name = NULL;
GLF(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) GLF(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) GLF(void, glClear, (GLbitfield))
GLF(void, glEnable, (GLenum)) GLF(void, glDisable, (GLenum)) GLF(void, glDepthFunc, (GLenum)) GLF(void, glDepthMask, (GLboolean)) GLF(void, glBlendFunc, (GLenum, GLenum))
GLF(GLuint, glCreateShader, (GLenum)) GLF(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) GLF(void, glCompileShader, (GLuint))
GLF(void, glGetShaderiv, (GLuint, GLenum, GLint *)) GLF(GLuint, glCreateProgram, (void)) GLF(void, glAttachShader, (GLuint, GLuint))
GLF(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *)) GLF(void, glLinkProgram, (GLuint)) GLF(void, glGetProgramiv, (GLuint, GLenum, GLint *))
GLF(void, glUseProgram, (GLuint)) GLF(GLint, glGetUniformLocation, (GLuint, const GLchar *)) GLF(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
GLF(void, glUniform2f, (GLint, GLfloat, GLfloat)) GLF(void, glUniform1i, (GLint, GLint)) GLF(void, glBindBuffer, (GLenum, GLuint))
GLF(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) GLF(void, glEnableVertexAttribArray, (GLuint)) GLF(void, glDisableVertexAttribArray, (GLuint))
GLF(void, glDrawArrays, (GLenum, GLint, GLsizei)) GLF(void, glGenTextures, (GLsizei, GLuint *)) GLF(void, glBindTexture, (GLenum, GLuint)) GLF(void, glDeleteTextures, (GLsizei, const GLuint *))
GLF(void, glTexParameteri, (GLenum, GLenum, GLint)) GLF(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))
GLF(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) GLF(void, glActiveTexture, (GLenum)) GLF(void, glPixelStorei, (GLenum, GLint))
GLF(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))
#undef GLF
static GLuint glProg3D = 0, glProg2D = 0, glTexUI = 0; static GLint uP = -1, uNF = -1, uTex = -1; static int glTexW = 0, glTexH = 0, glVW = 0, glVH = 0;
static GLuint glMakeProg(const char *vs, const char *fs2) {
  GLuint v = p_glCreateShader(0x8B31), f = p_glCreateShader(0x8B30); GLint ok = 0;
  p_glShaderSource(v, 1, &vs, NULL); p_glCompileShader(v); p_glGetShaderiv(v, 0x8B81, &ok); if (!ok) return 0;
  p_glShaderSource(f, 1, &fs2, NULL); p_glCompileShader(f); p_glGetShaderiv(f, 0x8B81, &ok); if (!ok) return 0;
  GLuint p = p_glCreateProgram(); p_glAttachShader(p, v); p_glAttachShader(p, f);
  p_glBindAttribLocation(p, 0, "aP"); p_glBindAttribLocation(p, 1, "aC"); p_glLinkProgram(p); p_glGetProgramiv(p, 0x8B82, &ok);
  return ok ? p : 0;
}
// loader - функция, которая по имени даёт адрес функции GL (SDL_GL_GetProcAddress или eglGetProcAddress)
static bool glSetup(void *(*loader)(const char *)) {
  #define LD(n) p_##n = (PFN_##n)loader(#n); if (!p_##n) return false;
  LD(glViewport) LD(glClearColor) LD(glClear) LD(glEnable) LD(glDisable) LD(glDepthFunc) LD(glDepthMask) LD(glBlendFunc) LD(glCreateShader) LD(glShaderSource)
  LD(glCompileShader) LD(glGetShaderiv) LD(glCreateProgram) LD(glAttachShader) LD(glBindAttribLocation) LD(glLinkProgram) LD(glGetProgramiv) LD(glUseProgram)
  LD(glGetUniformLocation) LD(glUniform4f) LD(glUniform2f) LD(glUniform1i) LD(glBindBuffer) LD(glVertexAttribPointer) LD(glEnableVertexAttribArray)
  LD(glDisableVertexAttribArray) LD(glDrawArrays) LD(glGenTextures) LD(glBindTexture) LD(glDeleteTextures) LD(glTexParameteri) LD(glTexImage2D) LD(glTexSubImage2D)
  LD(glActiveTexture) LD(glPixelStorei)
  #undef LD
  p_glReadPixels = (PFN_glReadPixels)loader("glReadPixels");
  // 3D: точки уже в координатах камеры; проекция как у программного движка (focal, vcx, vcy), глубина - видеокартой
  glProg3D = glMakeProg(
    "attribute vec3 aP; attribute vec4 aC; uniform vec4 uP; uniform vec2 uNF; varying vec4 vC;\n"
    "void main(){ vC = aC; gl_Position = vec4(aP.x * uP.x + uP.y * aP.z, aP.y * uP.z + uP.w * aP.z, aP.z * uNF.x + uNF.y, aP.z); }\n",
    "precision mediump float; varying vec4 vC; void main(){ gl_FragColor = vC; }\n");
  // 2D: картинка интерфейса поверх (в памяти байты B,G,R,A - переставляем)
  glProg2D = glMakeProg(
    "attribute vec2 aP; varying vec2 vT; void main(){ vT = vec2(aP.x * 0.5 + 0.5, 0.5 - aP.y * 0.5); gl_Position = vec4(aP, 0.0, 1.0); }\n",
    "precision mediump float; varying vec2 vT; uniform sampler2D uTex; void main(){ vec4 t = texture2D(uTex, vT); gl_FragColor = vec4(t.b, t.g, t.r, t.a); }\n");
  if (!glProg3D || !glProg2D) return false;
  uP = p_glGetUniformLocation(glProg3D, "uP"); uNF = p_glGetUniformLocation(glProg3D, "uNF"); uTex = p_glGetUniformLocation(glProg2D, "uTex");
  p_glGenTextures(1, &glTexUI);
  return true;
}
static void glUITex(int w, int h) {
  if (glTexW == w && glTexH == h) return;
  p_glBindTexture(0x0DE1, glTexUI);
  p_glTexParameteri(0x0DE1, 0x2801, 0x2600); p_glTexParameteri(0x0DE1, 0x2800, 0x2600);      // NEAREST: пиксельный шрифт чёткий
  p_glTexParameteri(0x0DE1, 0x2802, 0x812F); p_glTexParameteri(0x0DE1, 0x2803, 0x812F);
  p_glTexImage2D(0x0DE1, 0, 0x1908, w, h, 0, 0x1908, 0x1401, NULL); glTexW = w; glTexH = h;
}
// кадр: очистка, все 3D-треугольники одним вызовом, потом интерфейс поверх
static void glFrame(int vw, int vh) {
  p_glViewport(0, 0, vw, vh);
  p_glClearColor(((gSkyCol >> 16) & 255) / 255.0f, ((gSkyCol >> 8) & 255) / 255.0f, (gSkyCol & 255) / 255.0f, 1.0f);
  p_glClear(0x4000 | 0x100);
  if (!gVerts.empty()) {
    p_glEnable(0x0B71); p_glDepthFunc(0x0203); p_glDepthMask(1); p_glDisable(0x0BE2); p_glDisable(0x0B44);
    p_glUseProgram(glProg3D); p_glBindBuffer(0x8892, 0);
    float n = fmaxv(0.02f, gNearZ), f = 120.0f;
    p_glUniform4f(uP, 2.0f * focal / IW, 2.0f * vcx / IW - 1.0f, 2.0f * focal / IH, 1.0f - 2.0f * vcy / IH);
    p_glUniform2f(uNF, (f + n) / (f - n), -2.0f * f * n / (f - n));
    p_glEnableVertexAttribArray(0); p_glEnableVertexAttribArray(1);
    p_glVertexAttribPointer(0, 3, 0x1406, 0, sizeof(GV), &gVerts[0].x);
    p_glVertexAttribPointer(1, 4, 0x1401, 1, sizeof(GV), &gVerts[0].r);
    p_glDrawArrays(4, 0, (GLsizei)gVerts.size());
    p_glDisableVertexAttribArray(1);
  }
  p_glDisable(0x0B71); p_glEnable(0x0BE2); p_glBlendFunc(0x0302, 0x0303);
  p_glUseProgram(glProg2D); p_glActiveTexture(0x84C0); glUITex(IW, IH); p_glBindTexture(0x0DE1, glTexUI);
  p_glPixelStorei(0x0CF5, 4); p_glTexSubImage2D(0x0DE1, 0, 0, 0, IW, IH, 0x1908, 0x1401, fb); p_glUniform1i(uTex, 0);
  static const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
  p_glEnableVertexAttribArray(0); p_glVertexAttribPointer(0, 2, 0x1406, 0, 0, quad); p_glDrawArrays(5, 0, 4);
  gVerts.clear();
  memset(fb, 0, sizeof(u32) * IW * IH);          // интерфейс следующего кадра - на прозрачном фоне
}

static void *sdlGLProc(const char *n) { return SDL_GL_GetProcAddress(n); }
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  if (gWantGL) {                                   // пробуем видеокарту; не вышло - обычный режим
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    gWin = SDL_CreateWindow("Bitva bashen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (gWin) { gCtx = SDL_GL_CreateContext(gWin);
      if (!gCtx) { SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16); gCtx = SDL_GL_CreateContext(gWin); }
      if (gCtx && glSetup(sdlGLProc)) { SDL_GL_SetSwapInterval(1); SDL_GetWindowSize(gWin, &SW, &SH); gGL = true; gOverlay = true; return true; }
      if (gCtx) SDL_GL_DeleteContext(gCtx); gCtx = NULL; SDL_DestroyWindow(gWin); gWin = NULL; }
  }
  gWin = SDL_CreateWindow("Bitva bashen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                          SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
  if (!gWin) return false;
  SDL_GetWindowSize(gWin, &SW, &SH);
  gRen = SDL_CreateRenderer(gWin, -1, 0);
  if (!gRen) return false;
  return true;
}
static void gfxTex() {
  if (gGL) return;
  if (gTex) SDL_DestroyTexture(gTex);
  gTex = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, IW, IH);
}
static void gfxResize() {}
static void gfxPresent() {
  if (gGL) { int dw = SW, dh = SH; SDL_GL_GetDrawableSize(gWin, &dw, &dh); glFrame(dw, dh); SDL_GL_SwapWindow(gWin); return; }
  SDL_UpdateTexture(gTex, NULL, fb, IW * 4);
  SDL_RenderClear(gRen);
  if (rotMode) {
    SDL_Rect d;
    int avail = SH - insL - insR;
    int cy = rotDir > 0 ? insL + avail / 2 : SH - insL - avail / 2;
    d.w = avail; d.h = SW; d.x = SW / 2 - d.w / 2; d.y = cy - d.h / 2;
    SDL_RenderCopyEx(gRen, gTex, NULL, &d, rotDir > 0 ? 90.0 : 270.0, NULL, SDL_FLIP_NONE);
  } else {
    SDL_Rect d;
    d.x = insL; d.y = 0; d.w = SW - insL - insR; d.h = SH;
    SDL_RenderCopy(gRen, gTex, NULL, &d);
  }
  SDL_RenderPresent(gRen);
}
static void gfxQuit() { SDL_Quit(); }
#else
static SDL_Surface *gScr = NULL;
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  gScr = SDL_SetVideoMode(0, 0, 32, SDL_SWSURFACE | SDL_FULLSCREEN);
  if (!gScr) gScr = SDL_SetVideoMode(800, 480, 32, SDL_SWSURFACE);
  if (!gScr) return false;
  SW = gScr->w;
  SH = gScr->h;
  return true;
}
static void gfxTex() {}
static u32 *convBuf = NULL, *rotBuf = NULL;
static int *axMap = NULL;
static void gfxResize() { free(convBuf); convBuf = NULL; free(rotBuf); rotBuf = NULL; }
static void gfxPresent() {
  if (SDL_MUSTLOCK(gScr)) SDL_LockSurface(gScr);
  SDL_PixelFormat *f = gScr->format;
  int bpp = f->BytesPerPixel;
  int n = IW * IH;
  if (!axMap) axMap = (int *)malloc(sizeof(int) * SW);
  const u32 *src = fb;
  bool native = bpp == 4 && f->Rshift == 16 && f->Gshift == 8 && f->Bshift == 0 && f->Rloss == 0 && f->Gloss == 0 && f->Bloss == 0;
  if (!native) {       // перевод в формат экрана (только если он не 0x00RRGGBB)
    if (!convBuf) convBuf = (u32 *)malloc(sizeof(u32) * n);
    for (int i = 0; i < n; i++) {
      u32 c = fb[i];
      convBuf[i] = (((c >> 16) & 255) >> f->Rloss << f->Rshift) | (((c >> 8) & 255) >> f->Gloss << f->Gshift) |
                   ((c & 255) >> f->Bloss << f->Bshift);
    }
    src = convBuf;
  }
  int PW = rotMode ? SH : SW, avail = imax(1, PW - insL - insR);
  if (rotMode) {       // транспонируем блоками: строки rotBuf = столбцы картинки (читать потом подряд)
    if (!rotBuf) rotBuf = (u32 *)malloc(sizeof(u32) * n);
    const int B = 16;
    for (int vb = 0; vb < IH; vb += B)
      for (int ub = 0; ub < IW; ub += B) {
        int ve = imin(vb + B, IH), ue = imin(ub + B, IW);
        for (int v = vb; v < ve; v++) {
          const u32 *sr = src + v * IW;
          for (int u = ub; u < ue; u++) rotBuf[u * IH + v] = sr[u];
        }
      }
    for (int x = 0; x < SW; x++) axMap[x] = imin((rotDir > 0 ? SW - 1 - x : x) * IH / SW, IH - 1);
  } else {
    for (int x = 0; x < SW; x++) axMap[x] = clampi((x - insL) * IW / avail, 0, IW - 1);
  }
  unsigned char *base = (unsigned char *)gScr->pixels;
  int prevBy = -1;
  unsigned char *prevRow = NULL;
  for (int y = 0; y < SH; y++) {
    unsigned char *row = base + y * gScr->pitch;
    int by;
    const u32 *sr;
    if (!rotMode) {
      by = imin(y * IH / SH, IH - 1);
      sr = src + by * IW;
    } else {
      int pu = rotDir > 0 ? y : SH - 1 - y;
      if (pu < insL || pu >= SH - insR) { memset(row, 0, SW * bpp); prevBy = -1; continue; }   // поле экрана
      by = clampi((pu - insL) * IW / avail, 0, IW - 1);
      sr = rotBuf + by * IH;
    }
    if (by == prevBy) { memcpy(row, prevRow, SW * bpp); continue; }
    if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < SW; x++) d[x] = sr[axMap[x]]; }
    else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < SW; x++) d[x] = (uint16_t)sr[axMap[x]]; }
    else for (int x = 0; x < SW; x++) { u32 v = sr[axMap[x]]; row[x * 3] = v & 255; row[x * 3 + 1] = (v >> 8) & 255; row[x * 3 + 2] = (v >> 16) & 255; }
    if (!rotMode) {   // поля слева и справа
      if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
      else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
    }
    prevBy = by;
    prevRow = row;
  }
  if (SDL_MUSTLOCK(gScr)) SDL_UnlockSurface(gScr);
  SDL_Flip(gScr);
}
static void gfxQuit() { SDL_Quit(); }
#endif

static bool pollEv(Ev &e) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_QUIT: e.type = 0; e.x = e.y = 0; return true;
      case SDL_MOUSEBUTTONDOWN: e.type = 1; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEBUTTONUP: e.type = 2; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEMOTION: e.type = 3; e.x = ev.motion.x; e.y = ev.motion.y; return true;
      case SDL_KEYDOWN:
        if (ev.key.keysym.sym == SDLK_ESCAPE) { e.type = 4; e.x = e.y = 0; return true; }
        break;
#if SDL_MODE == 2
      case SDL_APP_WILLENTERBACKGROUND:
      case SDL_APP_TERMINATING:
      case SDL_APP_LOWMEMORY:
        e.type = 5; e.x = e.y = 0; return true;
#else
      case SDL_ACTIVEEVENT:
        if (ev.active.gain == 0) { e.type = 5; e.x = e.y = 0; return true; }
        break;
#endif
      default: break;
    }
  }
  return false;
}

// окно на Android часто сначала меньше экрана (пока прячутся системные панели), а потом вырастает:
// если не пересчитать размер, справа остаётся чёрная полоса
static bool gfxCheckResize() {
#if SDL_MODE == 2
  int w = 0, h = 0; SDL_GetWindowSize(gWin, &w, &h);
  if (w > 0 && h > 0 && (w != SW || h != SH)) { SW = w; SH = h; return true; }
#endif
  return false;
}

static void mapPt(int x, int y, int &mx, int &my) {
  int pu, pv;   // pu - физ. координата вдоль оси X картинки, pv - вдоль оси Y
  if (!rotMode) { pu = x; pv = y; }
  else if (rotDir > 0) { pu = y; pv = SW - 1 - x; }
  else { pu = SH - 1 - y; pv = x; }
  int PW = rotMode ? SH : SW, PH = rotMode ? SW : SH;
  int avail = imax(1, PW - insL - insR);
  mx = clampi((pu - insL) * IW / avail, 0, IW - 1);
  my = clampi(pv * IH / imax(1, PH), 0, IH - 1);
}
static void computeSize() {
  static const int T[3] = {360, 480, 600};
  int md = imax(1, imin(SW, SH));
  int qe = imax(0, quality - gResDrop);
  IW = imax(200, LW * T[qe] / md);
  IH = imax(120, LH * T[qe] / md);
  fs = imax(1, imin(IW, IH) / 170);
}
static bool allocBuffers() {
  free(fb); free(zb);
  fb = (u32 *)malloc(sizeof(u32) * IW * IH);
  zb = (float *)malloc(sizeof(float) * IW * IH);
  if (!fb || !zb) return false;
  memset(fb, 0, sizeof(u32) * IW * IH);
  gfxResize();
  gfxTex();
  return true;
}
// Настройки экрана (поля) хранятся отдельно от игры
// Папка для файлов игры. На Android рабочая папка недоступна для записи, поэтому пишем во внутреннюю память приложения
// (иначе деньги и прогресс не сохранялись). На других системах - рядом с игрой, как раньше.
static char gBase[400] = "";
static char gPathBuf[4][520];
static int gPathI = 0;
static const char *pth(const char *name) {
  gPathI = (gPathI + 1) & 3;
  snprintf(gPathBuf[gPathI], sizeof gPathBuf[0], "%s%s", gBase, name);
  return gPathBuf[gPathI];
}
static const char *CFGF = "bashni.cfg";
static int autoInset() { return rotMode ? SH * 19 / 1000 : 0; }
static void saveCfg() {
  FILE *f = fopen(pth(CFGF), "w");
  if (!f) return;
  fprintf(f, "%d %d %d %d %d %d %d %d 2\n", insL, insR, rotDir, quality, aaOn ? 1 : 0, fpsOn ? 1 : 0, autoPerf ? 1 : 0, soundOn ? 1 : 0);
  fclose(f);
}
static void loadCfg() {
  FILE *f = fopen(pth(CFGF), "r");
  if (!f) return;
  int a = 0, b = 0, d = 1, q = 1, aa = 1, fp = 1, ap = 1, so = 1, ver = 0;
  int got = fscanf(f, "%d %d %d %d %d %d %d %d %d", &a, &b, &d, &q, &aa, &fp, &ap, &so, &ver);
  if (got < 9 || ver < 2) { a = 0; b = 0; }          // старые поля экрана не берём: приложение теперь рисует на весь экран
  if (got >= 8) soundOn = so != 0;
  if (got >= 3) { insL = clampi(a, 0, 260); insR = clampi(b, 0, 260); rotDir = d < 0 ? -1 : 1; }
  if (got >= 4) quality = clampi(q, 0, 2);
  if (got >= 5) aaOn = aa != 0;
  if (got >= 6) fpsOn = fp != 0;
  if (got >= 7) autoPerf = ap != 0;
  fclose(f);
}

// ---------- Шрифт 5x7 (заглавные, латиница + кириллица) ----------
static const unsigned char FLAT[26][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}};
static const unsigned char FDIG[10][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
  {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};
// А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я
static const unsigned char FCYR[32][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1F,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
  {0x1F,0x10,0x10,0x10,0x10,0x10,0x10},{0x06,0x0A,0x0A,0x0A,0x0A,0x1F,0x11},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
  {0x15,0x15,0x15,0x0E,0x15,0x15,0x15},{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},{0x11,0x11,0x13,0x15,0x19,0x11,0x11},
  {0x0A,0x11,0x13,0x15,0x19,0x11,0x11},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x07,0x09,0x09,0x09,0x09,0x09,0x11},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1F,0x11,0x11,0x11,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x0F,0x01,0x11,0x0E},{0x04,0x0E,0x15,0x15,0x15,0x0E,0x04},
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},{0x12,0x12,0x12,0x12,0x12,0x1F,0x01},{0x11,0x11,0x11,0x0F,0x01,0x01,0x01},
  {0x15,0x15,0x15,0x15,0x15,0x15,0x1F},{0x15,0x15,0x15,0x15,0x15,0x1F,0x01},{0x18,0x08,0x08,0x0E,0x09,0x09,0x0E},
  {0x11,0x11,0x11,0x19,0x15,0x15,0x19},{0x10,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x01,0x0F,0x01,0x11,0x0E},
  {0x12,0x15,0x15,0x1D,0x15,0x15,0x12},{0x0F,0x11,0x11,0x0F,0x05,0x09,0x11}};
static const unsigned char FYO[7] = {0x0A,0x1F,0x10,0x1E,0x10,0x10,0x1F};
struct PG { char c; unsigned char r[7]; };
static const PG FPUN[] = {
  {'.',{0,0,0,0,0,0x0C,0x0C}},{',',{0,0,0,0,0x0C,0x04,0x08}},{':',{0,0x0C,0x0C,0,0x0C,0x0C,0}},
  {'!',{0x04,0x04,0x04,0x04,0x04,0,0x04}},{'?',{0x0E,0x11,0x01,0x02,0x04,0,0x04}},{'-',{0,0,0,0x1F,0,0,0}},
  {'+',{0,0x04,0x04,0x1F,0x04,0x04,0}},{'/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
  {'%',{0x18,0x19,0x02,0x04,0x08,0x13,0x03}},{'$',{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}},
  {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}},{')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
  {'<',{0x02,0x04,0x08,0x10,0x08,0x04,0x02}},{'>',{0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
  {'=',{0,0,0x1F,0,0x1F,0,0}},{'|',{0x04,0x04,0x04,0x04,0x04,0x04,0x04}},{'_',{0,0,0,0,0,0,0x1F}},
  {'\'',{0x04,0x04,0x08,0,0,0,0}},{'*',{0,0x15,0x0E,0x1F,0x0E,0x15,0}},{'#',{0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0}},
  {'~',{0,0,0x08,0x15,0x02,0,0}},{'"',{0x0A,0x0A,0x0A,0,0,0,0}}};
static const int NPUN = sizeof(FPUN) / sizeof(FPUN[0]);

static const unsigned char *glyph(int cp) {
  static const unsigned char blank[7] = {0, 0, 0, 0, 0, 0, 0};
  if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
  if (cp == 0x451) cp = 0x401;
  if (cp >= 'a' && cp <= 'z') cp -= 32;
  if (cp >= 'A' && cp <= 'Z') return FLAT[cp - 'A'];
  if (cp >= '0' && cp <= '9') return FDIG[cp - '0'];
  if (cp >= 0x410 && cp <= 0x42F) return FCYR[cp - 0x410];
  if (cp == 0x401) return FYO;
  if (cp < 128)
    for (int i = 0; i < NPUN; i++)
      if (FPUN[i].c == (char)cp) return FPUN[i].r;
  return blank;
}
static int u8next(const char *&p) {
  unsigned char c = (unsigned char)*p;
  if (c < 0x80) { p++; return c; }
  if ((c & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) { int r = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; return r; }
  if ((c & 0xF0) == 0xE0 && p[1] && p[2] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { int r = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; return r; }
  p++;
  return '?';
}
static int textW(const char *s, int sc) {
  int n = 0;
  while (*s) { u8next(s); n++; }
  return n * 6 * sc;
}
static inline void putpx(int x, int y, u32 c) {
  if ((unsigned)x < (unsigned)IW && (unsigned)y < (unsigned)IH) fb[y * IW + x] = c;
}
static void fillRect(int x, int y, int w, int h, u32 c) {
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    if (gOverlay) { u32 cc = c | 0xFF000000u; for (int i = x0; i < x1; i++) r[i] = cc; }
    else for (int i = x0; i < x1; i++) r[i] = c;
  }
}
static void fillRectA(int x, int y, int w, int h, u32 c, int a) {   // a: 0..255
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  int ia = 255 - a;
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    for (int i = x0; i < x1; i++) {
      u32 d = r[i];
      if (gOverlay) { u32 da = d >> 24; if (!da) { r[i] = (c & 0xFFFFFFu) | ((u32)a << 24); continue; }
        r[i] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255) | ((da + (255 - da) * a / 255) << 24); continue; }
      r[i] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
static void fillCircle(int cx, int cy, int r, u32 c, int a) {
  int ia = 255 - a;
  for (int j = -r; j <= r; j++) {
    int y = cy + j;
    if ((unsigned)y >= (unsigned)IH) continue;
    int w = (int)sqrtf((float)(r * r - j * j));
    for (int i = -w; i <= w; i++) {
      int x = cx + i;
      if ((unsigned)x >= (unsigned)IW) continue;
      u32 d = fb[y * IW + x];
      if (gOverlay) { u32 da = d >> 24; if (!da) { fb[y * IW + x] = (c & 0xFFFFFFu) | ((u32)a << 24); continue; }
        fb[y * IW + x] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255) | ((da + (255 - da) * a / 255) << 24); continue; }
      fb[y * IW + x] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
// align: 0 слева, 1 по центру, 2 справа
static void drawText(int x, int y, const char *s, int sc, u32 col, int align) {
  int w = textW(s, sc);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  while (*s) {
    int cp = u8next(s);
    const unsigned char *g = glyph(cp);
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++)
        if (g[r] & (0x10 >> c)) fillRect(x + c * sc, y + r * sc, sc, sc, col);
    x += 6 * sc;
  }
}
static void drawTextSh(int x, int y, const char *s, int sc, u32 col, int align) {
  drawText(x + sc, y + sc, s, sc, RGB(0, 0, 0), align);
  drawText(x, y, s, sc, col, align);
}
// ---------- Камера и растеризатор ----------
static V3 camEye, camF, camR, camU;
static void setCamera(V3 eye, V3 target) {
  camEye = eye;
  camF = vnorm(target - eye);
  camR = vnorm(cross(v3(0, 1, 0), camF));
  camU = cross(camF, camR);
}
static inline V3 toView(V3 p) {
  V3 d = p - camEye;
  return v3(dot(d, camR), dot(d, camU), dot(d, camF));
}
static bool projectPt(V3 p, int &sx, int &sy, float &z) {
  V3 v = toView(p);
  z = v.z;
  if (v.z < 0.5f) return false;
  sx = (int)(vcx + v.x * focal / v.z);
  sy = (int)(vcy - v.y * focal / v.z);
  return true;
}

static void rasterTri(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, u32 col) {
  // сортируем вершины по Y: a <= b <= c
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay);
  float dxac = (cx - ax) * iac, dwac = (cw - aw) * iac;
  float dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f;
    float xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f;
    float w = wl + dw * (xs + 0.5f - xl);
    float *zp = zb + y * IW + xs;
    u32 *fp = fb + y * IW + xs;
    int cnt = xe - xs + 1;
    for (; cnt >= 4; cnt -= 4, zp += 4, fp += 4, w += dw * 4.0f) {      // по 4 пикселя за проход
      float w1 = w + dw, w2 = w1 + dw, w3 = w2 + dw;
      if (w > zp[0]) { zp[0] = w; fp[0] = col; }
      if (w1 > zp[1]) { zp[1] = w1; fp[1] = col; }
      if (w2 > zp[2]) { zp[2] = w2; fp[2] = col; }
      if (w3 > zp[3]) { zp[3] = w3; fp[3] = col; }
    }
    for (; cnt > 0; cnt--, zp++, fp++, w += dw) {
      if (w > *zp) { *zp = w; *fp = col; }
    }
  }
}

// Мягкая тень: умножает цвет пикселей под многоугольником (k/256), глубину не пишет
static void rasterTriShadow(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, int k) {
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay), dxac = (cx - ax) * iac, dwac = (cw - aw) * iac, dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f, w = wl + dw * (xs + 0.5f - xl);
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw) {
      float zv = zb[idx];
      if (zv > 0.0f && w > zv * 0.9985f) {
        u32 c = fb[idx];
        fb[idx] = ((((c >> 16) & 255u) * (u32)k >> 8) << 16) | ((((c >> 8) & 255u) * (u32)k >> 8) << 8) | (((c & 255u) * (u32)k) >> 8);
      }
    }
  }
}
static void drawPolyView(const V3 *in, int n, u32 col, int shadowK = 0) {
  if (gDry) return;
  if (gGL) {
    if (shadowK || n < 3) return;
    bool any = false; for (int i = 0; i < n; i++) if (in[i].z > gNearZ) { any = true; break; } if (!any) return;   // целиком за камерой
    unsigned char cr = (col >> 16) & 255, cg = (col >> 8) & 255, cb = col & 255;
    for (int i = 1; i + 1 < n; i++) { const V3 *t[3] = {&in[0], &in[i], &in[i + 1]};
      for (int k = 0; k < 3; k++) { GV g; g.x = t[k]->x; g.y = t[k]->y; g.z = t[k]->z; g.r = cr; g.g = cg; g.b = cb; g.a = 255; gVerts.push_back(g); } }
    return;
  }
  V3 v[32];
  int m = 0;
  const float NZ = gNearZ;
  for (int i = 0; i < n; i++) {
    V3 a = in[i], b = in[(i + 1) % n];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) v[m++] = a;
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m++] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
    }
  }
  if (m < 3) return;
  float sx[32], sy[32], sw[32], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    sx[i] = vcx + v[i].x * focal * iz;
    sy[i] = vcy - v[i].y * focal * iz;
    sw[i] = iz;
    zs += v[i].z;
  }
  zs /= m;
  if (shadowK) {
    for (int i = 1; i + 1 < m; i++) rasterTriShadow(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], shadowK);
    return;
  }
  if (zs > fogA) col = mixc(col, fogCol, (zs - fogA) / (fogB - fogA));
  for (int i = 1; i + 1 < m; i++) rasterTri(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], col);
}

static V3 LDIR;
static float gLight = 1.0f, gNight = 0.0f;        // освещённость мира (1 - день) и «ночность» (0 день ... 1 ночь)
static float lightOf(V3 n) {
  float diff = fmaxv(0.0f, dot(n, LDIR));
  float amb = n.y < -0.4f ? 0.30f : 0.38f + 0.14f * n.y;
  return (amb + 0.62f * diff) * gLight;
}
// Глянец кодируется в старшем байте цвета (0 - матовый, 255 - зеркальный)
#define SHINE(c, sh) (((c) & 0xFFFFFFu) | ((u32)(sh) << 24))

// Грань с известной нормалью: отсечение обратных, свет, отрисовка
// Растеризатор с плавным цветом по треугольнику (освещение интерполируется между вершинами)
// вершина: [0]=x, [1]=y, [2]=1/z, [3]=R, [4]=G, [5]=B
static void rasterTriG(float *a, float *b, float *c) {
  float *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, dr = (R[2] - L[2]) * inv, dg = (R[3] - L[3]) * inv, db = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, rr = L[2] + dr * off, gg = L[3] + dg * off, bb = L[4] + db * off;
    int idx = y * IW + xs;
    {
      float n1 = (float)(xe - xs), r1 = rr + dr * n1, g1 = gg + dg * n1, b1 = bb + db * n1;
      if (rr >= 0.0f && rr <= 255.0f && r1 >= 0.0f && r1 <= 255.0f && gg >= 0.0f && gg <= 255.0f && g1 >= 0.0f && g1 <= 255.0f &&
          bb >= 0.0f && bb <= 255.0f && b1 >= 0.0f && b1 <= 255.0f) {      // быстрый путь: цвет не выходит за 0..255
        for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
          if (w > zb[idx]) { zb[idx] = w; fb[idx] = (u32)(((int)rr << 16) | ((int)gg << 8) | (int)bb); }
        }
        continue;
      }
    }
    for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
      if (w > zb[idx]) {
        zb[idx] = w;
        int ir = (int)rr, ig = (int)gg, ib = (int)bb;
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir);
        ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig);
        ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
// Многоугольник с цветами в вершинах: отсечение по ближней плоскости, туман, веер треугольников
// Треугольник с текстурой (ливрея): атрибуты [0]=x, [1]=y, [2]=1/z, [3]=u, [4]=v, [5]=освещённость; tc - цвета уровня текстуры, rx0..ry1 - рамка проекции на этом уровне
static void rasterTriT(const float *a0, const float *b0, const float *c0, const u32 *tc, int stride, int rx0, int ry0, int rx1, int ry1, float invs, float fogT) {
  const float *a = a0, *b = b0, *c = c0, *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbl = (float)(fogCol & 255);
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, du = (R[2] - L[2]) * inv, dv = (R[3] - L[3]) * inv, dl = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, u = L[2] + du * off, v = L[3] + dv * off, l = L[4] + dl * off;
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw, u += du, v += dv, l += dl) {
      if (w > zb[idx]) {
        int tx = (int)(u * invs), ty = (int)(v * invs);
        tx = tx < rx0 ? rx0 : (tx > rx1 ? rx1 : tx);
        ty = ty < ry0 ? ry0 : (ty > ry1 ? ry1 : ty);
        u32 pc = tc[ty * stride + tx];
        float lk = l < 0.0f ? 0.0f : (l > 1.3f ? 1.3f : l);
        float cr = ((pc >> 16) & 255) * lk, cg = ((pc >> 8) & 255) * lk, cb = (pc & 255) * lk;
        if (fogT > 0.0f) { cr += (fr - cr) * fogT; cg += (fg - cg) * fogT; cb += (fbl - cb) * fogT; }
        int ir = cr > 255.0f ? 255 : (int)cr, ig = cg > 255.0f ? 255 : (int)cg, ib = cb > 255.0f ? 255 : (int)cb;
        zb[idx] = w; fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
static void drawPolyViewG(const V3 *in, const float (*cin)[3], int n) {
  if (gDry) return;
  if (n == 3 && in[0].z >= 0.3f && in[1].z >= 0.3f && in[2].z >= 0.3f) {      // целый треугольник перед камерой: без отсечения
    float vt3[3][6];
    float zs3 = (in[0].z + in[1].z + in[2].z) * (1.0f / 3.0f);
    for (int i = 0; i < 3; i++) {
      float iz = 1.0f / in[i].z;
      vt3[i][0] = vcx + in[i].x * focal * iz; vt3[i][1] = vcy - in[i].y * focal * iz; vt3[i][2] = iz;
      vt3[i][3] = cin[i][0]; vt3[i][4] = cin[i][1]; vt3[i][5] = cin[i][2];
    }
    if (zs3 > fogA) {
      float t = clampf((zs3 - fogA) / (fogB - fogA), 0.0f, 1.0f);
      float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
      for (int i = 0; i < 3; i++) { vt3[i][3] += (fr - vt3[i][3]) * t; vt3[i][4] += (fg - vt3[i][4]) * t; vt3[i][5] += (fbb - vt3[i][5]) * t; }
    }
    {   // мелкий треугольник (до ~20 пикселей): плавный цвет не виден, красим одним средним цветом - в разы быстрее
      float ar2 = fabsf((vt3[1][0] - vt3[0][0]) * (vt3[2][1] - vt3[0][1]) - (vt3[2][0] - vt3[0][0]) * (vt3[1][1] - vt3[0][1]));
      if (ar2 < 40.0f) {
        int ir = (int)((vt3[0][3] + vt3[1][3] + vt3[2][3]) * (1.0f / 3.0f)), ig = (int)((vt3[0][4] + vt3[1][4] + vt3[2][4]) * (1.0f / 3.0f)),
            ib = (int)((vt3[0][5] + vt3[1][5] + vt3[2][5]) * (1.0f / 3.0f));
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir); ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig); ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        rasterTri(vt3[0][0], vt3[0][1], vt3[0][2], vt3[1][0], vt3[1][1], vt3[1][2], vt3[2][0], vt3[2][1], vt3[2][2], (u32)((ir << 16) | (ig << 8) | ib));
        return;
      }
    }
    rasterTriG(vt3[0], vt3[1], vt3[2]);
    return;
  }
  V3 v[32];
  float cv[32][3];
  int m = 0;
  const float NZ = gNearZ;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    V3 a = in[i], b = in[j];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) { v[m] = a; cv[m][0] = cin[i][0]; cv[m][1] = cin[i][1]; cv[m][2] = cin[i][2]; m++; }
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
      for (int k = 0; k < 3; k++) cv[m][k] = cin[i][k] + (cin[j][k] - cin[i][k]) * t;
      m++;
    }
  }
  if (m < 3) return;
  float vt[32][6], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    vt[i][0] = vcx + v[i].x * focal * iz;
    vt[i][1] = vcy - v[i].y * focal * iz;
    vt[i][2] = iz;
    vt[i][3] = cv[i][0]; vt[i][4] = cv[i][1]; vt[i][5] = cv[i][2];
    zs += v[i].z;
  }
  zs /= m;
  if (zs > fogA) {
    float t = clampf((zs - fogA) / (fogB - fogA), 0.0f, 1.0f);
    float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
    for (int i = 0; i < m; i++) {
      vt[i][3] += (fr - vt[i][3]) * t; vt[i][4] += (fg - vt[i][4]) * t; vt[i][5] += (fbb - vt[i][5]) * t;
    }
  }
  for (int i = 1; i + 1 < m; i++) rasterTriG(vt[0], vt[i], vt[i + 1]);
}
// цвет в вершине: освещение по нормали вершины плюс глянец (те же формулы, что у плоской грани)
// Цвет «окружения» в направлении R: светлый горизонт и голубое небо сверху, тёмная дорога снизу
static void envColor(V3 R, float *o) {
  if (R.y >= 0) {
    float t = sqrtf(fminv(1.0f, R.y));
    o[0] = 238.0f + (40.0f - 238.0f) * t; o[1] = 244.0f + (96.0f - 244.0f) * t; o[2] = 250.0f + (196.0f - 250.0f) * t;
  } else {
    float t = fminv(1.0f, -R.y * 2.2f);
    o[0] = 120.0f + (22.0f - 120.0f) * t; o[1] = 124.0f + (24.0f - 124.0f) * t; o[2] = 130.0f + (28.0f - 130.0f) * t;
  }
}
// Глянец: отражение окружения (сильнее под скользящим углом, как у лака и стекла) и яркий блик солнца. c = r, g, b (0..255)
static void glossMix(V3 p, V3 nrm, int shi, float *c) {
  V3 vd = vnorm(camEye - p), hv = vnorm(vd + LDIR);
  float nd = dot(nrm, vd), fb = 1.0f - fmaxv(0.0f, nd), fr = fb * fb * fb, g = shi * (1.0f / 255.0f);
  V3 R = nrm * (2.0f * nd) - vd;
  float e[3];
  envColor(R, e);
  float kr = clampf(g * (0.30f + 0.70f * fr), 0.0f, 0.85f);
  c[0] += (e[0] - c[0]) * kr; c[1] += (e[1] - c[1]) * kr; c[2] += (e[2] - c[2]) * kr;
  float sb = fmaxv(0.0f, dot(nrm, hv)), s2 = sb * sb, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8;
  float k2 = clampf(s16 * s8 * s4 * g, 0.0f, 0.9f);
  c[0] += (255.0f - c[0]) * k2; c[1] += (255.0f - c[1]) * k2; c[2] += (255.0f - c[2]) * k2;
}
// цвет в вершине: освещение по нормали, затемнение у земли и глянец
static void shadeVertex(V3 p, V3 nrm, u32 col, int shi, float *o) {
  float l = lightOf(nrm);
  float c[3] = {((col >> 16) & 255) * l, ((col >> 8) & 255) * l, (col & 255) * l};
  if (shi) {
    float ao = 0.52f + 0.48f * clampf(p.y * (1.0f / 1.0f), 0.0f, 1.0f);     // низ кузова темнее: машина «стоит» на земле
    c[0] *= ao; c[1] *= ao; c[2] *= ao;
    glossMix(p, nrm, shi, c);
  }
  o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
}
// то же для плоской грани: возвращает готовый цвет
static u32 shadeFlat(V3 fc, V3 nrm, u32 col, int shi, bool lit) {
  col &= 0xFFFFFFu;
  if (!shi) return lit ? shadec(col, lightOf(nrm)) : col;
  float l = lit ? lightOf(nrm) : 1.0f;
  float ao = lit ? 0.52f + 0.48f * clampf(fc.y * (1.0f / 1.0f), 0.0f, 1.0f) : 1.0f;
  float c[3] = {((col >> 16) & 255) * l * ao, ((col >> 8) & 255) * l * ao, (col & 255) * l * ao};
  glossMix(fc, nrm, shi, c);
  return RGB((int)clampf(c[0], 0.0f, 255.0f), (int)clampf(c[1], 0.0f, 255.0f), (int)clampf(c[2], 0.0f, 255.0f));
}
static void faceSmooth(const V3 *p, const V3 *nv, int n, V3 fnrm, u32 col) {
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(fnrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[8];
  float cc[8][3];
  for (int i = 0; i < n && i < 8; i++) { vv[i] = toView(p[i]); shadeVertex(p[i], nv[i], col, shi, cc[i]); }
  drawPolyViewG(vv, cc, n);
}
static void faceN(const V3 *p, int n, V3 nrm, u32 col, bool lit) {
  if (gDry) return;
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(nrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[24];
  for (int i = 0; i < n; i++) vv[i] = toView(p[i]);
  u32 c2 = shadeFlat(fc, nrm, col, shi, lit);
  drawPolyView(vv, n, c2);
}
static void quadFace(V3 c, V3 u, V3 v, V3 n, u32 col, bool lit) {
  V3 p[4];
  p[0] = c - u - v; p[1] = c + u - v; p[2] = c + u + v; p[3] = c - u + v;
  faceN(p, 4, n, col, lit);
}
// Параллелепипед: центр, полуразмеры, поворот вокруг Y
static void drawBox(V3 c, V3 h, float yaw, u32 col, bool lit) {
  if (gDry) return;
  float cs = cosf(yaw), sn = sinf(yaw);
  V3 ax = v3(cs, 0, -sn), ay = v3(0, 1, 0), az = v3(sn, 0, cs);
  V3 ex = ax * h.x, ey = ay * h.y, ez = az * h.z;
  quadFace(c + ey, ex, ez, ay, col, lit);
  quadFace(c + ex, ey, ez, ax, col, lit);
  quadFace(c - ex, ey, ez, ax * -1.0f, col, lit);
  quadFace(c + ez, ex, ey, az, col, lit);
  quadFace(c - ez, ex, ey, az * -1.0f, col, lit);
}
// Призма (колесо) вокруг оси X кузова
static void drawWheel(V3 c, V3 ax, V3 ay, V3 az, float r, float hw, u32 col) {
  // мелкие детали (фары, колпаки) рисуются с меньшим числом граней - на картинке не видно, а полигонов втрое меньше
  int N = r < 0.13f ? imin(wheelN, 8) : (r < 0.25f ? imin(wheelN, 12) : wheelN);
  static float CT[25][24], ST[25][24];
  static bool tabOk[25];
  if (N < 6) N = 6;
  if (!tabOk[N]) {
    for (int i = 0; i < N; i++) { float t = 6.2831853f * i / N; CT[N][i] = cosf(t); ST[N][i] = sinf(t); }
    tabOk[N] = true;
  }
  V3 ring[24];
  for (int i = 0; i < N; i++) ring[i] = ay * (r * CT[N][i]) + az * (r * ST[N][i]);
  for (int i = 0; i < N; i++) {
    int j = (i + 1) % N;
    V3 p[4];
    p[0] = c + ring[i] - ax * hw; p[1] = c + ring[j] - ax * hw;
    p[2] = c + ring[j] + ax * hw; p[3] = c + ring[i] + ax * hw;
    V3 mid = vnorm(ring[i] + ring[j]);
    faceN(p, 4, mid, col, true);
  }
  V3 cap[24];
  for (int s = -1; s <= 1; s += 2) {
    for (int i = 0; i < N; i++) cap[i] = c + ring[i] + ax * (hw * s);
    faceN(cap, N, ax * (float)s, col, true);
  }
}

static void setClipRect(int x0, int y0, int x1, int y1) { clipX0 = x0; clipY0 = y0; clipX1 = x1; clipY1 = y1; }
static void clearDepth() {
  if (gGL) return;
  if (clipX0 == 0 && clipX1 == IW) { memset(zb + clipY0 * IW, 0, sizeof(float) * (size_t)(clipY1 - clipY0) * IW); return; }
  for (int y = clipY0; y < clipY1; y++) { float *r = zb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = 0; }
}
static void fillSky() {
  if (gGL) { gSkyCol = fogCol; return; }
  float horizon = vcy + focal * camF.y / sqrtf(fmaxv(0.05f, 1 - camF.y * camF.y));
  u32 top = mixc(RGB(60, 70, 110), RGB(6, 8, 26), gNight);
  for (int y = clipY0; y < clipY1; y++) {
    u32 c = y < horizon ? mixc(top, fogCol, clampf((y - clipY0) / fmaxv(1.0f, horizon - clipY0), 0, 1)) : fogCol;
    u32 *r = fb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = c;
  }
}
static void groundQuad(float x0, float z0, float x1, float z1, float y, u32 col) {
  if (gLight < 0.999f) col = RGB((int)(CR(col) * gLight), (int)(CG(col) * gLight), (int)(CB(col) * gLight));
  V3 p[4] = {v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1)};
  V3 vv[4]; for (int i = 0; i < 4; i++) vv[i] = toView(p[i]);
  drawPolyView(vv, 4, col);
}
static void discOct(float cx, float cz, float r, float y, u32 col) {
  V3 vv[8];
  for (int i = 0; i < 8; i++) { float t = 6.2831853f * i / 8 + 0.3927f; vv[i] = toView(v3(cx + cosf(t) * r, y, cz + sinf(t) * r)); }
  drawPolyView(vv, 8, col);
}
static bool hitR(int x, int y, int w, int h) {
  if (!P.clicked) return false;
  if (P.cx >= x && P.cx < x + w && P.cy >= y && P.cy < y + h && P.sx >= x && P.sx < x + w && P.sy >= y && P.sy < y + h) { P.clicked = false; return true; }
  return false;
}
static bool button(int x, int y, int w, int h, const char *label, bool en, u32 col) {
  u32 c = en ? col : RGB(58, 60, 70);
  fillRect(x, y, w, h, shadec(c, 0.55f));
  fillRect(x + 1, y + 1, w - 2, h - 2, c);
  fillRect(x + 1, y + 1, w - 2, imax(1, h / 8), shadec(c, 1.25f));
  char lines[3][64]; int n = 0; const char *p = label;
  while (n < 3) {
    const char *e = strchr(p, '\n'); int len = e ? (int)(e - p) : (int)strlen(p); if (len > 63) len = 63;
    memcpy(lines[n], p, len); lines[n][len] = 0; n++; if (!e) break; p = e + 1;
  }
  int sc = fs; while (sc > 1 && textW(lines[0], sc) > w - 4) sc--;
  int lh = 9 * sc, y0 = y + (h - n * lh + 2 * sc) / 2;
  for (int i = 0; i < n; i++) drawText(x + w / 2, y0 + i * lh, lines[i], sc, en ? RGB(255, 255, 255) : RGB(140, 142, 152), 1);
  return en && hitR(x, y, w, h);
}
static void drawLine(int x0, int y0, int x1, int y1, u32 c, int th) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy, n = 0;
  while (n++ < 4000) {
    fillRect(x0 - th / 2, y0 - th / 2, th, th, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err; if (e2 > -dy) { err -= dy; x0 += sx; } if (e2 < dx) { err += dx; y0 += sy; }
  }
}

// =====================================================================
//  БИТВА БАШЕН: карты, юниты, арены, сундуки, задания, профиль игрока
// =====================================================================
#include <algorithm>
#include <string>
static unsigned hsh(unsigned x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

// ---- арена: 18 клеток в ширину, 32 в длину, река посередине ----
static const float AW = 18.0f, AL = 32.0f, RIVER_Z = 16.0f, RIVER_HW = 1.0f;
static const float BRIDGE_X[2] = {3.5f, 14.5f}; static const float BRIDGE_HW = 1.1f;

// ---- модели (как рисовать) ----
enum { M_KNIGHT, M_ARCHER, M_GOBLIN, M_GIANT, M_FIREMAGE, M_DRAGON, M_SKELETON, M_XBOW, M_BULL, M_GOLEM, M_NECRO, M_LANCER, M_BAT, M_BOMBER, M_GOBKING, M_DART, M_VOLAR, M_PUP, M_RECRUIT, M_ICEWIZ, M_BARB, M_EBARB, M_HOG, M_MEGA, M_STRONG, M_CGOLEM, M_FSPIRIT, M_ISPIRIT,
       M_CANNON, M_ATOWER, M_HUT, M_INFERNO, M_COLLECTOR, M_FURNACE, M_PRINCESS, M_KING, NMODEL };

// ---- юниты: войска, здания и башни ----
// hp, урон - на 1 уровне; hitT - время между ударами; speed - клеток в секунду; range - дальность удара (от края до края);
// sight - на каком расстоянии замечает врага; splash - радиус урона по площади; projV - скорость снаряда (0 - удар вблизи)
enum { UT_TROOP, UT_BUILD, UT_TOWER };
struct UnitT { const char *name; int model, kind; float hp, dmg, hitT, speed, range, radius, sight, splash, projV; bool air, onlyBuild, canAir; float mass, lifeT; int spawnU, spawnN; float spawnT, deathDmg, deathR; int deathU, deathN; float shield; };   // deathN > 0: при гибели распадается на deathN юнитов deathU
enum { U_KNIGHT, U_ARCHER, U_GOBLIN, U_GIANT, U_FIREMAGE, U_DRAGON, U_SKELETON, U_XBOW, U_BULL, U_GOLEM, U_NECRO, U_LANCER, U_BAT, U_BOMBER, U_GOBKING, U_DART, U_VOLAR, U_PUP, U_RECRUIT, U_ICEWIZ, U_BARB, U_EBARB, U_HOG, U_MEGA, U_STRONG, U_CGOLEM, U_FSPIRIT, U_ISPIRIT,
       U_CANNON, U_ATOWER, U_HUT, U_INFERNO, U_COLLECTOR, U_FURNACE, U_PRINCESS, U_KING, NUNIT };
static UnitT UNITS[NUNIT] = {
  //  имя                модель      вид       hp     урон  удар  скор  дальн радиус обзор  сплеш снаряд возд  здан  по возд масса жизнь
  {"РЫЦАРЬ",            M_KNIGHT,   UT_TROOP, 1400,  160, 1.2f, 1.0f, 0.6f, 0.45f, 5.5f, 0,    0,     false, false, false, 6, 0, -1, 0, 0, 0, 0},
  {"ЛУЧНИЦА",           M_ARCHER,   UT_TROOP, 250,   93.5f,  1.0f, 1.0f, 5.0f, 0.35f, 5.5f, 0,    14,    false, false, true,  3, 0, -1, 0, 0, 0, 0},
  {"ГОБЛИН",            M_GOBLIN,   UT_TROOP, 170,   100, 1.1f, 1.6f, 0.5f, 0.35f, 5.5f, 0,    0,     false, false, false, 2, 0, -1, 0, 0, 0, 0},
  {"ВЕЛИКАН",           M_GIANT,    UT_TROOP, 3300,  210, 1.5f, 0.75f,0.7f, 0.75f, 7.5f, 0,    0,     false, true,  false, 18, 0, -1, 0, 0, 0, 0},
  {"МАГ ОГНЯ",          M_FIREMAGE, UT_TROOP, 550,   210, 1.4f, 1.0f, 5.5f, 0.4f,  5.5f, 1.5f, 9,     false, false, true,  4, 0, -1, 0, 0, 0, 0},
  {"ДРАКОНЧИК",         M_DRAGON,   UT_TROOP, 1272.5f,148.5f, 1.6f, 1.33f,3.5f, 0.6f,  5.5f, 1.2f, 10,    true,  false, true,  5, 0, -1, 0, 0, 0, 0},
  {"СКЕЛЕТ",            M_SKELETON, UT_TROOP, 80,    80,  1.0f, 1.33f,0.5f, 0.3f,  5.5f, 0,    0,     false, false, false, 1, 0, -1, 0, 0, 0, 0},
  {"АРБАЛЕТЧИК",        M_XBOW,     UT_TROOP, 600,   180, 1.1f, 1.0f, 6.0f, 0.4f,  6.5f, 0,    16,    false, false, true,  4, 0, -1, 0, 0, 0, 0},
  {"БОЕВОЙ БЫК",        M_BULL,     UT_TROOP, 1450,  260, 1.6f, 2.0f, 0.6f, 0.6f,  9.5f, 0,    0,     false, true,  false, 8, 0, -1, 0, 0, 0, 0},
  {"КАМЕННЫЙ ГОЛЕМ",    M_GOLEM,    UT_TROOP, 6000,  280, 2.5f, 0.6f, 0.8f, 1.0f,  7.5f, 0,    0,     false, true,  false, 40, 0, -1, 0, 0, 260, 2.0f},
  {"НЕКРОМАНТ",         M_NECRO,    UT_TROOP, 700,   90,  1.0f, 1.0f, 5.0f, 0.4f,  5.5f, 1.2f, 10,    false, false, true,  4, 0, U_SKELETON, 4, 7.0f, 0, 0},
  {"КОПЕЙЩИК",          M_LANCER,   UT_TROOP, 1300,  250, 1.5f, 1.0f, 1.4f, 0.5f,  5.5f, 0,    0,     false, false, false, 7, 0, -1, 0, 0, 0, 0},
  {"ЛЕТУЧАЯ МЫШЬ",      M_BAT,      UT_TROOP, 70,    70,  1.1f, 2.0f, 0.5f, 0.3f,  5.5f, 0,    0,     true,  false, true,  1, 0, -1, 0, 0, 0, 0},
  {"ГНОМ-ПОДРЫВНИК",    M_BOMBER,   UT_TROOP, 300,   187, 1.8f, 1.0f, 4.5f, 0.35f, 5.0f, 1.5f, 7,     false, false, false, 3, 0, -1, 0, 0, 0, 0},
  {"КОРОЛЬ ГОБЛИНОВ",   M_GOBKING,  UT_TROOP, 2600,  254.5f,1.8f, 0.75f,0.8f, 0.7f,  7.5f, 0,    0,     false, true,  false, 14, 0, U_GOBLIN, 4, 5.0f, 0, 0},
  {"ГОБЛИН-ДУДЕЦ",      M_DART,     UT_TROOP, 250,   100, 0.7f, 1.4f, 6.0f, 0.35f, 6.5f, 0,    18,    false, false, true,  2, 0, -1, 0, 0, 0, 0},
  {"ВОЛАР БЕБЕ",        M_VOLAR,    UT_TROOP, 2757,  148.5f,1.8f, 0.5f, 2.0f, 0.8f,  7.5f, 0,    9,     true,  true,  false, 16, 0, -1, 0, 0, 0, 0, U_PUP, 6},
  {"ЛЕТАЮЩИЙ ЩЕНОК",    M_PUP,      UT_TROOP, 200,   50,  1.2f, 1.0f, 1.6f, 0.35f, 5.5f, 0,    10,    true,  false, true,  2, 0, -1, 0, 0, 0, 0},
  {"НОВОБРАНЕЦ",        M_RECRUIT,  UT_TROOP, 307.5f,74.7f,1.3f, 1.0f, 1.3f, 0.4f,  5.5f, 0,    0,     false, false, false, 5, 0, -1, 0, 0, 0, 0, -1, 0, 184.5f},   // щит = 60% здоровья
  {"ЛЕДЯНОЙ МАГ",       M_ICEWIZ,   UT_TROOP, 320.2f,41.6f,1.7f, 1.0f, 5.5f, 0.4f,  6.0f, 1.0f, 10,    false, false, true,  4, 0, -1, 0, 0, 0, 0},
  {"ВАРВАР",            M_BARB,     UT_TROOP, 369.5f,101.9f,1.3f, 1.0f, 0.7f, 0.42f, 5.5f, 0,    0,     false, false, false, 5, 0, -1, 0, 0, 0, 0},
  {"ЭЛИТНЫЙ ВАРВАР",    M_EBARB,    UT_TROOP, 576.4f,165.0f,1.4f,1.5f, 1.2f, 0.45f, 5.5f, 0,    0,     false, false, false, 6, 0, -1, 0, 0, 0, 0},
  {"ВСАДНИК НА КАБАНЕ", M_HOG,      UT_TROOP, 933.1f,134.4f,1.6f,2.0f, 0.8f, 0.55f, 9.5f, 0,    0,     false, true,  false, 7, 0, -1, 0, 0, 0, 0},
  {"МЕГАРЫЦАРЬ",        M_MEGA,     UT_TROOP, 1693.5f,113.7f,1.7f,1.0f,1.2f, 0.75f, 6.0f, 1.3f, 0,     false, false, false, 22, 0, -1, 0, 0, 0, 0},
  {"СИЛАЧ",             M_STRONG,   UT_TROOP, 1272.5f,148.5f,1.3f,1.0f, 1.0f, 0.6f,  5.5f, 0,    0,     false, false, false, 14, 0, -1, 0, 0, 0, 0},   // бьёт полосой 2x6 перед собой
  {"МЕДНЫЙ ГОЛЕМ",      M_CGOLEM,   UT_TROOP, 1272.5f,212.05f,1.5f,0.8f, 1.1f, 0.85f, 6.0f, 0,    0,     false, false, false, 30, 0, -1, 0, 0, 0, 0},   // бьёт одну цель или по площади, притягивает врагов
  {"ОГНЕННЫЙ ДУХ",      M_FSPIRIT,  UT_TROOP, 700,   190, 0.5f, 2.0f, 0.9f, 0.32f, 6.0f, 0,    0,     false, false, true,  2, 0, -1, 0, 0, 0, 0},   // подбегает, взрывается огнём; здоровья хватает дойти до башни
  {"ЛЕДЯНОЙ ДУХ",       M_ISPIRIT,  UT_TROOP, 700,   95,  0.5f, 1.8f, 0.9f, 0.32f, 6.0f, 0,    0,     false, false, true,  2, 0, -1, 0, 0, 0, 0},   // подбегает, взрывается льдом и замораживает
  {"ПУШКА",             M_CANNON,   UT_BUILD, 800,   150, 0.8f, 0,    5.5f, 0.6f,  5.5f, 0,    18,    false, false, false, 99, 30, -1, 0, 0, 0, 0},
  {"БАШНЯ ЛУЧНИКОВ",    M_ATOWER,   UT_BUILD, 1100,  100, 0.9f, 0,    7.0f, 0.7f,  7.0f, 0,    16,    false, false, true,  99, 40, -1, 0, 0, 0, 0},
  {"ГОБЛИНСКАЯ ХИЖИНА", M_HUT,      UT_BUILD, 900,   0,   1.0f, 0,    0,    0.8f,  0,    0,    0,     false, false, false, 99, 40, U_GOBLIN, 1, 5.0f, 0, 0},
  {"ИНФЕРНО",            M_INFERNO,  UT_BUILD, 1400,  20,  0.4f, 0,    6.0f, 0.65f, 6.0f, 0,    0,     false, false, true,  99, 30, -1, 0, 0, 0, 0},   // луч: урон нарастает, пока бьёт одну цель
  {"СБОРНИК ЭЛИКСИРА",  M_COLLECTOR,UT_BUILD, 1000,  0,   1.0f, 0,    0,    0.7f,  0,    0,    0,     false, false, false, 99, 70, -1, 0, 0, 0, 0},   // даёт эликсир тому, кто поставил
  {"ПЕЧЬ",              M_FURNACE,  UT_BUILD, 800,   0,   1.0f, 0,    0,    0.75f, 0,    0,    0,     false, false, false, 99, 50, U_FSPIRIT, 1, 4.0f, 0, 0},   // каждые 4 с выпускает огненного духа
  {"БАШНЯ ПРИНЦЕССЫ",   M_PRINCESS, UT_TOWER, 2500,  90,  0.8f, 0,    7.5f, 1.4f,  7.5f, 0,    16,    false, false, true,  999, 0, -1, 0, 0, 0, 0},
  {"КОРОЛЕВСКАЯ БАШНЯ", M_KING,     UT_TOWER, 4000,  110, 1.0f, 0,    7.0f, 1.9f,  7.0f, 0,    14,    false, false, true,  999, 0, -1, 0, 0, 0, 0},
};

// ---- карты ----
enum { CT_TROOP, CT_BUILD, CT_SPELL };
enum { RA_COMMON, RA_RARE, RA_EPIC, RA_LEGEND, NRAR };
static const char *RARN[NRAR] = {"ОБЫЧНАЯ", "РЕДКАЯ", "ЭПИЧЕСКАЯ", "ЛЕГЕНДАРНАЯ"};
static const u32 RARC[NRAR] = {RGB(110, 160, 220), RGB(240, 150, 50), RGB(180, 80, 230), RGB(80, 220, 210)};
enum { SP_NONE, SP_FIREBALL, SP_ARROWS, SP_FREEZE, SP_HEAL, SP_ROCKET, SP_ZAP, SP_BARREL, SP_MAGEFALL };   // SP_MAGEFALL - огненный шар, из которого появляется маг огня
struct Card { const char *name; int cost, type, rarity, unit, count, arena; int spell; float sR, sDmg, sTowerK, sDelay, sTime; const char *desc; };
enum { C_KNIGHT, C_ARCHERS, C_GOBLINS, C_GIANT, C_FIREBALL, C_ARROWS, C_SKELETONS, C_CANNON,
       C_LANCER, C_BATS, C_ZAP, C_NECRO, C_BOMBER, C_ATOWER, C_FREEZE, C_DRAGON, C_XBOW, C_FIREMAGE, C_ROCKET, C_HUT, C_GOLEM, C_BULL, C_HEAL, C_GOBKING, C_DART, C_VOLAR, C_SKARMY, C_RECRUITS, C_ICEWIZ, C_BARREL, C_BARBS, C_EBARBS, C_HOG, C_MEGA, C_STRONG, C_CGOLEM, C_INFERNO, C_COLLECTOR, C_FSPIRIT, C_ISPIRIT, C_FURNACE, NCARD };
static Card CARDS[NCARD] = {
  {"РЫЦАРЬ",            3, CT_TROOP, RA_COMMON, U_KNIGHT,   1, 0, SP_NONE, 0, 0, 0, 0, 0, "КРЕПКИЙ БОЕЦ С МЕЧОМ. ДЕРЖИТ УДАР И БЬЁТ ВБЛИЗИ."},
  {"ЛУЧНИЦЫ",           3, CT_TROOP, RA_COMMON, U_ARCHER,   2, 0, SP_NONE, 0, 0, 0, 0, 0, "ДВЕ ЛУЧНИЦЫ. СТРЕЛЯЮТ ИЗДАЛЕКА, ДОСТАЮТ ЛЕТАЮЩИХ."},
  {"ГОБЛИНЫ",           2, CT_TROOP, RA_COMMON, U_GOBLIN,   4, 0, SP_NONE, 0, 0, 0, 0, 0, "ЧЕТВЕРО БЫСТРЫХ ГОБЛИНОВ С КИНЖАЛАМИ."},
  {"ВЕЛИКАН",           4, CT_TROOP, RA_RARE,   U_GIANT,    1, 0, SP_NONE, 0, 0, 0, 0, 0, "ОЧЕНЬ МНОГО ЗДОРОВЬЯ. ИДЁТ ТОЛЬКО К ЗДАНИЯМ И БАШНЯМ."},
  {"ОГНЕННЫЙ ШАР",      4, CT_SPELL, RA_RARE,   -1,         0, 0, SP_FIREBALL, 2.5f, 600, 0.35f, 0.7f, 0, "ВЗРЫВ ПО ПЛОЩАДИ. ОТБРАСЫВАЕТ ЛЁГКИХ."},
  {"СТРЕЛЫ",            3, CT_SPELL, RA_COMMON, -1,         0, 0, SP_ARROWS, 4.0f, 250, 0.35f, 0.9f, 0, "ДОЖДЬ СТРЕЛ НА БОЛЬШУЮ ПЛОЩАДЬ. ПРОТИВ ТОЛПЫ."},
  {"СКЕЛЕТЫ",           1, CT_TROOP, RA_COMMON, U_SKELETON, 4, 0, SP_NONE, 0, 0, 0, 0, 0, "ЧЕТЫРЕ ДЕШЁВЫХ СКЕЛЕТА. ОТВЛЕКАЮТ ВРАГА."},
  {"ПУШКА",             3, CT_BUILD, RA_COMMON, U_CANNON,   1, 0, SP_NONE, 0, 0, 0, 0, 0, "ЗДАНИЕ. СТРЕЛЯЕТ ПО НАЗЕМНЫМ, ОТВЛЕКАЕТ ВЕЛИКАНОВ."},
  {"КОПЕЙЩИК",          4, CT_TROOP, RA_RARE,   U_LANCER,   1, 1, SP_NONE, 0, 0, 0, 0, 0, "РАЗГОНЯЕТСЯ И ПЕРВЫМ УДАРОМ БЬЁТ ВДВОЕ СИЛЬНЕЕ."},
  {"ЛЕТУЧИЕ МЫШИ",      2, CT_TROOP, RA_COMMON, U_BAT,      4, 1, SP_NONE, 0, 0, 0, 0, 0, "СТАЯ ИЗ ЧЕТЫРЁХ. ЛЕТАЮТ, БЫСТРЫЕ, НО ХРУПКИЕ."},
  {"РАЗРЯД",            2, CT_SPELL, RA_RARE,   -1,         0, 1, SP_ZAP, 2.5f, 160, 0.35f, 0, 0, "МГНОВЕННАЯ МОЛНИЯ. ОГЛУШАЕТ НА ПОЛСЕКУНДЫ."},
  {"НЕКРОМАНТ",         5, CT_TROOP, RA_EPIC,   U_NECRO,    1, 2, SP_NONE, 0, 0, 0, 0, 0, "ПОДНИМАЕТ ПО 4 СКЕЛЕТА. ТЁМНАЯ МАГИЯ БЬЁТ ПО ПЛОЩАДИ."},
  {"ГНОМ-ПОДРЫВНИК",    3, CT_TROOP, RA_COMMON, U_BOMBER,   1, 2, SP_NONE, 0, 0, 0, 0, 0, "КИДАЕТ БОМБЫ ПО ТОЛПЕ. ЛЕТАЮЩИХ НЕ ДОСТАЁТ."},
  {"БАШНЯ ЛУЧНИКОВ",    4, CT_BUILD, RA_RARE,   U_ATOWER,   1, 2, SP_NONE, 0, 0, 0, 0, 0, "ЗДАНИЕ. ДАЛЕКО СТРЕЛЯЕТ ПО ВСЕМ, И ПО ЛЕТАЮЩИМ."},
  {"ЗАМОРОЗКА",         4, CT_SPELL, RA_EPIC,   -1,         0, 3, SP_FREEZE, 3.0f, 90, 1.0f, 0, 4.0f, "ВСЁ В КРУГУ ЗАМИРАЕТ НА 4 СЕКУНДЫ."},
  {"ДРАКОНЧИК",         4, CT_TROOP, RA_EPIC,   U_DRAGON,   1, 3, SP_NONE, 0, 0, 0, 0, 0, "ЛЕТАЕТ И ДЫШИТ ОГНЁМ ПО ПЛОЩАДИ."},
  {"АРБАЛЕТЧИК",        4, CT_TROOP, RA_RARE,   U_XBOW,     1, 3, SP_NONE, 0, 0, 0, 0, 0, "МЕТКИЙ И ДАЛЬНОБОЙНЫЙ. СИЛЬНЫЙ УРОН ПО ОДНОЙ ЦЕЛИ."},
  {"МАГ ОГНЯ",          4, CT_TROOP, RA_LEGEND, U_FIREMAGE, 1, 4, SP_NONE, 0, 0, 0, 0, 0, "ОГНЕННЫЕ ШАРЫ ПО ПЛОЩАДИ. ПРОТИВ ТОЛПЫ И ЛЕТАЮЩИХ."},
  {"РАКЕТА",            6, CT_SPELL, RA_LEGEND, -1,         0, 4, SP_ROCKET, 2.0f, 2025, 0.2f, 1.0f, 0, "ЛЕТИТ С КОРОЛЕВСКОЙ БАШНИ. ОГРОМНЫЙ УРОН В НЕБОЛЬШОМ КРУГЕ."},
  {"ГОБЛИНСКАЯ ХИЖИНА", 5, CT_BUILD, RA_RARE,   U_HUT,      1, 4, SP_NONE, 0, 0, 0, 0, 0, "ЗДАНИЕ. КАЖДЫЕ 5 СЕКУНД ВЫПУСКАЕТ ГОБЛИНА."},
  {"КАМЕННЫЙ ГОЛЕМ",    8, CT_TROOP, RA_EPIC,   U_GOLEM,    1, 5, SP_NONE, 0, 0, 0, 0, 0, "САМЫЙ КРЕПКИЙ. ИДЁТ К ЗДАНИЯМ, ПРИ ГИБЕЛИ ВЗРЫВАЕТСЯ."},
  {"БОЕВОЙ БЫК",        4, CT_TROOP, RA_RARE,   U_BULL,     1, 5, SP_NONE, 0, 0, 0, 0, 0, "БЫСТРО НЕСЁТСЯ К БАШНЯМ, ПЕРЕПРЫГИВАЕТ РЕКУ."},
  {"ЛЕЧЕНИЕ",           3, CT_SPELL, RA_RARE,   -1,         0, 5, SP_HEAL, 3.0f, 450, 0, 0, 2.0f, "ЛЕЧИТ СВОИХ В КРУГУ ЗА 2 СЕКУНДЫ."},
  {"КОРОЛЬ ГОБЛИНОВ",   7, CT_TROOP, RA_LEGEND, U_GOBKING,  1, 1, SP_NONE, 0, 0, 0, 0, 0, "ИДЁТ ТОЛЬКО К ЗДАНИЯМ И БАШНЯМ. КАЖДЫЕ 5 СЕКУНД ПРИЗЫВАЕТ 4 ГОБЛИНОВ."},
  {"ГОБЛИН-ДУДЕЦ",      3, CT_TROOP, RA_RARE,   U_DART,     1, 1, SP_NONE, 0, 0, 0, 0, 0, "ПЛЮЁТСЯ ИЗ ДУДКИ ОЧЕНЬ БЫСТРО И ДАЛЕКО. ДОСТАЁТ ЛЕТАЮЩИХ."},
  {"ВОЛАР БЕБЕ",        7, CT_TROOP, RA_LEGEND, U_VOLAR,    1, 4, SP_NONE, 0, 0, 0, 0, 0, "ЛЕТАЕТ И АТАКУЕТ ТОЛЬКО ЗДАНИЯ. ПРИ ГИБЕЛИ - 6 ЩЕНКОВ, ОНИ БЬЮТ ВСЕХ."},
  {"АРМИЯ СКЕЛЕТОВ",    3, CT_TROOP, RA_EPIC,   U_SKELETON, 17, 0, SP_NONE, 0, 0, 0, 0, 0, "ЦЕЛАЯ ТОЛПА: 17 СКЕЛЕТОВ СРАЗУ. ОКРУЖАЕТ И РАЗБИРАЕТ ОДИНОЧНЫХ ВРАГОВ, НО БОИТСЯ СТРЕЛ И ЗАЖИГАЛОК."},
  {"КОРОЛЕВСКИЕ НОВОБРАНЦЫ", 7, CT_TROOP, RA_COMMON, U_RECRUIT, 6, 2, SP_NONE, 0, 0, 0, 0, 0, "ШЕСТЬ ВОИНОВ С КОПЬЯМИ И ЩИТАМИ ВСТАЮТ ЦЕПЬЮ НА ВСЮ ШИРИНУ АРЕНЫ. ЩИТ ПРИНИМАЕТ УДАРЫ ПЕРВЫМ."},
  {"ЛЕДЯНОЙ МАГ",       3, CT_TROOP, RA_LEGEND, U_ICEWIZ,   1, 3, SP_NONE, 0, 0, 0, 0, 0, "ПРИ ПОЯВЛЕНИИ ЗАМОРАЖИВАЕТ ВСЁ ВОКРУГ НА 2 С И БЬЁТ ХОЛОДОМ. ЕГО ЛЕДЯНЫЕ ШАРЫ ЗАМЕДЛЯЮТ ВРАГОВ."},
  {"БОЧКА С ГОБЛИНАМИ", 3, CT_SPELL, RA_EPIC,   U_GOBLIN,   3, 1, SP_BARREL, 1.5f, 0, 0, 0, 0, "КОРОЛЬ БРОСАЕТ БОЧКУ КУДА УГОДНО - ДАЖЕ К ВРАЖЕСКОЙ БАШНЕ. ИЗ НЕЁ ВЫПРЫГИВАЮТ 3 ГОБЛИНА."},
  {"ВАРВАРЫ",           5, CT_TROOP, RA_COMMON, U_BARB,     5, 2, SP_NONE, 0, 0, 0, 0, 0, "ПЯТЬ КРЕПКИХ БОЙЦОВ С МЕЧАМИ. СИЛЬНЫ ПРОТИВ ТАНКОВ, НО БОЯТСЯ УРОНА ПО ПЛОЩАДИ."},
  {"ЭЛИТНЫЕ ВАРВАРЫ",   6, CT_TROOP, RA_COMMON, U_EBARB,    2, 3, SP_NONE, 0, 0, 0, 0, 0, "ДВА БЫСТРЫХ И ЗЛЫХ ВАРВАРА. БЕГУТ К БАШНЕ И БЬЮТ ОЧЕНЬ БОЛЬНО."},
  {"ВСАДНИК НА КАБАНЕ", 4, CT_TROOP, RA_RARE,   U_HOG,      1, 2, SP_NONE, 0, 0, 0, 0, 0, "ОЧЕНЬ БЫСТРО НЕСЁТСЯ К ЗДАНИЯМ И БАШНЯМ, ПЕРЕПРЫГИВАЕТ РЕКУ."},
  {"МЕГАРЫЦАРЬ",        7, CT_TROOP, RA_LEGEND, U_MEGA,     1, 4, SP_NONE, 0, 0, 0, 0, 0, "ОГРОМНЫЙ ЛАТНИК. ПРИ ПОЯВЛЕНИИ БЬЁТ ВСЕХ ВОКРУГ, ПРЫГАЕТ НА БОЙЦОВ ИЗДАЛЕКА И БЬЁТ ВСЕХ ВОКРУГ НА 68% СВОЕГО УДАРА."},
  {"СИЛАЧ",             5, CT_TROOP, RA_EPIC,   U_STRONG,   1, 1, SP_NONE, 0, 0, 0, 0, 0, "БЬЁТ КУЛАКАМИ О ЗЕМЛЮ: УДАР ПРОХОДИТ ПОЛОСОЙ 2 КЛЕТКИ В ШИРИНУ И 6 В ДЛИНУ ПЕРЕД НИМ."},
  {"МЕДНЫЙ ГОЛЕМ",      5, CT_TROOP, RA_LEGEND, U_CGOLEM,   1, 2, SP_NONE, 0, 0, 0, 0, 0, "ЦЕПЬЮ ПРИТЯГИВАЕТ ВРАГОВ К СЕБЕ. ОДНОГО БЬЁТ СИЛЬНО, А ТОЛПУ - УДАРОМ ПО ПЛОЩАДИ: КАЖДОМУ СЛЕДУЮЩЕМУ УРОН +10%."},
  {"ИНФЕРНО",           5, CT_BUILD, RA_RARE,   U_INFERNO,  1, 2, SP_NONE, 0, 0, 0, 0, 0, "ЗДАНИЕ. ЛУЧ БЬЁТ ОДНУ ЦЕЛЬ, И ЧЕМ ДОЛЬШЕ - ТЕМ БОЛЬНЕЕ. БЬЁТ И ПО ЛЕТАЮЩИМ. СМЕНА ЦЕЛИ СБРАСЫВАЕТ РАЗОГРЕВ."},
  {"СБОРНИК ЭЛИКСИРА",  6, CT_BUILD, RA_RARE,   U_COLLECTOR,1, 1, SP_NONE, 0, 0, 0, 0, 0, "ЗДАНИЕ. КАЖДЫЕ 9 СЕКУНД ДАЁТ ТЕБЕ 1 ЭЛИКСИР, А ПРИ ГИБЕЛИ ЕЩЁ 1. ВРАГИ ЛОМАЮТ ЕГО, ПОКА ОН НЕ ОКУПИЛСЯ."},
  {"ОГНЕННЫЙ ДУХ",      1, CT_TROOP, RA_COMMON, U_FSPIRIT,  1, 1, SP_NONE, 0, 0, 0, 0, 0, "БЫСТРЫЙ ДУХ. БЕЖИТ К БАШНЕ, ПО ДОРОГЕ ТЕРПИТ УДАРЫ, А У ЦЕЛИ ВЗРЫВАЕТСЯ ОГНЁМ ПО ПЛОЩАДИ. БЬЁТ И ЛЕТАЮЩИХ."},
  {"ЛЕДЯНОЙ ДУХ",       1, CT_TROOP, RA_COMMON, U_ISPIRIT,  1, 1, SP_NONE, 0, 0, 0, 0, 0, "ДУХ-СНЕЖОК. ДОБЕГАЕТ ДО ЦЕЛИ, ВЗРЫВАЕТСЯ И ЗАМОРАЖИВАЕТ ВСЕХ ВОКРУГ. БЬЁТ И ЛЕТАЮЩИХ."},
  {"ПЕЧЬ",              5, CT_BUILD, RA_RARE,   U_FURNACE,  1, 2, SP_NONE, 0, 0, 0, 0, 0, "ЗДАНИЕ. КАЖДЫЕ 4 СЕКУНДЫ ВЫПУСКАЕТ ОГНЕННОГО ДУХА, КОТОРЫЙ БЕЖИТ К БАШНЕ И ВЗРЫВАЕТСЯ. ЖИВЁТ 50 СЕКУНД."},
};
static const int START_DECK[8] = {C_KNIGHT, C_ARCHERS, C_GOBLINS, C_GIANT, C_FIREBALL, C_ARROWS, C_SKELETONS, C_CANNON};

// ---- арены ----
struct Arena { const char *name; int trophies; u32 grass, grass2, path, border, deco; int decoKind; };   // decoKind: 0 ёлки, 1 сосны, 2 кости, 3 лёд, 4 лава, 5 замок, 6 легендарная
static const Arena ARENAS[] = {
  {"ТРЕНИРОВОЧНЫЙ ЛАГЕРЬ", 0,    RGB(96, 160, 70),  RGB(88, 150, 64),  RGB(196, 170, 120), RGB(120, 96, 70),  RGB(60, 120, 50), 0},
  {"ГОБЛИНСКИЙ ЛЕС",       300,  RGB(70, 140, 60),  RGB(62, 128, 54),  RGB(150, 120, 80),  RGB(90, 70, 50),   RGB(40, 100, 45), 1},
  {"КОСТЯНАЯ ЯМА",         600,  RGB(140, 130, 100),RGB(128, 120, 92), RGB(110, 100, 86),  RGB(80, 70, 60),   RGB(220, 215, 200), 2},
  {"ЛЕДЯНОЙ ПИК",          1000, RGB(200, 220, 235),RGB(186, 208, 226),RGB(150, 180, 205), RGB(120, 150, 180),RGB(170, 210, 240), 3},
  {"ОГНЕННАЯ ДОЛИНА",      1500, RGB(120, 80, 60),  RGB(108, 70, 54),  RGB(160, 110, 70),  RGB(70, 40, 30),   RGB(255, 120, 40), 4},
  {"КОРОЛЕВСКИЙ ЗАМОК",    2200, RGB(90, 150, 90),  RGB(82, 140, 82),  RGB(190, 185, 175), RGB(140, 135, 130),RGB(220, 190, 80), 5},
  {"ЛЕГЕНДАРНАЯ АРЕНА",    3000, RGB(58, 48, 92),   RGB(50, 41, 82),   RGB(214, 178, 84),  RGB(120, 88, 36),  RGB(255, 215, 90), 6},   // здесь играют ТОП-100 ботов
};
static const int NARENA = (int)(sizeof ARENAS / sizeof ARENAS[0]);
static bool isLegend(int a) { return a >= 0 && a < NARENA && ARENAS[a].decoKind == 6; }
static int arenaOf(int trophies) { int a = 0; for (int i = 0; i < NARENA; i++) if (trophies >= ARENAS[i].trophies) a = i; return a; }

// ---- уровни карт ----
static const int MAXLVL = 10;
static const int NEED_CARDS[MAXLVL] = {0, 2, 4, 10, 20, 50, 100, 200, 400, 800};       // сколько копий нужно для перехода на уровень (обычные)
static const int RAR_DIV[NRAR] = {1, 4, 20, 100};
static const int UP_GOLD[MAXLVL] = {0, 10, 30, 80, 200, 500, 1000, 2000, 4000, 8000};
static const int RAR_GOLDK[NRAR] = {1, 2, 4, 8};
static int needCards(int rar, int toLvl) { if (toLvl < 2 || toLvl > MAXLVL) return 0; return imax(1, NEED_CARDS[toLvl - 1] / RAR_DIV[rar]); }
static int needGold(int rar, int toLvl) { if (toLvl < 2 || toLvl > MAXLVL) return 0; return UP_GOLD[toLvl - 1] * RAR_GOLDK[rar]; }
static float lvlK(int lvl) { return powf(1.1f, (float)(imax(1, lvl) - 1)); }
static const int XP_UP[MAXLVL] = {0, 4, 10, 25, 50, 100, 200, 400, 800, 1600};        // опыт за улучшение до уровня N
static const int KING_XP[MAXLVL] = {0, 20, 70, 150, 300, 600, 1100, 2000, 3500, 6000}; // опыт для уровня короля

// ---- сундуки ----
enum { CH_WOOD, CH_SILVER, CH_GOLD, CH_MAGIC, CH_LEGEND, CH_FREE, CH_CROWN, NCHEST };
struct ChestT { const char *name; int unlockMin, cards, gold, gems, minRare, minEpic, legendPct; u32 col, col2; };
static const ChestT CHESTS[NCHEST] = {
  {"ДЕРЕВЯННЫЙ СУНДУК",   5,   6,   25,  0, 0, 0, 0,   RGB(150, 100, 60), RGB(110, 70, 40)},
  {"СЕРЕБРЯНЫЙ СУНДУК",   20,  14,  60,  0, 1, 0, 1,   RGB(200, 205, 215), RGB(130, 135, 150)},
  {"ЗОЛОТОЙ СУНДУК",      60,  36,  180, 2, 3, 0, 3,   RGB(240, 200, 60), RGB(180, 130, 30)},
  {"МАГИЧЕСКИЙ СУНДУК",   180, 90,  500, 4, 12, 2, 10, RGB(170, 80, 230), RGB(110, 40, 170)},
  {"ЛЕГЕНДАРНЫЙ СУНДУК",  360, 1,   0,   0, 0, 0, 100, RGB(80, 220, 210), RGB(30, 140, 150)},
  {"БЕСПЛАТНЫЙ СУНДУК",   0,   4,   20,  1, 0, 0, 0,   RGB(120, 200, 110), RGB(70, 140, 60)},
  {"СУНДУК КОРОН",        0,   24,  200, 5, 2, 0, 2,   RGB(80, 140, 240), RGB(40, 80, 170)},
};
static const int CHEST_CYCLE[16] = {CH_WOOD, CH_WOOD, CH_SILVER, CH_WOOD, CH_SILVER, CH_GOLD, CH_WOOD, CH_SILVER, CH_WOOD, CH_SILVER, CH_GOLD, CH_SILVER, CH_WOOD, CH_MAGIC, CH_SILVER, CH_GOLD};
static const int NSLOT = 4;

// ---- задания ----
enum { Q_WIN, Q_PLAY, Q_TOWERS, Q_DMG, Q_SPELLS, Q_THREE, Q_CHEST, Q_UPGRADE, NQTYPE };
struct QuestT { const char *text; int need, gold, gems; };
static const QuestT QUESTS[NQTYPE] = {
  {"ПОБЕДИ В БОЯХ",            2,    120, 0},
  {"СЫГРАЙ КАРТЫ",             25,   80,  5},
  {"СЛОМАЙ БАШНИ",             4,    150, 0},
  {"НАНЕСИ УРОН БАШНЯМ",       6000, 100, 5},
  {"ПРИМЕНИ ЗАКЛИНАНИЯ",       6,    80,  5},
  {"ПОБЕДИ С ТРЕМЯ КОРОНАМИ",  1,    0,   20},
  {"ОТКРОЙ СУНДУКИ",           2,    100, 0},
  {"УЛУЧШИ КАРТУ",             1,    60,  10},
};

// ---- профиль игрока ----
struct Slot { int type; long long unlockAt; bool opening; };    // type -1 - пусто; unlockAt - когда откроется (unix-время), если opening
struct Quest { int type, prog, need; bool done, claimed; };
static const int MAXCARDS = 64;                                  // запас: новые карты не ломают сохранение
template <int N> struct ProfileBaseT {
  char name[24]; int gold, gems, trophies, bestTrophies, kingLvl, kingXp, wins, losses, threeCrowns;
  int lvl[N], cnt[N]; bool have[N];
  int deck[8]; Slot slots[NSLOT]; int chestN;
  long long freeAt; int crowns; long long crownDay;
  Quest quests[3]; long long questDay;
  int shopCard[3]; bool shopBought[3]; long long shopDay;
  int arenaSeen; bool soundOn;
};
template <int N> struct ProfileT : ProfileBaseT<N> { int rating, ratingBest; long long ratingWeek; };   // рейтинг Легендарной арены: сбрасывается каждую неделю
typedef ProfileT<MAXCARDS> Profile;
typedef ProfileBaseT<MAXCARDS> ProfileV2;                           // прошлая версия сохранения (без рейтинга)
typedef ProfileBaseT<23> ProfileV1;                                 // первая версия (23 карты)
static_assert(NCARD <= MAXCARDS, "карт больше запаса");
// ---- баланс: множители силы карт, подобраны на 24 000 боях ботов (цель - 49-52% побед) ----
static const float BAL_K[NCARD] = {1.000f, 1.000f, 1.152f, 1.269f, 1.228f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.258f, 1.000f, 1.300f, 1.000f, 1.000f, 1.000f, 1.300f, 1.558f, 1.438f, 1.298f, 1.300f, 1.000f, 1.000f, 1.233f, 1.000f, 1.184f, 1.671f, 1.501f, 1.590f, 1.468f, 1.600f, 1.579f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f};
static void balanceInit() {
  static bool done = false; if (done) return; done = true;
  for (int c = 0; c < NCARD; c++) { float k = BAL_K[c]; if (fabsf(k - 1) < 1e-4f) continue; Card &cd = CARDS[c];
    if (c == C_HUT) { UNITS[U_HUT].hp *= k; UNITS[U_HUT].spawnT /= k; continue; }
    if (c == C_BARREL) { cd.sDmg = 120.0f * (k - 1); cd.sTowerK = 0.35f; continue; }
    if (cd.type == CT_SPELL) { if (cd.spell == SP_FREEZE) cd.sTime *= k; cd.sDmg *= k; continue; }
    UnitT &u = UNITS[cd.unit]; u.hp *= k; u.dmg *= k; u.shield *= k;
    if (c == C_VOLAR) { UNITS[U_PUP].hp *= k; UNITS[U_PUP].dmg *= k; } }
}

static Profile pf;
static long long nowSec() { return (long long)time(NULL); }
static long long dayOf(long long t) { time_t tt = (time_t)t; struct tm *lt = localtime(&tt); if (!lt) return t / 86400; return (long long)(lt->tm_year * 400 + lt->tm_yday); }

static void newQuests() {
  int used[3] = {-1, -1, -1};
  for (int i = 0; i < 3; i++) { int t; do { t = rand() % NQTYPE; } while (t == used[0] || t == used[1]); used[i] = t;
    pf.quests[i].type = t; pf.quests[i].prog = 0; pf.quests[i].need = QUESTS[t].need; pf.quests[i].done = pf.quests[i].claimed = false; }
  pf.questDay = dayOf(nowSec());
}
static void questAdd(int type, int n) { for (int i = 0; i < 3; i++) { Quest &q = pf.quests[i]; if (q.type == type && !q.done) { q.prog = imin(q.need, q.prog + n); if (q.prog >= q.need) q.done = true; } } }
static int cardsUnlocked(int arena, int *out) { int n = 0; for (int c = 0; c < NCARD; c++) if (CARDS[c].arena <= arena) out[n++] = c; return n; }
static void newShop() {
  int av[NCARD], n = cardsUnlocked(arenaOf(pf.trophies), av);
  for (int i = 0; i < 3; i++) { int c; int tries = 0; do { c = av[rand() % n]; tries++; } while (tries < 20 && ((i > 0 && c == pf.shopCard[0]) || (i > 1 && c == pf.shopCard[1]))); pf.shopCard[i] = c; pf.shopBought[i] = false; }
  pf.shopDay = dayOf(nowSec());
}
static int shopPrice(int c) { static const int P[NRAR] = {10, 100, 1000, 20000}; return P[CARDS[c].rarity] * (CARDS[c].rarity == RA_COMMON ? 10 : (CARDS[c].rarity == RA_RARE ? 2 : 1)); }
static int shopCount(int c) { static const int N[NRAR] = {10, 2, 1, 1}; return N[CARDS[c].rarity]; }

static void newProfile() {
  memset(&pf, 0, sizeof pf); snprintf(pf.name, sizeof pf.name, "ИГРОК");
  pf.gold = 300; pf.gems = 50; pf.trophies = 0; pf.kingLvl = 1; pf.soundOn = true;
  for (int c = 0; c < MAXCARDS; c++) { pf.lvl[c] = 1; pf.cnt[c] = 0; pf.have[c] = false; }
  for (int i = 0; i < 8; i++) { pf.deck[i] = START_DECK[i]; pf.have[START_DECK[i]] = true; }
  for (int s = 0; s < NSLOT; s++) pf.slots[s].type = -1;
  pf.slots[0].type = CH_WOOD;                                   // первый сундук в подарок
  pf.freeAt = nowSec(); pf.crownDay = dayOf(nowSec());
  newQuests(); newShop();
}
static const unsigned SAVE_MAGIC = 0xBA5E0001u;
// сохранение надёжно: пишем во временный файл, проверяем и только потом подменяем; прошлое сохранение остаётся копией
static unsigned profSum(const Profile &p) { const unsigned char *b = (const unsigned char *)&p; unsigned h = 2166136261u; for (size_t i = 0; i < sizeof p; i++) { h ^= b[i]; h *= 16777619u; } return h; }
static bool writeProfileTo(const char *fn) {
  FILE *f = fopen(fn, "wb"); if (!f) return false;
  unsigned m = SAVE_MAGIC; int sz = (int)sizeof pf; unsigned cs = profSum(pf);
  bool ok = fwrite(&m, 4, 1, f) == 1 && fwrite(&sz, 4, 1, f) == 1 && fwrite(&pf, sizeof pf, 1, f) == 1 && fwrite(&cs, 4, 1, f) == 1;
  ok = (fflush(f) == 0) && ok; ok = (fclose(f) == 0) && ok; return ok;
}
// режим разработчика: отдельный профиль (bashni_dev.sav), настоящий профиль с кубками не трогается
static bool devMode = false; static Profile realPf;
static void saveProfile() {
  if (devMode) { writeProfileTo(pth("bashni_dev.sav")); return; }
  char tmp[600], main0[600], bak[600]; snprintf(tmp, sizeof tmp, "%s", pth("bashni.tmp")); snprintf(main0, sizeof main0, "%s", pth("bashni.sav")); snprintf(bak, sizeof bak, "%s", pth("bashni.bak"));
  if (!writeProfileTo(tmp)) { writeProfileTo(main0); return; }
  remove(bak); rename(main0, bak);                       // старое сохранение - в запас
  if (rename(tmp, main0) != 0) writeProfileTo(main0);
}
static bool loadProfileFrom(const char *fn);
static bool loadProfile() { if (loadProfileFrom(pth("bashni.sav"))) return true; if (loadProfileFrom(pth("bashni.bak"))) { saveProfile(); return true; } return loadProfileFrom(pth("bashni.tmp")); }
static bool loadProfileFrom(const char *fn) {
  FILE *f = fopen(fn, "rb"); if (!f) return false;
  unsigned m = 0; int sz = 0; bool ok = fread(&m, 4, 1, f) == 1 && fread(&sz, 4, 1, f) == 1 && m == SAVE_MAGIC;
  if (ok && sz == (int)sizeof pf) { static Profile t; ok = fread(&t, sizeof t, 1, f) == 1; unsigned cs = 0; if (ok && fread(&cs, 4, 1, f) == 1 && cs != profSum(t)) ok = false;   // повреждённый файл не берём
    if (ok) pf = t; }
  else if (ok && sz == (int)sizeof(ProfileV2)) { static ProfileV2 o2; ok = fread(&o2, sizeof o2, 1, f) == 1; if (ok) { memset(&pf, 0, sizeof pf); memcpy(&pf, &o2, sizeof o2); } }   // сохранение до рейтинга: поля те же, рейтинг с нуля
  else if (ok && sz == (int)sizeof(ProfileV1)) { static ProfileV1 o; ok = fread(&o, sizeof o, 1, f) == 1;           // старое сохранение: переносим поля
    if (ok) { memset(&pf, 0, sizeof pf); memcpy(pf.name, o.name, sizeof pf.name); pf.gold = o.gold; pf.gems = o.gems; pf.trophies = o.trophies; pf.bestTrophies = o.bestTrophies; pf.kingLvl = o.kingLvl; pf.kingXp = o.kingXp;
      pf.wins = o.wins; pf.losses = o.losses; pf.threeCrowns = o.threeCrowns; for (int c = 0; c < MAXCARDS; c++) { pf.lvl[c] = c < 23 ? o.lvl[c] : 1; pf.cnt[c] = c < 23 ? o.cnt[c] : 0; pf.have[c] = c < 23 ? o.have[c] : false; }
      memcpy(pf.deck, o.deck, sizeof pf.deck); memcpy(pf.slots, o.slots, sizeof pf.slots); pf.chestN = o.chestN; pf.freeAt = o.freeAt; pf.crowns = o.crowns; pf.crownDay = o.crownDay;
      memcpy(pf.quests, o.quests, sizeof pf.quests); pf.questDay = o.questDay; memcpy(pf.shopCard, o.shopCard, sizeof pf.shopCard); memcpy(pf.shopBought, o.shopBought, sizeof pf.shopBought); pf.shopDay = o.shopDay; pf.arenaSeen = o.arenaSeen; pf.soundOn = o.soundOn; } }
  else ok = false;
  fclose(f); if (!ok) return false;
  for (int c = 0; c < NCARD; c++) pf.lvl[c] = clampi(pf.lvl[c], 1, MAXLVL);
  pf.kingLvl = clampi(pf.kingLvl, 1, MAXLVL);
  for (int i = 0; i < 8; i++) if (pf.deck[i] < 0 || pf.deck[i] >= NCARD) pf.deck[i] = START_DECK[i];
  return true;
}
// ежедневное: задания, магазин, короны
// ---- неделя рейтинга: с понедельника 00:00 по местному времени ----
static const int RATING_WIN = 100, RATING_LOSS = 200;               // победа +100, поражение -200 (ниже нуля не падает)
static long long localDayNum(long long t) { time_t tt = (time_t)t; struct tm *lt = localtime(&tt); if (!lt) return t / 86400; long long y = lt->tm_year + 1900, m = lt->tm_mon + 1, d = lt->tm_mday; y -= m <= 2; long long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + doe - 719468; }
static long long weekOf(long long t) { return (localDayNum(t) + 3) / 7; }   // 1 января 1970 - четверг, поэтому +3: неделя меняется в понедельник
static long long weekLeftSec() { time_t tt = (time_t)nowSec(); struct tm *lt = localtime(&tt); long long dn = localDayNum(nowSec()), sec = lt ? lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec : 0; return (7 - (dn + 3) % 7) * 86400 - sec; }
static bool weekJustReset = false;
static void dailyRefresh() {
  long long d = dayOf(nowSec());
  if (pf.questDay != d) newQuests();
  if (pf.shopDay != d) newShop();
  if (pf.crownDay != d) { pf.crownDay = d; pf.crowns = 0; }
  long long w = weekOf(nowSec());                                                  // новая неделя - рейтинг с нуля
  if (pf.ratingWeek != w) { if (pf.ratingWeek != 0 && pf.rating > 0) weekJustReset = true; pf.rating = 0; pf.ratingBest = 0; pf.ratingWeek = w; }
}
static int kingTowerLvl() { return pf.kingLvl; }
static void devMaxOut() {                                         // все карты есть и прокачаны до максимума
  for (int c = 0; c < NCARD; c++) { pf.have[c] = true; pf.lvl[c] = MAXLVL; }
  pf.kingLvl = MAXLVL; pf.kingXp = KING_XP[MAXLVL - 1]; if (pf.gold < 999999) pf.gold = 999999; if (pf.gems < 99999) pf.gems = 99999;
  if (pf.trophies < ARENAS[NARENA - 1].trophies) pf.trophies = ARENAS[NARENA - 1].trophies; if (pf.bestTrophies < pf.trophies) pf.bestTrophies = pf.trophies;
}
static void dailyRefresh();
static void enterDev() {
  if (devMode) return; saveProfile(); realPf = pf; devMode = true;
  if (!loadProfileFrom(pth("bashni_dev.sav"))) { newProfile(); snprintf(pf.name, sizeof pf.name, "РАЗРАБОТЧИК"); }
  devMaxOut(); dailyRefresh(); saveProfile();
}
static void exitDev() { if (!devMode) return; saveProfile(); pf = realPf; devMode = false; }
static void addKingXp(int xp) { pf.kingXp += xp; while (pf.kingLvl < MAXLVL && pf.kingXp >= KING_XP[pf.kingLvl]) pf.kingLvl++; }

// =====================================================================
//  БИТВА БАШЕН: бой - юниты, снаряды, заклинания, эликсир, время, бот
// =====================================================================
static const float SIM_DT = 1.0f / 30.0f;
static const float MATCH_T = 180.0f, OVER_T = 60.0f, ELIX_T = 2.8f;
struct Unit {
  int id, type, team, lvl; float x, z, px, pz, y, hp, maxhp, dmg, atkCD, deployT, frozenT, stunT, spawnCD, chargeDist, face, anim, atkAnim, flash, slowT, shield, jumpT, jumpCD, jx0, jz0, jx1, jz1, pullT, pullX, pullZ, infT;
  int target, infTg, owner; bool alive, attacking, charging, moving;
};
enum { PJ_ARROW, PJ_BOLT, PJ_FIRE, PJ_BOMB, PJ_CANNON, PJ_DARK, PJ_DART, PJ_LAVA, PJ_ICE };
struct Proj { int kind, team, target; float x, z, y, px, pz, py, tx, tz, ty, v, dmg, splash; bool canAir, canGround, arc, dead; float sx, sz, sy, t, T, arcH; };
struct Spell { int kind, team, lvl; float x, z, r, dmg, towerK, delay, time, t, sx, sz; bool hit; };
enum { FX_HIT, FX_BOOM, FX_DEATH, FX_DEPLOY, FX_TOWERDOWN, FX_ZAP, FX_SPLASH, FX_HEALP };
struct Fx { int kind, team; float x, z, y, t, T, r; };
enum { BE_PLAY = 1, BE_HIT, BE_SHOOT, BE_BOOM, BE_TOWER, BE_DEATH, BE_END, BE_SPELL };
struct BEv { int type, team, a; float x, z; };
struct Side {
  int deck[8], lvl[8], hand[4], q[4]; float elixir; int crowns, kingLvl; bool kingActive; int towerId[3]; int cardsPlayed, spells, towersBroken; float towerDmg; bool bot;
};
struct Battle {
  std::vector<Unit> units; std::vector<Proj> projs; std::vector<Spell> spells; std::vector<Fx> fx; std::vector<BEv> ev;
  Side side[4]; int nSides; bool duo; float elixMul; float t, acc; bool over, overtime; int winner, nextId; int arena; unsigned rng;   // стороны: 0 и 2 - команда 0, 1 и 3 - команда 1 (2 на 2)
};
static Battle B;
static float brand(float a, float b) { B.rng = B.rng * 1664525u + 1013904223u; return a + (b - a) * ((B.rng >> 8) & 0xFFFF) / 65535.0f; }
static const UnitT &UT(const Unit &u) { return UNITS[u.type]; }
static bool isBuild(const Unit &u) { return UT(u).kind != UT_TROOP; }
static float zOf(int team, float z) { return team == 0 ? z : AL - z; }       // координата «от своей стороны»

static Unit *unitById(int id) { for (size_t i = 0; i < B.units.size(); i++) if (B.units[i].id == id && B.units[i].alive) return &B.units[i]; return NULL; }
static void bEvent(int type, int team, float x, float z, int a = 0) { BEv e; e.type = type; e.team = team; e.x = x; e.z = z; e.a = a; if (B.ev.size() < 200) B.ev.push_back(e); }
static void addFx(int kind, int team, float x, float z, float y, float T, float r) { Fx f; f.kind = kind; f.team = team; f.x = x; f.z = z; f.y = y; f.t = 0; f.T = T; f.r = r; if (B.fx.size() < 160) B.fx.push_back(f); }

static int spawnUnit(int type, int team, int lvl, float x, float z, float deploy) {
  if (B.units.size() >= 560) return -1;                       // запас ёмкости: ссылки на юнитов не должны «переехать»
  Unit u; memset(&u, 0, sizeof u); const UnitT &t = UNITS[type];
  u.id = ++B.nextId; u.type = type; u.team = team; u.lvl = lvl; u.x = u.px = x; u.z = u.pz = z; u.y = t.air ? 2.2f : 0;
  float k = t.kind == UT_TOWER ? powf(1.08f, (float)(lvl - 1)) : lvlK(lvl);
  u.maxhp = u.hp = t.hp * k; u.dmg = t.dmg * k; u.shield = t.shield * k; u.atkCD = t.hitT * 0.4f; u.deployT = deploy; u.spawnCD = t.spawnT * 0.5f; u.target = -1; u.alive = true;
  u.face = team == 0 ? 0 : 3.14159f; u.anim = brand(0, 6.28f);
  B.units.push_back(u); return u.id;
}
static inline int teamOf(int si) { return si & 1; }
static inline bool riverJumper(int type) { return type == U_BULL || type == U_HOG; }   // перепрыгивают реку
static void initSide(int si, const int *deck, const int *lvls, int kingLvl, bool bot) {
  int team = teamOf(si); Side &s = B.side[si]; memset(&s, 0, sizeof s);
  for (int i = 0; i < 8; i++) { s.deck[i] = deck[i]; s.lvl[i] = lvls[i]; }
  int ord[8]; for (int i = 0; i < 8; i++) ord[i] = i;
  for (int i = 7; i > 0; i--) { int j = (int)brand(0, i + 0.999f); int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
  for (int i = 0; i < 4; i++) { s.hand[i] = ord[i]; s.q[i] = ord[4 + i]; }
  s.elixir = 5; s.kingLvl = kingLvl; s.bot = bot;
  if (si >= 2) return;                                            // напарник: башни общие с первым игроком команды
  s.towerId[0] = spawnUnit(U_KING, team, kingLvl, 9.0f, zOf(team, 3.0f), 0);
  s.towerId[1] = spawnUnit(U_PRINCESS, team, kingLvl, BRIDGE_X[0], zOf(team, 6.5f), 0);
  s.towerId[2] = spawnUnit(U_PRINCESS, team, kingLvl, BRIDGE_X[1], zOf(team, 6.5f), 0);
}
static void startBattle(const int *deck0, const int *lvl0, int king0, const int *deck1, const int *lvl1, int king1, bool bot1, int arena, unsigned seed) {
  B.units.clear(); B.units.reserve(600); B.projs.clear(); B.spells.clear(); B.fx.clear(); B.ev.clear();
  B.t = 0; B.acc = 0; B.over = false; B.overtime = false; B.winner = -1; B.nextId = 0; B.arena = arena; B.rng = seed ? seed : 12345u;
  B.nSides = 2; B.duo = false; B.elixMul = 1.0f;
  initSide(0, deck0, lvl0, king0, false); initSide(1, deck1, lvl1, king1, bot1);
}
// 2 на 2: четыре колоды, башни у команды общие
static void startBattle4(const int decks[4][8], const int lvls[4][8], int king0, int king1, int arena, unsigned seed) {
  startBattle(decks[0], lvls[0], king0, decks[1], lvls[1], king1, true, arena, seed);
  initSide(2, decks[2], lvls[2], king0, true); initSide(3, decks[3], lvls[3], king1, true); B.nSides = 4; B.duo = true;
}
static bool doubleElixir() { return B.t >= MATCH_T - 60.0f; }
static float timeLeft() { return B.overtime ? MATCH_T + OVER_T - B.t : MATCH_T - B.t; }

// ---- куда можно ставить ----
static bool towerAlive(int team, int idx) { Unit *u = unitById(B.side[team].towerId[idx]); return u != NULL; }
// своя половина; если у врага сломана башня на этой стороне - можно дальше, до его башен
static bool canPlace(int team, int card, float x, float z) {
  const Card &c = CARDS[card];
  if (x < 0.5f || x > AW - 0.5f || z < 0.5f || z > AL - 0.5f) return false;
  if (c.type == CT_SPELL) return true;
  float lz = zOf(team, z);                                     // «своя» координата: 0 - у своего короля
  float maxZ = 15.0f;
  int enemy = 1 - team;
  if (x < 9.0f && !towerAlive(enemy, 1)) maxZ = 21.5f;
  if (x >= 9.0f && !towerAlive(enemy, 2)) maxZ = 21.5f;
  if (lz > maxZ) return false;
  if (fabsf(z - RIVER_Z) < RIVER_HW + 0.2f) return false;
  for (size_t i = 0; i < B.units.size(); i++) { const Unit &u = B.units[i]; if (!u.alive || UT(u).kind != UT_TOWER) continue; float r = UT(u).radius + 0.3f; if (fabsf(u.x - x) < r && fabsf(u.z - z) < r) return false; }
  return true;
}
static const float COLL_T = 9.0f;                                   // сборник эликсира: 1 эликсир раз в 9 с (в двойной эликсир - вдвое чаще)
static void cycleHand(Side &s, int slot) { int played = s.hand[slot]; s.hand[slot] = s.q[0]; for (int i = 0; i < 3; i++) s.q[i] = s.q[i + 1]; s.q[3] = played; }
static void iceBurst(Unit &w);
static bool playCard(int si, int slot, float x, float z) {
  if (B.over || slot < 0 || slot > 3 || si < 0 || si >= B.nSides) return false;
  int team = teamOf(si); Side &s = B.side[si], &ts = B.side[team]; int di = s.hand[slot], card = s.deck[di], lvl = s.lvl[di]; const Card &c = CARDS[card];
  if (s.elixir + 1e-4f < c.cost || !canPlace(team, card, x, z)) return false;
  s.elixir -= c.cost; s.cardsPlayed++;
  if (c.type == CT_SPELL || card == C_FIREMAGE) {
    Spell sp; memset(&sp, 0, sizeof sp); sp.kind = card == C_FIREMAGE ? SP_MAGEFALL : c.spell; sp.team = team; sp.lvl = lvl; sp.x = x; sp.z = z; sp.r = c.sR; sp.dmg = c.sDmg * lvlK(lvl); sp.towerK = c.sTowerK; sp.delay = c.sDelay; sp.time = c.sTime;
    Unit *k = unitById(ts.towerId[0]); if (!k) k = unitById(ts.towerId[1]); if (!k) k = unitById(ts.towerId[2]);
    sp.sx = k ? k->x : 9.0f; sp.sz = k ? k->z : zOf(team, 3.0f); if (k) { k->atkAnim = 1.0f; k->face = atan2f(x - k->x, z - k->z); }
    { float d = sqrtf((x - sp.sx) * (x - sp.sx) + (z - sp.sz) * (z - sp.sz)), v = sp.kind == SP_ROCKET ? 11.0f : (sp.kind == SP_FIREBALL || sp.kind == SP_MAGEFALL ? 15.0f : (sp.kind == SP_ARROWS ? 17.0f : (sp.kind == SP_BARREL ? 10.0f : 0)));   // скорость полёта, клеток/с
      if (v > 0) sp.delay = fmaxv(0.35f, d / v); }
    if (sp.kind == SP_MAGEFALL) { sp.r = 1.0f; sp.dmg = 0; } else s.spells++;
    B.spells.push_back(sp);
  } else {
    int n = imax(1, c.count); const UnitT &t = UNITS[c.unit];
    for (int i = 0; i < n; i++) {
      float ox = 0, oz = 0;
      if (c.unit == U_RECRUIT) { ox = 1.5f + i * (AW - 3.0f) / imax(1, n - 1) - x; }       // новобранцы - цепью поперёк арены
      else if (n == 2) { ox = (i ? 0.6f : -0.6f); }
      else if (n == 3) { float a = i * 2.094f; ox = sinf(a) * 0.7f; oz = cosf(a) * 0.7f; }
      else if (n > 4) { float rr = 0.4f * sqrtf((float)i), a = i * 2.39996f; ox = sinf(a) * rr; oz = cosf(a) * rr; }   // толпа - спиралью, чтобы не стояли друг в друге
      else if (n >= 4) { ox = ((i & 1) ? 0.55f : -0.55f); oz = ((i & 2) ? 0.55f : -0.55f); }
      float ux = clampf(x + ox, 0.4f, AW - 0.4f), uz = clampf(z + oz, 0.4f, AL - 0.4f);
      if (!t.air && fabsf(uz - RIVER_Z) < RIVER_HW + 0.2f) uz = z;
      int nid = spawnUnit(c.unit, team, lvl, ux, uz, c.unit == U_ICEWIZ ? 0.3f : 1.0f);
      if (c.unit == U_COLLECTOR) { Unit *w = unitById(nid); if (w) { w->owner = si; w->spawnCD = COLL_T; } }   // запоминаем, кому капает эликсир
      if (c.unit == U_ICEWIZ) { Unit *w = unitById(nid); if (w) iceBurst(*w); }   // мороз - в момент постановки, пока маг ещё появляется
    }
    addFx(FX_DEPLOY, team, x, z, 0, 0.6f, t.radius + 0.5f);
  }
  cycleHand(s, slot);
  bEvent(BE_PLAY, team, x, z, card);
  return true;
}

// ---- урон ----
static void killUnit(Unit &u);
static void dealDmg(Unit &t, float dmg, int byTeam) {
  if (!t.alive || dmg <= 0) return;
  t.flash = 0.15f;
  if (t.shield > 0) { t.shield -= dmg; if (t.shield < 0) t.shield = 0; return; }   // щит принимает удар целиком, лишнее пропадает
  t.hp -= dmg;
  if (UT(t).kind == UT_TOWER) { B.side[byTeam].towerDmg += dmg; if (t.type == U_KING) B.side[t.team].kingActive = true; }
  if (t.hp <= 0) killUnit(t);
}
static void healUnit(Unit &t, float v) { if (t.alive && UT(t).kind == UT_TROOP) t.hp = fminv(t.maxhp, t.hp + v); }
static void splashAt(float x, float z, float r, float dmg, int team, bool air, bool ground, float towerK, bool knock, float stepK = 0.0f) {
  // stepK > 0: каждый следующий задетый (от ближнего к дальнему) получает на stepK больше урона: 100%, 110%, 120%...
  std::vector<std::pair<float, int> > order;
  for (size_t i = 0; i < B.units.size(); i++) { Unit &u = B.units[i]; if (!u.alive || u.team == team) continue; bool ua = UT(u).air; if ((ua && !air) || (!ua && !ground)) continue;
    float dx = u.x - x, dz = u.z - z, rr = r + UT(u).radius * 0.5f; if (dx * dx + dz * dz > rr * rr) continue; order.push_back(std::make_pair(dx * dx + dz * dz, (int)i)); }
  if (stepK > 0) std::sort(order.begin(), order.end());
  for (size_t oi = 0; oi < order.size(); oi++) { Unit &u = B.units[order[oi].second]; if (!u.alive) continue;
    float dx = u.x - x, dz = u.z - z;
    float dm = dmg * (1.0f + stepK * (float)oi);
    dealDmg(u, UT(u).kind == UT_TOWER ? dm * towerK : dm, team);
    if (knock && u.alive && UT(u).kind == UT_TROOP && UT(u).mass <= 6) { float l = sqrtf(dx * dx + dz * dz) + 0.01f; u.x += dx / l * 0.8f; u.z += dz / l * 0.8f; } }
}
static const float ICEWIZ_SPAWN_DMG = 84.85f;                                 // урон ледяного мага при появлении (x lvlK)
static const float MEGA_JUMP_K = 0.68f;                                       // прыжок мегарыцаря = 68% его удара
static void lineSplash(const Unit &u, float len, float halfW, float dmg) {     // силач: удар полосой вперёд по направлению взгляда
  float fx = sinf(u.face), fz = cosf(u.face);
  for (size_t i = 0; i < B.units.size(); i++) { Unit &e = B.units[i]; if (!e.alive || e.team == u.team || UT(e).air) continue;
    float dx = e.x - u.x, dz = e.z - u.z, along = dx * fx + dz * fz, side = dx * fz - dz * fx, r = UT(e).radius * 0.5f;
    if (along < -r || along > len + r || fabsf(side) > halfW + r) continue; dealDmg(e, dmg, u.team); }
  for (int k = 1; k <= 5; k++) { float a = len * k / 5.5f; addFx(FX_SPLASH, u.team, u.x + fx * a, u.z + fz * a, 0.1f, 0.45f, 0.9f); }
}
// инферно: разогрев луча на одной цели - 1 стадия, через 2 с - x3.5, через 4 с - x10
static const float INF_T2 = 2.0f, INF_T3 = 4.0f, INF_M2 = 3.5f, INF_M3 = 10.0f;
static float infMul(float t) { return t >= INF_T3 ? INF_M3 : (t >= INF_T2 ? INF_M2 : 1.0f); }
static const float CG_SPLASH_STEP = 0.10f, CG_SPLASH_K = 0.6f, CG_PULL_R = 8.0f, CG_PULL_T = 5.0f;      // медный голем: удар по площади = 60% (300 из 500), цепь на 8 клеток раз в 5 с
// ---- духи (огненный и ледяной): подбегают к цели и взрываются ----
static const float SPIRIT_R_FIRE = 1.4f, SPIRIT_R_ICE = 1.7f, SPIRIT_FREEZE = 2.0f;
static void spiritBlast(Unit &u) {
  bool ice = u.type == U_ISPIRIT; float r = ice ? SPIRIT_R_ICE : SPIRIT_R_FIRE;
  splashAt(u.x, u.z, r, u.dmg, u.team, true, true, 1.0f, false);   // урон по земле и воздуху, башням тоже
  if (ice) for (size_t q = 0; q < B.units.size(); q++) { Unit &e = B.units[q]; if (!e.alive || e.team == u.team) continue; float dx = e.x - u.x, dz = e.z - u.z, rr = r + UT(e).radius * 0.5f; if (dx * dx + dz * dz <= rr * rr) e.frozenT = fmaxv(e.frozenT, SPIRIT_FREEZE); }
  addFx(FX_BOOM, u.team, u.x, u.z, 0, 0.6f, r); bEvent(BE_BOOM, u.team, u.x, u.z); killUnit(u);
}
static void copperPull(Unit &g) {                                               // цепью тянет врагов к себе
  int n = 0;
  for (size_t i = 0; i < B.units.size(); i++) { Unit &e = B.units[i]; if (!e.alive || e.team == g.team || UT(e).kind != UT_TROOP || UT(e).mass >= 20 || e.pullT > 0) continue;
    float dx = g.x - e.x, dz = g.z - e.z, d = sqrtf(dx * dx + dz * dz); if (d > CG_PULL_R || d < UT(g).radius + UT(e).radius + 0.6f) continue;
    if (!UT(e).air && (e.z - RIVER_Z) * (g.z - RIVER_Z) < 0) continue;            // через реку по земле не тянем
    float stop = UT(g).radius + UT(e).radius + 0.25f; e.pullT = 0.5f; e.pullX = g.x - dx / d * stop; e.pullZ = g.z - dz / d * stop; e.jx0 = g.x; e.jz0 = g.z; n++; }
  if (n) { g.atkAnim = 1.0f; bEvent(BE_HIT, g.team, g.x, g.z, -2); }
}
static void slowArea(float x, float z, float r, int team, float T) {                 // замедление от льда
  for (size_t i = 0; i < B.units.size(); i++) { Unit &u = B.units[i]; if (!u.alive || u.team == team || UT(u).kind == UT_TOWER) continue;
    float dx = u.x - x, dz = u.z - z, rr = r + UT(u).radius * 0.5f; if (dx * dx + dz * dz <= rr * rr) u.slowT = fmaxv(u.slowT, T); }
}
static void iceBurst(Unit &w) {   // ледяной маг: сразу при появлении замораживает и бьёт всех вокруг
 float r = 2.0f; splashAt(w.x, w.z, r, ICEWIZ_SPAWN_DMG * lvlK(w.lvl), w.team, true, true, 1.0f, false);   /* 200 урона на 10 ур. */
      for (size_t q = 0; q < B.units.size(); q++) { Unit &e = B.units[q]; if (!e.alive || e.team == w.team) continue; float dx = e.x - w.x, dz = e.z - w.z, rr = r + UT(e).radius * 0.5f; if (dx * dx + dz * dz <= rr * rr) e.frozenT = fmaxv(e.frozenT, 2.0f); }   /* всё вокруг замерзает на 2 с */ addFx(FX_SPLASH, w.team, w.x, w.z, 0.3f, 0.5f, r); bEvent(BE_SPELL, w.team, w.x, w.z, SP_FREEZE); 
}
static void killUnit(Unit &u) {
  if (!u.alive) return; u.alive = false; u.hp = 0;
  const UnitT &t = UT(u);
  if (u.type == U_COLLECTOR && u.owner >= 0 && u.owner < B.nSides) B.side[u.owner].elixir = fminv(10.0f, B.side[u.owner].elixir + 1.0f);   // при гибели отдаёт ещё 1 эликсир
  if (t.kind == UT_TOWER) {
    int enemy = 1 - u.team; Side &e = B.side[enemy]; e.towersBroken++;
    if (u.type == U_KING) { e.crowns = 3; B.over = true; B.winner = enemy; }
    else { e.crowns = imin(3, e.crowns + 1); B.side[u.team].kingActive = true; if (B.overtime) { B.over = true; B.winner = enemy; } }
    addFx(FX_TOWERDOWN, u.team, u.x, u.z, 0, 1.5f, 2.5f); bEvent(BE_TOWER, u.team, u.x, u.z, u.type);
    if (B.over) bEvent(BE_END, B.winner, 0, 0, 0);
  } else {
    addFx(FX_DEATH, u.team, u.x, u.z, u.y, 0.6f, t.radius); bEvent(BE_DEATH, u.team, u.x, u.z, u.type);
    if (t.deathN > 0) { for (int k = 0; k < t.deathN; k++) { float a = k * 6.2832f / t.deathN; spawnUnit(t.deathU, u.team, u.lvl, clampf(u.x + cosf(a) * 0.7f, 0.5f, AW - 0.5f), clampf(u.z + sinf(a) * 0.7f, 0.5f, AL - 0.5f), 0.15f); }
      addFx(FX_BOOM, u.team, u.x, u.z, u.y, 0.6f, 1.2f); bEvent(BE_BOOM, u.team, u.x, u.z); }                // распался на щенков
    if (t.deathDmg > 0) { splashAt(u.x, u.z, t.deathR, t.deathDmg * lvlK(u.lvl), u.team, false, true, 1.0f, true); addFx(FX_BOOM, u.team, u.x, u.z, 0, 0.7f, t.deathR); bEvent(BE_BOOM, u.team, u.x, u.z); }
  }
}

// ---- выбор цели ----
static bool canHit(const Unit &a, const Unit &b) { if (!b.alive || b.team == a.team) return false; if (UT(b).air && !UT(a).canAir) return false; if (UT(a).onlyBuild && !isBuild(b)) return false; return true; }
static float edgeDist(const Unit &a, const Unit &b) { float dx = a.x - b.x, dz = a.z - b.z; return sqrtf(dx * dx + dz * dz) - UT(a).radius - UT(b).radius; }
static int findTarget(const Unit &u) {
  const UnitT &t = UT(u); int best = -1; float bd = 1e9f;
  // король не стреляет, пока его не тронули
  if (u.type == U_KING && !B.side[u.team].kingActive) return -1;
  for (size_t i = 0; i < B.units.size(); i++) { const Unit &o = B.units[i]; if (!canHit(u, o)) continue;
    float d = edgeDist(u, o);
    if (t.kind != UT_TROOP) { if (d > t.range) continue; }
    else if (!t.onlyBuild && d > t.sight && !isBuild(o)) continue;
    if (t.kind == UT_TROOP && !t.onlyBuild && isBuild(o) && d > t.sight) d += 1000;      // здание вдалеке - запасная цель
    if (t.kind == UT_TROOP && !t.air && !riverJumper(u.type) && !UT(o).air) {                                       // через реку - длиннее
      if ((u.z - RIVER_Z) * (o.z - RIVER_Z) < 0) { float bx = fabsf(u.x - BRIDGE_X[0]) + fabsf(o.x - BRIDGE_X[0]) < fabsf(u.x - BRIDGE_X[1]) + fabsf(o.x - BRIDGE_X[1]) ? BRIDGE_X[0] : BRIDGE_X[1]; d += fabsf(u.x - bx) + fabsf(o.x - bx) - fabsf(u.x - o.x); } }
    if (d < bd) { bd = d; best = o.id; } }
  return best;
}

// ---- снаряды ----
static void shoot(Unit &u, Unit &tg) {
  const UnitT &t = UT(u); Proj p; memset(&p, 0, sizeof p);
  p.kind = u.type == U_FIREMAGE || u.type == U_DRAGON ? PJ_FIRE : (u.type == U_BOMBER ? PJ_BOMB : (u.type == U_CANNON ? PJ_CANNON : (u.type == U_XBOW ? PJ_BOLT : (u.type == U_NECRO ? PJ_DARK : (u.type == U_DART ? PJ_DART : (u.type == U_VOLAR || u.type == U_PUP ? PJ_LAVA : (u.type == U_ICEWIZ ? PJ_ICE : PJ_ARROW)))))));
  p.team = u.team; p.target = tg.id; p.dmg = u.dmg; p.splash = t.splash; p.canAir = t.canAir; p.canGround = true; p.arc = true;
  p.arcH = p.kind == PJ_BOMB ? 2.5f : (p.kind == PJ_ARROW ? 0.9f : (p.kind == PJ_FIRE ? 0.6f : (p.kind == PJ_CANNON ? 0.25f : (p.kind == PJ_DART ? 0.3f : 0.35f))));   // снаряды летят дугой
  float h0 = u.type == U_PRINCESS ? 4.2f : (u.type == U_KING ? 4.8f : (u.type == U_ATOWER ? 3.4f : (t.air ? u.y : 1.0f)));
  p.x = p.sx = u.x; p.z = p.sz = u.z; p.y = p.sy = h0; p.tx = tg.x; p.tz = tg.z; p.ty = UT(tg).air ? tg.y : 0.8f; p.v = t.projV;
  float d = sqrtf((p.tx - p.x) * (p.tx - p.x) + (p.tz - p.z) * (p.tz - p.z)); p.T = fmaxv(0.12f, d / p.v); p.t = 0;
  B.projs.push_back(p); bEvent(BE_SHOOT, u.team, u.x, u.z, p.kind);
}
static void updateProjs(float dt) {
  for (size_t i = 0; i < B.projs.size(); i++) { Proj &p = B.projs[i]; if (p.dead) continue;
    Unit *tg = unitById(p.target); if (tg) { p.tx = tg->x; p.tz = tg->z; p.ty = UT(*tg).air ? tg->y : 0.8f; }
    p.px = p.x; p.pz = p.z; p.py = p.y; p.t += dt; float k = clampf(p.t / p.T, 0, 1);
    p.x = p.sx + (p.tx - p.sx) * k; p.z = p.sz + (p.tz - p.sz) * k; p.y = p.sy + (p.ty - p.sy) * k + (p.arc ? sinf(k * 3.14159f) * p.arcH * fminv(1.0f, p.T * 1.5f) : 0);
    if (k >= 1) { p.dead = true;
      if (p.splash > 0) { splashAt(p.tx, p.tz, p.splash, p.dmg, p.team, p.canAir, p.canGround, 1.0f, false); if (p.kind == PJ_ICE) slowArea(p.tx, p.tz, p.splash, p.team, 2.5f); addFx(p.kind == PJ_FIRE ? FX_SPLASH : FX_BOOM, p.team, p.tx, p.tz, p.ty, 0.4f, p.splash); }
      else if (tg) { dealDmg(*tg, p.dmg, p.team); addFx(FX_HIT, p.team, p.tx, p.tz, p.ty, 0.25f, 0.4f); }
      bEvent(BE_HIT, p.team, p.tx, p.tz, p.kind); } }
  B.projs.erase(std::remove_if(B.projs.begin(), B.projs.end(), [](const Proj &p){ return p.dead; }), B.projs.end());
}
// ---- заклинания ----
static void updateSpells(float dt) {
  for (size_t i = 0; i < B.spells.size(); i++) { Spell &s = B.spells[i]; s.t += dt;
    if (!s.hit && s.t >= s.delay) { s.hit = true;
      switch (s.kind) {
        case SP_MAGEFALL: spawnUnit(U_FIREMAGE, s.team, s.lvl, s.x, s.z, 0.35f); addFx(FX_SPLASH, s.team, s.x, s.z, 0.3f, 0.5f, 1.2f); addFx(FX_DEPLOY, s.team, s.x, s.z, 0, 0.6f, 0.9f); bEvent(BE_BOOM, s.team, s.x, s.z); break;   // шар упал - появился маг
        case SP_FIREBALL: splashAt(s.x, s.z, s.r, s.dmg, s.team, true, true, s.towerK, true); addFx(FX_BOOM, s.team, s.x, s.z, 0, 0.7f, s.r); bEvent(BE_BOOM, s.team, s.x, s.z); break;
        case SP_ARROWS: splashAt(s.x, s.z, s.r, s.dmg, s.team, true, true, s.towerK, false); bEvent(BE_HIT, s.team, s.x, s.z, PJ_ARROW); break;
        case SP_ROCKET: splashAt(s.x, s.z, s.r, s.dmg, s.team, true, true, s.towerK, true); addFx(FX_BOOM, s.team, s.x, s.z, 0, 1.0f, s.r * 1.5f); bEvent(BE_BOOM, s.team, s.x, s.z, 1); break;
        case SP_ZAP: splashAt(s.x, s.z, s.r, s.dmg, s.team, true, true, s.towerK, false); addFx(FX_ZAP, s.team, s.x, s.z, 0, 0.4f, s.r);
          for (size_t k = 0; k < B.units.size(); k++) { Unit &u = B.units[k]; if (u.alive && u.team != s.team && (u.x - s.x) * (u.x - s.x) + (u.z - s.z) * (u.z - s.z) < s.r * s.r) { u.stunT = 0.5f; u.atkCD = fmaxv(u.atkCD, UT(u).hitT * 0.5f); } }
          bEvent(BE_BOOM, s.team, s.x, s.z, 2); break;
        case SP_FREEZE: splashAt(s.x, s.z, s.r, s.dmg, s.team, true, true, s.towerK, false);
          for (size_t k = 0; k < B.units.size(); k++) { Unit &u = B.units[k]; if (u.alive && u.team != s.team && (u.x - s.x) * (u.x - s.x) + (u.z - s.z) * (u.z - s.z) < s.r * s.r) u.frozenT = s.time; }
          bEvent(BE_SPELL, s.team, s.x, s.z, SP_FREEZE); break;
        case SP_HEAL: bEvent(BE_SPELL, s.team, s.x, s.z, SP_HEAL); break;
        case SP_BARREL: { const Card &bc = CARDS[C_BARREL];                                          // бочка разбилась - гоблины выскакивают
          for (int k = 0; k < bc.count; k++) { float a = k * 6.2832f / bc.count + 0.5f; float gx = clampf(s.x + cosf(a) * 0.6f, 0.5f, AW - 0.5f), gz = clampf(s.z + sinf(a) * 0.6f, 0.5f, AL - 0.5f);
            if (fabsf(gz - RIVER_Z) < RIVER_HW + 0.2f) gz = s.z < RIVER_Z ? RIVER_Z - RIVER_HW - 0.3f : RIVER_Z + RIVER_HW + 0.3f;
            spawnUnit(bc.unit, s.team, s.lvl, gx, gz, 0.25f); }
          if (s.dmg > 0) splashAt(s.x, s.z, s.r, s.dmg, s.team, false, true, s.towerK, false);
          addFx(FX_BOOM, s.team, s.x, s.z, 0, 0.5f, 1.0f); bEvent(BE_BOOM, s.team, s.x, s.z); } break;
      } }
    if (s.kind == SP_HEAL && s.t < s.time) for (size_t k = 0; k < B.units.size(); k++) { Unit &u = B.units[k]; if (u.alive && u.team == s.team && (u.x - s.x) * (u.x - s.x) + (u.z - s.z) * (u.z - s.z) < s.r * s.r) healUnit(u, s.dmg / s.time * dt); }
  }
  B.spells.erase(std::remove_if(B.spells.begin(), B.spells.end(), [](const Spell &s){ return s.hit && s.t > fmaxv(s.delay, s.time) + 0.6f; }), B.spells.end());
}

// ---- движение ----
static bool onBridge(float x) { return fabsf(x - BRIDGE_X[0]) < BRIDGE_HW || fabsf(x - BRIDGE_X[1]) < BRIDGE_HW; }
static void moveToward(Unit &u, float tx, float tz, float dt) {
  const UnitT &t = UT(u); float sp = t.speed * (u.charging ? 2.0f : 1.0f) * (u.slowT > 0 ? 0.65f : 1.0f);
  float gx = tx, gz = tz;
  bool jumper = riverJumper(u.type);                                       // бык перепрыгивает реку, как кабан в CR
  if (!t.air && !jumper && (u.z - RIVER_Z) * (tz - RIVER_Z) < 0) {                // нужна переправа
    float bx = fabsf(u.x - BRIDGE_X[0]) + fabsf(tx - BRIDGE_X[0]) < fabsf(u.x - BRIDGE_X[1]) + fabsf(tx - BRIDGE_X[1]) ? BRIDGE_X[0] : BRIDGE_X[1];
    float sd = u.z < RIVER_Z ? -1.0f : 1.0f;                              // на какой стороне
    if (fabsf(u.x - bx) > 0.55f && fabsf(u.z - RIVER_Z) > RIVER_HW - 0.1f) { gx = bx; gz = RIVER_Z + sd * (RIVER_HW + 0.3f); }
    else { gx = bx + clampf(u.x - bx, -0.4f, 0.4f); gz = RIVER_Z - sd * (RIVER_HW + 0.8f); } }
  float dx = gx - u.x, dz = gz - u.z, l = sqrtf(dx * dx + dz * dz);
  if (l < 1e-4f) return;
  float st = fminv(l, sp * dt); u.x += dx / l * st; u.z += dz / l * st; u.chargeDist += st;
  u.face = atan2f(dx, dz); u.moving = true;
}
static void separate() {
  size_t n = B.units.size();
  for (size_t i = 0; i < n; i++) { Unit &a = B.units[i]; if (!a.alive) continue; const UnitT &ta = UT(a);
    for (size_t j = i + 1; j < n; j++) { Unit &b = B.units[j]; if (!b.alive) continue; const UnitT &tb = UT(b);
      if (ta.air != tb.air) continue;
      float dx = b.x - a.x, dz = b.z - a.z, r = ta.radius + tb.radius, d2 = dx * dx + dz * dz; if (d2 >= r * r) continue;
      float d = sqrtf(d2); if (d < 1e-4f) { dx = 0.01f * ((i * 7 + j) % 3 - 1 + 0.5f); dz = 0.01f; d = 0.014f; }
      float ov = r - d, ma = ta.kind != UT_TROOP ? 1e6f : ta.mass, mb = tb.kind != UT_TROOP ? 1e6f : tb.mass, ka = mb / (ma + mb), kb = ma / (ma + mb);
      if (a.deployT > 0) ka *= 0.3f; if (b.deployT > 0) kb *= 0.3f;
      a.x -= dx / d * ov * ka; a.z -= dz / d * ov * ka; b.x += dx / d * ov * kb; b.z += dz / d * ov * kb; } }
  for (size_t i = 0; i < n; i++) { Unit &u = B.units[i]; if (!u.alive || UT(u).kind != UT_TROOP) continue; const UnitT &t = UT(u);
    u.x = clampf(u.x, t.radius, AW - t.radius); u.z = clampf(u.z, t.radius, AL - t.radius);
    if (!t.air && !riverJumper(u.type) && u.jumpT <= 0 && fabsf(u.z - RIVER_Z) < RIVER_HW + 0.1f && !onBridge(u.x)) {                // в реку нельзя
      if (fabsf(u.pz - RIVER_Z) >= RIVER_HW + 0.1f || !onBridge(u.px)) { if (onBridge(u.px) && fabsf(u.pz - RIVER_Z) < RIVER_HW + 0.1f) { float bx = fabsf(u.px - BRIDGE_X[0]) < BRIDGE_HW ? BRIDGE_X[0] : BRIDGE_X[1]; u.x = clampf(u.x, bx - BRIDGE_HW + 0.05f, bx + BRIDGE_HW - 0.05f); }
        else u.z = u.pz < RIVER_Z ? RIVER_Z - RIVER_HW - 0.1f : RIVER_Z + RIVER_HW + 0.1f; } } }
}

// ---- шаг боя ----
static void updateUnit(Unit &u, float dt) {
  const UnitT &t = UT(u);
  u.flash = fmaxv(0.0f, u.flash - dt); u.atkAnim = fmaxv(0.0f, u.atkAnim - dt * 3.0f); u.moving = false;
  if (u.deployT > 0) { u.deployT -= dt;
    if (u.deployT <= 0 && u.type == U_MEGA) { float r = 2.2f; splashAt(u.x, u.z, r, u.dmg * 1.604f, u.team, false, true, 1.0f, true); addFx(FX_BOOM, u.team, u.x, u.z, 0, 0.7f, r); bEvent(BE_BOOM, u.team, u.x, u.z); }   // удар при приземлении
    return; }
  float slowK = 1.0f; if (u.slowT > 0) { u.slowT -= dt; slowK = 0.65f; }
  if (u.pullT > 0) { u.pullT -= dt; float k = fminv(1.0f, dt * 9.0f); u.x += (u.pullX - u.x) * k; u.z += (u.pullZ - u.z) * k; u.moving = false; return; }   // тянут цепью
  if (u.type == U_CGOLEM) { u.spawnCD -= dt; if (u.spawnCD <= 0) { u.spawnCD = CG_PULL_T; copperPull(u); } }
  if (u.jumpT > 0) {                                                          // мегарыцарь в прыжке: летит к цели и давит при приземлении
    u.jumpT -= dt; float k = clampf(1.0f - u.jumpT / 0.8f, 0, 1); u.x = u.jx0 + (u.jx1 - u.jx0) * k; u.z = u.jz0 + (u.jz1 - u.jz0) * k; u.moving = false;
    if (u.jumpT <= 0) { splashAt(u.x, u.z, 2.0f, u.dmg * MEGA_JUMP_K, u.team, false, true, 1.0f, true);   /* приземление: 68% от обычного удара по всем вокруг */ addFx(FX_BOOM, u.team, u.x, u.z, 0, 0.6f, 2.0f); bEvent(BE_BOOM, u.team, u.x, u.z); u.atkCD = t.hitT * 0.5f; }
    return; }
  if (u.jumpCD > 0) u.jumpCD -= dt;
  if (t.kind == UT_BUILD && t.lifeT > 0) { u.hp -= u.maxhp / t.lifeT * dt; if (u.hp <= 0) { killUnit(u); return; } }
  if (u.frozenT > 0) { u.frozenT -= dt; u.infT = 0; return; }
  if (u.stunT > 0) { u.stunT -= dt; u.infT = 0; return; }
  // спавн (некромант, хижина)
  if (t.spawnU >= 0) { u.spawnCD -= dt; if (u.spawnCD <= 0) { u.spawnCD = t.spawnT; float fz = u.team == 0 ? 1.0f : -1.0f;
      bool ring = t.kind == UT_TROOP;                                      // войска (некромант, король гоблинов) призывают кольцом вокруг себя
      for (int k = 0; k < t.spawnN; k++) { float sx, sz;
        if (ring) { float a = k * 6.2832f / t.spawnN + u.face + 0.785f, rr = t.radius + 0.55f; sx = u.x + sinf(a) * rr; sz = u.z + cosf(a) * rr; }
        else { float a = (k - (t.spawnN - 1) * 0.5f) * 0.9f; sx = u.x + a; sz = u.z + fz * (t.radius + 0.5f); }
        sx = clampf(sx, 0.5f, AW - 0.5f); sz = clampf(sz, 0.5f, AL - 0.5f);
        if (!UNITS[t.spawnU].air && fabsf(sz - RIVER_Z) < RIVER_HW + 0.2f) sz = u.z;                // не в реку
        spawnUnit(t.spawnU, u.team, u.lvl, sx, sz, 0.3f); }
      addFx(FX_DEPLOY, u.team, u.x, u.z + (ring ? 0 : fz * (t.radius + 0.5f)), 0, 0.4f, ring ? t.radius + 0.9f : 0.8f); } }
  if (u.type == U_COLLECTOR) { u.spawnCD -= dt * (doubleElixir() || B.overtime ? 2.0f : 1.0f) * B.elixMul;
    if (u.spawnCD <= 0 && u.owner >= 0 && u.owner < B.nSides) { u.spawnCD += COLL_T; B.side[u.owner].elixir = fminv(10.0f, B.side[u.owner].elixir + 1.0f); addFx(FX_HEALP, u.team, u.x, u.z, 0, 0.8f, 0.7f); bEvent(BE_HIT, u.team, u.x, u.z, -1); } }
  if (t.dmg <= 0) return;
  Unit *tg = u.target >= 0 ? unitById(u.target) : NULL;
  if (tg && !canHit(u, *tg)) tg = NULL;
  // держим цель, пока бьём её; иначе каждый шаг ищем ближайшую
  if (!(tg && u.attacking && edgeDist(u, *tg) <= t.range + 0.3f)) { int id = findTarget(u); tg = id >= 0 ? unitById(id) : NULL; u.target = id; }
  if (!tg) { u.attacking = false; u.infT = 0;
    if (t.kind == UT_TROOP) { float gz = zOf(u.team, 28.0f); moveToward(u, u.x < 9 ? BRIDGE_X[0] : BRIDGE_X[1], gz, dt); }
    return; }
  float d = edgeDist(u, *tg);
  if (u.type == U_MEGA && u.jumpCD <= 0 && !u.attacking && d >= 2.5f && d <= 5.0f && (u.z - RIVER_Z) * (tg->z - RIVER_Z) >= 0) {   // прыжок на врага
    u.jumpT = 0.8f; u.jumpCD = 3.0f; u.jx0 = u.x; u.jz0 = u.z; float dx = tg->x - u.x, dz = tg->z - u.z, l = sqrtf(dx * dx + dz * dz) + 1e-4f, back = UT(*tg).radius + t.radius * 0.6f;
    u.jx1 = tg->x - dx / l * back; u.jz1 = tg->z - dz / l * back; u.face = atan2f(dx, dz); return; }
  if (d <= t.range) {
    if (!u.attacking) { u.attacking = true; u.atkCD = fmaxv(u.atkCD, t.hitT * 0.35f); }
    u.face = atan2f(tg->x - u.x, tg->z - u.z);
    if (u.type == U_INFERNO) { if (u.target != u.infTg) { u.infTg = u.target; u.infT = 0; } u.infT += dt; }   // тот же враг - луч разогревается
    u.atkCD -= dt * slowK;
    if (u.atkCD <= 0) { u.atkCD = t.hitT; u.atkAnim = 1.0f;
      float dm = u.dmg * (u.charging ? 2.0f : 1.0f); u.charging = false; u.chargeDist = 0;
      if (t.projV > 0) { Unit tmp = u; tmp.dmg = dm; shoot(tmp, *tg); }
      else if (u.type == U_INFERNO) { dealDmg(*tg, dm * infMul(u.infT), u.team); bEvent(BE_HIT, u.team, tg->x, tg->z, -1); }
      else if (u.type == U_FSPIRIT || u.type == U_ISPIRIT) { spiritBlast(u); return; }
      else if (u.type == U_CGOLEM) { int near = 0; for (size_t q = 0; q < B.units.size(); q++) { const Unit &e = B.units[q]; if (e.alive && e.team != u.team && !UT(e).air && (e.x - tg->x) * (e.x - tg->x) + (e.z - tg->z) * (e.z - tg->z) < (1.6f + UT(e).radius * 0.5f) * (1.6f + UT(e).radius * 0.5f)) near++; }
        if (near >= 2) { splashAt(tg->x, tg->z, 1.6f, dm * CG_SPLASH_K, u.team, false, true, 1.0f, true, CG_SPLASH_STEP); addFx(FX_BOOM, u.team, tg->x, tg->z, 0, 0.4f, 1.6f); bEvent(BE_BOOM, u.team, tg->x, tg->z); }   // толпа - удар по площади
        else { dealDmg(*tg, dm, u.team); addFx(FX_HIT, u.team, tg->x, tg->z, 0.8f, 0.25f, 0.5f); bEvent(BE_HIT, u.team, tg->x, tg->z, -1); } }   // одна цель - сильный удар
      else if (u.type == U_STRONG) { lineSplash(u, 6.0f, 1.0f, dm); bEvent(BE_BOOM, u.team, tg->x, tg->z); }
      else { if (t.splash > 0) splashAt(tg->x, tg->z, t.splash, dm, u.team, t.canAir, true, 1.0f, false); else dealDmg(*tg, dm, u.team); addFx(FX_HIT, u.team, tg->x, tg->z, UT(*tg).air ? tg->y : 0.8f, 0.2f, 0.35f); bEvent(BE_HIT, u.team, tg->x, tg->z, -1); } }
  } else {
    u.attacking = false; u.infT = 0; u.atkCD = fmaxv(0.0f, u.atkCD - dt);
    if (t.kind == UT_TROOP) { moveToward(u, tg->x, tg->z, dt); if (u.type == U_LANCER && u.chargeDist > 2.5f) u.charging = true; }
  }
}
static void simStep(float dt) {
  if (B.over) return;
  B.t += dt;
  for (int s = 0; s < B.nSides; s++) { float r = (doubleElixir() || B.overtime ? 2.0f : 1.0f) * B.elixMul / ELIX_T; B.side[s].elixir = fminv(10.0f, B.side[s].elixir + r * dt); }
  for (size_t i = 0; i < B.units.size(); i++) { B.units[i].px = B.units[i].x; B.units[i].pz = B.units[i].z; }
  size_t n = B.units.size();
  for (size_t i = 0; i < n && i < B.units.size(); i++) if (B.units[i].alive) updateUnit(B.units[i], dt);
  for (size_t i = 0; i < B.units.size(); i++) if (B.units[i].moving || B.units[i].attacking) B.units[i].anim += dt * (UT(B.units[i]).speed * 6.0f + 2.0f);
  separate(); updateProjs(dt); updateSpells(dt);
  for (size_t i = 0; i < B.fx.size(); i++) B.fx[i].t += dt;
  B.fx.erase(std::remove_if(B.fx.begin(), B.fx.end(), [](const Fx &f){ return f.t >= f.T; }), B.fx.end());
  B.units.erase(std::remove_if(B.units.begin(), B.units.end(), [](const Unit &u){ return !u.alive && UT(u).kind != UT_TOWER; }), B.units.end());
  // время
  if (!B.over) {
    if (!B.overtime && B.t >= MATCH_T) { if (B.side[0].crowns != B.side[1].crowns) { B.over = true; B.winner = B.side[0].crowns > B.side[1].crowns ? 0 : 1; } else B.overtime = true; }
    else if (B.overtime && B.t >= MATCH_T + OVER_T) {                                           // ничья по коронам: у кого слабее самая побитая башня
      float lo[2]; for (int s = 0; s < 2; s++) { lo[s] = 1; for (int k = 0; k < 3; k++) { Unit *u = unitById(B.side[s].towerId[k]); lo[s] = fminv(lo[s], u ? u->hp / u->maxhp : 0); } }
      B.over = true; B.winner = fabsf(lo[0] - lo[1]) < 0.001f ? -1 : (lo[0] > lo[1] ? 0 : 1); }
    if (B.over) bEvent(BE_END, B.winner, 0, 0, 0);
  }
}
// фиксированный шаг; возвращает долю до следующего шага (для плавной отрисовки)
static float simRun(float dt) { B.acc += fminv(dt, 0.25f); while (B.acc >= SIM_DT) { simStep(SIM_DT); B.acc -= SIM_DT; } return B.acc / SIM_DT; }

// =====================================================================
//  БОТ
// =====================================================================
struct Bot { float thinkT, react, smart; int team, side; float holdUntil; int lastLane; };
static Bot bot;
static void botInit(int side, int difficulty) { memset(&bot, 0, sizeof bot); bot.side = side; bot.team = teamOf(side); bot.react = 1.4f - 0.25f * difficulty; bot.smart = 0.45f + 0.1f * difficulty; bot.thinkT = 2.0f; bot.lastLane = rand() & 1; }
static int botHandCard(int slot) { Side &s = B.side[bot.side]; return s.deck[s.hand[slot]]; }
// оценка угрозы на своей половине
struct Threat { float elix, x, z, hp; int n, air, swarm, tank; };
static Threat botThreat() {
  Threat t; memset(&t, 0, sizeof t); float sx = 0, sz = 0;
  for (size_t i = 0; i < B.units.size(); i++) { const Unit &u = B.units[i]; if (!u.alive || u.team == bot.team || UT(u).kind != UT_TROOP) continue;
    float lz = zOf(bot.team, u.z); if (lz > 17.5f) continue;                    // ещё далеко
    t.n++; t.hp += u.hp; sx += u.x; sz += u.z; if (UT(u).air) t.air++; if (u.maxhp < 300) t.swarm++; if (UT(u).onlyBuild || u.maxhp > 2500) t.tank++; }
  if (t.n) { t.x = sx / t.n; t.z = sz / t.n; t.elix = t.hp / 500.0f; }
  return t;
}
static bool botTry(int slot, float x, float z) { return playCard(bot.side, slot, x, z); }
static void botThink(float dt) {
  if (B.over) return;
  bot.thinkT -= dt; if (bot.thinkT > 0) return;
  bot.thinkT = bot.react * (0.7f + 0.6f * (rand() / (float)RAND_MAX));
  Side &s = B.side[bot.side]; Threat th = botThreat();
  if (B.duo) { bool otherSide = bot.side >= 2; if (th.n > 0 && ((th.x < 9) == otherSide) && (rand() % 3)) th.n = 0; }   // в 2 на 2 напарники делят линии
  float myKingZ = zOf(bot.team, 3.0f);
  // защита
  if (th.n > 0) {
    int best = -1; float bestScore = -1;
    for (int i = 0; i < 4; i++) { int c = botHandCard(i); const Card &cd = CARDS[c]; if (cd.cost > s.elixir + 0.01f || cd.unit == U_COLLECTOR) continue; float sc = 0;
      if (cd.type == CT_SPELL) { if (cd.spell == SP_HEAL) continue; if (th.swarm >= 2 && (cd.spell == SP_ARROWS || cd.spell == SP_ZAP || cd.spell == SP_FIREBALL)) sc = 3 + th.swarm; else if (th.n >= 3 && cd.spell == SP_FIREBALL) sc = 2.5f;
        else if (cd.spell == SP_ROCKET && th.hp >= 2000 && th.n >= 2) sc = 2.0f; else if (cd.spell == SP_FREEZE && th.hp >= 2500 && zOf(bot.team, th.z) < 9.0f) sc = 2.2f; else continue; }
      else { const UnitT &ut = UNITS[cd.unit];
        if (th.air && !ut.canAir) sc -= 3;
        if (th.tank && (cd.type == CT_BUILD || ut.dmg * imax(1, cd.count) > 300)) sc += 3;
        if (th.swarm >= 2 && (ut.splash > 0)) sc += 3;
        if (th.air && ut.canAir) sc += 2.5f;
        if (ut.onlyBuild) sc -= 4;
        sc += 2 - cd.cost * 0.25f; }
      if ((rand() / (float)RAND_MAX) > bot.smart) sc += (rand() % 3) - 1;
      if (sc > bestScore) { bestScore = sc; best = i; } }
    if (best >= 0 && bestScore > 0) {
      const Card &cd = CARDS[botHandCard(best)];
      float px, pz;
      if (cd.type == CT_SPELL) { px = th.x; pz = th.z; }
      else if (cd.type == CT_BUILD) { px = 9.0f; pz = zOf(bot.team, 9.5f); }
      else { px = clampf(th.x + (th.x < 9 ? 1.5f : -1.5f), 1.5f, AW - 1.5f); pz = zOf(bot.team, clampf(zOf(bot.team, th.z) - 3.0f, 4.0f, 13.0f)); }
      if (!botTry(best, px, pz)) botTry(best, 9.0f, myKingZ + (bot.team ? -2.5f : 2.5f));
      return; }
  }
  if (s.elixir >= 7.0f) { bool calm = true; for (size_t i = 0; i < B.units.size(); i++) { const Unit &u = B.units[i]; if (u.alive && u.team != bot.team && UT(u).kind == UT_TROOP && zOf(bot.team, u.z) < 17.0f) calm = false; }   // нет врагов на моей половине - ставлю сборник
    if (calm) for (int i = 0; i < 4; i++) if (CARDS[botHandCard(i)].unit == U_COLLECTOR && CARDS[botHandCard(i)].cost <= s.elixir && botTry(i, 9.0f + ((rand() & 1) ? 2.5f : -2.5f), zOf(bot.team, 6.5f))) return; }
  // атака: танк или «ломатель» башен, за ним поддержка; полный эликсир не копим зря
  bool tankOut = false; float tankX = 0, tankZ = 0;
  for (size_t i = 0; i < B.units.size(); i++) { const Unit &u = B.units[i]; if (u.alive && u.team == bot.team && UT(u).kind == UT_TROOP && (UT(u).onlyBuild || u.maxhp > 1200) && zOf(bot.team, u.z) > 4.0f) { tankOut = true; tankX = u.x; tankZ = u.z; } }
  Unit *pl = unitById(B.side[1 - bot.team].towerId[1]), *pr = unitById(B.side[1 - bot.team].towerId[2]);
  int lane = bot.lastLane; if (pl && pr) lane = pl->hp <= pr->hp ? 0 : 1; else if (!pl) lane = 0; else if (!pr) lane = 1;
  float lx = BRIDGE_X[lane];
  if (tankOut && s.elixir >= 2) {                                   // поддержка за танком
    for (int i = 0; i < 4; i++) { const Card &cd = CARDS[botHandCard(i)]; if (cd.cost > s.elixir) continue;
      if (cd.type == CT_SPELL) { if (cd.spell == SP_HEAL || cd.spell == SP_FREEZE || cd.spell == SP_ZAP) continue;
        // заклинание по защитникам у танка
        int near = 0; float sx = 0, sz = 0; for (size_t k = 0; k < B.units.size(); k++) { const Unit &u = B.units[k]; if (!u.alive || u.team == bot.team || UT(u).kind != UT_TROOP) continue; float dx = u.x - tankX, dz = u.z - tankZ; if (dx * dx + dz * dz < 16) { near++; sx += u.x; sz += u.z; } }
        if (near >= 3 && botTry(i, sx / near, sz / near)) return; continue; }
      if (cd.type == CT_BUILD) continue; const UnitT &ut = UNITS[cd.unit]; if (ut.onlyBuild) continue;
      float pz = zOf(bot.team, clampf(zOf(bot.team, tankZ) - 1.5f, 2.0f, 14.5f)); if (botTry(i, clampf(tankX, 1.0f, AW - 1.0f), pz)) return; } }
  { int myN = 0; float mx = 0, mz = 0, lost = 0; for (size_t k = 0; k < B.units.size(); k++) { const Unit &u = B.units[k]; if (!u.alive || u.team != bot.team || UT(u).kind != UT_TROOP || !u.attacking) continue; myN++; mx += u.x; mz += u.z; lost += u.maxhp - u.hp; }
    if (myN >= 2) { mx /= myN; mz /= myN;
      for (int i = 0; i < 4; i++) { const Card &cd = CARDS[botHandCard(i)]; if (cd.type != CT_SPELL || cd.cost > s.elixir) continue;
        if (cd.spell == SP_HEAL && lost > 700 && botTry(i, mx, mz)) return;                                           // лечим свою дерущуюся толпу
        if (cd.spell == SP_FREEZE && zOf(bot.team, mz) > 18.0f) { Unit *tw = NULL; float bd = 1e9f; for (int q = 0; q < 3; q++) { Unit *t2 = unitById(B.side[1 - bot.team].towerId[q]); if (t2) { float d2 = (t2->x - mx) * (t2->x - mx) + (t2->z - mz) * (t2->z - mz); if (d2 < bd) { bd = d2; tw = t2; } } }
          if (tw && bd < 36.0f && botTry(i, (tw->x + mx) * 0.5f, (tw->z + mz) * 0.5f)) return; } } } }        // замораживаем башню, пока наши её бьют
  for (int i = 0; i < 4; i++) { const Card &cd = CARDS[botHandCard(i)]; if (cd.unit != U_HOG || cd.type != CT_TROOP || cd.cost > s.elixir) continue;
    int en = 1 - bot.team; float enEl = 0; for (int q = 0; q < B.nSides; q++) if (teamOf(q) == en) enEl = fmaxv(enEl, B.side[q].elixir);
    int best = -1; float bestDef = 1e9f, bestTw = 1e9f;
    for (int ln = 0; ln < 2; ln++) { Unit *tw = unitById(B.side[en].towerId[ln + 1]); float twh = tw ? tw->hp : 0, def = 0;
      for (size_t k = 0; k < B.units.size(); k++) { const Unit &u = B.units[k]; if (!u.alive || u.team != en || UT(u).kind == UT_TOWER || UT(u).air) continue; if (fabsf(u.x - BRIDGE_X[ln]) < 5.5f && zOf(bot.team, u.z) > 13.0f) def += UT(u).kind == UT_BUILD ? 3.0f : 1.0f; }   // защитники на линии
      if (def < bestDef || (def == bestDef && twh < bestTw)) { bestDef = def; bestTw = twh; best = ln; } }
    bool push = (enEl < 2.5f && s.elixir >= 6.0f) || (bestDef == 0 && s.elixir >= 8.0f);                        // всадник бежит, когда врагу нечем ответить
    if (best >= 0 && push && botTry(i, BRIDGE_X[best] + (best ? -0.4f : 0.4f), zOf(bot.team, 14.5f))) { bot.lastLane = best; return; } }
  bool go = s.elixir >= 9.5f || (s.elixir >= 7.0f && (rand() % 3) == 0) || (doubleElixir() && s.elixir >= 5.5f);
  if (!tankOut && go) {
    int best = -1; float bs = -1;
    for (int i = 0; i < 4; i++) { const Card &cd = CARDS[botHandCard(i)]; if (cd.cost > s.elixir || cd.type != CT_TROOP || cd.unit == U_HOG) continue; const UnitT &ut = UNITS[cd.unit];   /* всадника ставим только по своему правилу */
      float sc = ut.hp * imax(1, cd.count) / 1000.0f + (ut.onlyBuild ? 3 : 0); if (sc > bs) { bs = sc; best = i; } }
    if (best >= 0) { const Card &cd = CARDS[botHandCard(best)]; const UnitT &ut = UNITS[cd.unit];
      float pz = (ut.speed >= 1.5f || s.elixir < 9) ? zOf(bot.team, 14.0f) : zOf(bot.team, 1.5f);
      bot.lastLane = lane; if (botTry(best, lx + (lane ? -0.5f : 0.5f), pz)) return; if (botTry(best, 9.0f, zOf(bot.team, 2.0f))) return; }
    // метеорит или огненный шар по башне, если почти сломана
    for (int i = 0; i < 4; i++) { const Card &cd = CARDS[botHandCard(i)]; if (cd.type != CT_SPELL || cd.cost > s.elixir) continue; Unit *tw = unitById(B.side[1 - bot.team].towerId[lane + 1]); if (!tw) tw = unitById(B.side[1 - bot.team].towerId[0]);
      if (tw && (cd.spell == SP_ROCKET || cd.spell == SP_FIREBALL) && tw->hp < cd.sDmg * lvlK(B.side[bot.side].lvl[B.side[bot.side].hand[i]]) * cd.sTowerK * 1.05f) { if (botTry(i, tw->x, tw->z)) return; } }
    for (int i = 0; i < 4; i++) { const Card &cd = CARDS[botHandCard(i)]; if (cd.spell != SP_BARREL || cd.cost > s.elixir) continue;   // бочка с гоблинами - прямо к башне
      Unit *tw = unitById(B.side[1 - bot.team].towerId[lane + 1]); if (!tw) tw = unitById(B.side[1 - bot.team].towerId[0]);
      if (tw && botTry(i, tw->x + (tw->x < 9 ? 1.2f : -1.2f), tw->z + (bot.team == 0 ? -1.6f : 1.6f))) return; }
    if (s.elixir >= 9.8f) for (int i = 0; i < 4; i++) { const Card &cd = CARDS[botHandCard(i)]; if (cd.type == CT_TROOP && cd.unit != U_HOG && cd.cost <= s.elixir && botTry(i, lx, zOf(bot.team, 13.5f))) return; }   // не копим лишнее
  }

}

// =====================================================================
//  БИТВА БАШЕН: отрисовка арены, юнитов, башен, снарядов и эффектов
// =====================================================================
static int viewTeam = 0;                                   // за кого смотрим: своя сторона всегда внизу
static inline V3 RP(float x, float y, float z) { return viewTeam == 0 ? v3(x, y, z) : v3(AW - x, y, AL - z); }
static inline float RY(float yaw) { return viewTeam == 0 ? yaw : yaw + 3.14159265f; }
static const u32 COL_ME = RGB(60, 120, 230), COL_EN = RGB(220, 60, 50);
static u32 teamCol(int team) { return team == viewTeam ? COL_ME : COL_EN; }
static u32 teamDark(int team) { return team == viewTeam ? RGB(30, 60, 140) : RGB(130, 30, 25); }

// ---- камера: сверху под углом, своя сторона снизу ----
static float arenaTopY = 0, arenaBotY = 0;                 // экранные границы, куда помещаем арену
static void setupArenaCam(int areaY0, int areaY1) {
  float h = (float)(areaY1 - areaY0), w = (float)IW;
  // дальний край (z=32) у верхней границы, ближний (z=0) - у нижней
  V3 eye = v3(AW * 0.5f, 34.0f, -5.5f), tg = v3(AW * 0.5f, 0.0f, 15.0f);
  setCamera(eye, tg);
  vcx = IW * 0.5f; vcy = 0;
  // подбираем фокус и центр так, чтобы ширина 18 у ближних башен влезла и длина 32 тоже
  V3 nearL = toView(v3(0.2f, 0, 3.0f)), nearR = toView(v3(AW - 0.2f, 0, 3.0f)), far = toView(v3(AW * 0.5f, 3.0f, AL + 1.2f)), nearC = toView(v3(AW * 0.5f, 0, -0.8f));
  float fw = w / ((nearR.x / nearR.z) - (nearL.x / nearL.z));
  float yFar = far.y / far.z, yNear = nearC.y / nearC.z;            // в долях фокуса
  float fh = h / (yFar - yNear);
  focal = fminv(fw, fh);
  vcy = areaY0 + h * 0.5f + focal * (yFar + yNear) * 0.5f;
  arenaTopY = (float)areaY0; arenaBotY = (float)areaY1;
}
// экран -> клетка арены (в мировых координатах боя)
static bool screenToArena(int sx, int sy, float &wx, float &wz) {
  V3 d = camF + camR * ((sx - vcx) / focal) + camU * (-(sy - vcy) / focal);
  if (d.y > -1e-4f) return false; float t = -camEye.y / d.y; V3 p = camEye + d * t;
  if (viewTeam == 0) { wx = p.x; wz = p.z; } else { wx = AW - p.x; wz = AL - p.z; }
  return true;
}

// ---- примитивы в мировых координатах боя ----
static void BX(float x, float y, float z, float hx, float hy, float hz, float yaw, u32 col) { drawBox(RP(x, y, z), v3(hx, hy, hz), RY(yaw), col, true); }
static void BXF(float x, float y, float z, float hx, float hy, float hz, float yaw, u32 col) { drawBox(RP(x, y, z), v3(hx, hy, hz), RY(yaw), col, false); }
static void GQ(float x0, float z0, float x1, float z1, float y, u32 col) { V3 a = RP(x0, 0, z0), b = RP(x1, 0, z1); groundQuad(fminv(a.x, b.x), fminv(a.z, b.z), fmaxv(a.x, b.x), fmaxv(a.z, b.z), y, col); }
static void DISC(float x, float z, float r, float y, u32 col) { V3 a = RP(x, 0, z); discOct(a.x, a.z, r, y, col); }
static void GLOW(float x, float y, float z, float r, u32 col, int a) {
  int sx, sy; float zz; if (!projectPt(RP(x, y, z), sx, sy, zz)) return; if (sx < -IW || sx > 2 * IW || sy < -IH || sy > 2 * IH) return; int rr = (int)(r * focal / zz); if (rr < 1) rr = 1; if (rr > IH / 5) rr = IH / 5;
  fillCircle(sx, sy, rr, col, a); if (rr > 2) fillCircle(sx, sy, imax(1, rr / 2), mixc(col, RGB(255, 255, 255), 0.6f), imin(255, a + 40));
}
// локальная точка относительно юнита: ox вправо, oz вперёд по взгляду
static inline void LP(float x, float z, float yaw, float ox, float oz, float &rx, float &rz) { float cs = cosf(yaw), sn = sinf(yaw); rx = x + cs * ox + sn * oz; rz = z - sn * ox + cs * oz; }
#define PART(ox, oy, oz, hx, hy, hz, col) { float _x, _z; LP(x, z, yaw, (ox) * s, (oz) * s, _x, _z); BX(_x, y + (oy) * s, _z, (hx) * s, (hy) * s, (hz) * s, yaw, col); }

// ---- человечек: ноги, туловище, голова, руки, оружие и шапка ----
enum { WP_NONE, WP_SWORD, WP_BOW, WP_STAFF, WP_SPEAR, WP_XBOW, WP_DAGGER, WP_BOMB, WP_CLUB, WP_SCEPTER, WP_PIPE };
enum { HT_NONE, HT_HELM, HT_HOOD, HT_WIZARD, HT_CROWN, HT_EARS, HT_SKULL, HT_BEARD };
static u32 gHair = 0;          // не 0 - девушка: длинные волосы этого цвета, юбка, ресницы, губы
static u32 gEyes = 0;          // не 0 - глаза-огоньки этого цвета в тёмных глазницах (скелет, некромант)
static u32 gPads = 0;          // не 0 - наплечники этого цвета
static u32 gBrow = 0;          // не 0 - цвет бровей (иначе тёмные)
static bool gShieldOn = true;  // щит новобранца ещё цел
static void humanoid(float x, float y, float z, float yaw, float s, u32 skin, u32 body, u32 leg, u32 team, float walk, float atk, int weapon, int hat, u32 wcol, bool shield) {
  float sw = sinf(walk) * 0.09f, sw2 = -sw;
  PART(-0.09f, 0.17f, sw, 0.055f, 0.17f, 0.06f, leg); PART(0.09f, 0.17f, sw2, 0.055f, 0.17f, 0.06f, leg);
  PART(-0.09f, 0.025f, sw + 0.03f, 0.06f, 0.025f, 0.08f, RGB(60, 45, 35)); PART(0.09f, 0.025f, sw2 + 0.03f, 0.06f, 0.025f, 0.08f, RGB(60, 45, 35));
  bool girl = gHair != 0;
  if (girl) { PART(0, 0.52f, 0, 0.14f, 0.15f, 0.1f, body); PART(0, 0.3f, 0, 0.19f, 0.09f, 0.14f, mixc(body, team, 0.35f)); }   // талия уже, юбка колоколом
  else PART(0, 0.5f, 0, 0.17f, 0.17f, 0.11f, body);
  PART(0, 0.5f, girl ? 0.102f : 0.112f, girl ? 0.08f : 0.1f, 0.13f, 0.005f, team);       // нагрудник цвета команды
  PART(0, girl ? 0.4f : 0.36f, 0, girl ? 0.145f : 0.175f, 0.025f, girl ? 0.105f : 0.115f, mixc(body, RGB(0, 0, 0), 0.35f));   // пояс
  PART(0, 0.8f, 0, girl ? 0.11f : 0.12f, girl ? 0.115f : 0.12f, girl ? 0.11f : 0.12f, skin);
  if (girl) { float fz = 0.111f;
    PART(-0.045f, 0.815f, fz, 0.022f, 0.026f, 0.003f, RGB(255, 255, 255)); PART(0.045f, 0.815f, fz, 0.022f, 0.026f, 0.003f, RGB(255, 255, 255));   // большие глаза
    PART(-0.045f, 0.81f, fz + 0.002f, 0.014f, 0.018f, 0.003f, RGB(60, 110, 60)); PART(0.045f, 0.81f, fz + 0.002f, 0.014f, 0.018f, 0.003f, RGB(60, 110, 60));
    PART(-0.045f, 0.845f, fz + 0.003f, 0.028f, 0.006f, 0.003f, RGB(25, 20, 20)); PART(0.045f, 0.845f, fz + 0.003f, 0.028f, 0.006f, 0.003f, RGB(25, 20, 20));   // ресницы
    PART(0, 0.755f, fz, 0.025f, 0.008f, 0.003f, RGB(210, 70, 90));                                                                                       // губы
    PART(-0.075f, 0.775f, fz, 0.018f, 0.012f, 0.002f, RGB(250, 160, 160)); PART(0.075f, 0.775f, fz, 0.018f, 0.012f, 0.002f, RGB(250, 160, 160));   // румянец
    // волосы: макушка, чёлка, пряди по бокам и длинные сзади, хвост
    PART(0, 0.9f, -0.01f, 0.122f, 0.045f, 0.125f, gHair); PART(0, 0.87f, 0.105f, 0.12f, 0.025f, 0.012f, gHair);
    PART(-0.115f, 0.74f, 0.02f, 0.02f, 0.13f, 0.1f, gHair); PART(0.115f, 0.74f, 0.02f, 0.02f, 0.13f, 0.1f, gHair);
    PART(0, 0.72f, -0.115f, 0.115f, 0.17f, 0.02f, gHair); PART(0, 0.5f, -0.14f, 0.06f, 0.14f, 0.03f, gHair); PART(0, 0.36f, -0.15f, 0.04f, 0.05f, 0.025f, mixc(gHair, RGB(0, 0, 0), 0.2f)); }
  else if (gEyes) { float fz = 0.121f;                                                     // тёмные глазницы со светящимися точками
    PART(-0.05f, 0.82f, fz, 0.032f, 0.03f, 0.003f, RGB(25, 22, 28)); PART(0.05f, 0.82f, fz, 0.032f, 0.03f, 0.003f, RGB(25, 22, 28));
    PART(-0.05f, 0.82f, fz + 0.002f, 0.013f, 0.013f, 0.003f, gEyes); PART(0.05f, 0.82f, fz + 0.002f, 0.013f, 0.013f, 0.003f, gEyes); }
  else { float fz = 0.121f; u32 br = gBrow ? gBrow : mixc(skin, RGB(30, 20, 15), 0.7f);   // лицо: белки, зрачки, брови, нос, рот
    PART(-0.05f, 0.82f, fz, 0.026f, 0.026f, 0.003f, RGB(250, 250, 250)); PART(0.05f, 0.82f, fz, 0.026f, 0.026f, 0.003f, RGB(250, 250, 250));
    PART(-0.045f, 0.815f, fz + 0.002f, 0.014f, 0.017f, 0.003f, RGB(25, 25, 35)); PART(0.045f, 0.815f, fz + 0.002f, 0.014f, 0.017f, 0.003f, RGB(25, 25, 35));
    PART(-0.052f, 0.862f, fz, 0.032f, 0.008f, 0.004f, br); PART(0.052f, 0.862f, fz, 0.032f, 0.008f, 0.004f, br);
    PART(0, 0.78f, fz + 0.012f, 0.016f, 0.022f, 0.014f, mixc(skin, RGB(120, 60, 40), 0.18f));
    PART(0, 0.735f, fz, 0.035f, 0.007f, 0.003f, mixc(skin, RGB(90, 30, 30), 0.6f)); }
  // руки: правая с оружием выносится вперёд при ударе
  float a = atk;  float raZ = 0.04f + a * 0.18f, raY = 0.5f + a * 0.12f;
  PART(0.23f, raY, raZ, 0.05f, 0.14f, 0.05f, body); PART(0.23f, raY - 0.16f, raZ + 0.02f, 0.045f, 0.04f, 0.045f, skin);
  PART(-0.23f, 0.5f, 0.04f - sw * 0.5f, 0.05f, 0.14f, 0.05f, body); PART(-0.23f, 0.34f, 0.06f - sw * 0.5f, 0.045f, 0.04f, 0.045f, skin);
  PART(-0.23f, 0.4f, 0.04f - sw * 0.5f, 0.054f, 0.02f, 0.054f, mixc(body, RGB(0, 0, 0), 0.3f)); PART(0.23f, raY - 0.1f, raZ, 0.054f, 0.02f, 0.054f, mixc(body, RGB(0, 0, 0), 0.3f));   // манжеты
  PART(0, 0.69f, 0, 0.06f, 0.025f, 0.06f, mixc(skin, RGB(0, 0, 0), 0.15f));                                                    // шея
  if (gPads) { PART(-0.23f, 0.65f, 0.03f, 0.075f, 0.04f, 0.075f, gPads); PART(0.23f, raY + 0.15f, raZ - 0.01f, 0.075f, 0.04f, 0.075f, gPads);
    PART(-0.23f, 0.69f, 0.03f, 0.06f, 0.012f, 0.06f, mixc(gPads, RGB(255, 255, 255), 0.3f)); PART(0.23f, raY + 0.19f, raZ - 0.01f, 0.06f, 0.012f, 0.06f, mixc(gPads, RGB(255, 255, 255), 0.3f)); }
  float hx = 0.23f, hy = raY - 0.16f, hz = raZ + 0.06f;
  switch (weapon) {
    case WP_SWORD: PART(hx, hy + 0.25f - a * 0.1f, hz + a * 0.15f, 0.018f, 0.22f, 0.035f, RGB(215, 220, 230)); PART(hx, hy + 0.03f, hz, 0.06f, 0.015f, 0.025f, RGB(140, 110, 50)); break;
    case WP_CLUB: PART(hx, hy + 0.12f, hz + 0.02f + a * 0.15f, 0.03f, 0.1f, 0.03f, RGB(100, 70, 40)); PART(hx, hy + 0.3f, hz + 0.04f + a * 0.2f, 0.055f, 0.1f, 0.055f, RGB(125, 88, 52)); break;   // рукоять в кулаке, утолщение сверху
    case WP_SCEPTER: PART(hx, hy + 0.2f, hz + a * 0.15f, 0.018f, 0.2f, 0.018f, wcol ? wcol : RGB(230, 190, 60)); PART(hx, hy + 0.43f, hz + a * 0.15f, 0.045f, 0.045f, 0.045f, RGB(80, 220, 120)); PART(hx, hy + 0.43f, hz + a * 0.15f, 0.06f, 0.012f, 0.06f, RGB(230, 190, 60)); break;
    case WP_PIPE: PART(0.04f, 0.77f, 0.28f + a * 0.04f, 0.024f, 0.024f, 0.2f, RGB(200, 160, 80)); PART(0.04f, 0.77f, 0.49f + a * 0.04f, 0.036f, 0.036f, 0.016f, RGB(110, 70, 30)); PART(0.23f, 0.62f, 0.3f, 0.045f, 0.045f, 0.045f, RGB(110, 180, 80)); break;   // длинная дудка у рта, рука держит   // дудка у рта
    case WP_DAGGER: PART(hx, hy + 0.08f, hz + 0.05f + a * 0.1f, 0.015f, 0.08f, 0.025f, RGB(200, 205, 215)); break;
    case WP_SPEAR: PART(hx, hy + 0.05f, hz + 0.35f + a * 0.25f, 0.018f, 0.018f, 0.55f, RGB(130, 95, 60)); PART(hx, hy + 0.05f, hz + 0.95f + a * 0.25f, 0.035f, 0.035f, 0.08f, RGB(215, 220, 230)); break;
    case WP_STAFF: PART(hx, hy + 0.25f, hz, 0.022f, 0.42f, 0.022f, RGB(110, 80, 50)); PART(hx, hy + 0.7f, hz, 0.06f, 0.06f, 0.06f, wcol); break;
    case WP_BOW: { float pull = 1.0f - a;                                // чем дальше от выстрела, тем сильнее натянута тетива
      PART(-0.1f, 0.5f, 0.3f, 0.015f, 0.26f, 0.02f, RGB(120, 80, 45)); PART(-0.1f, 0.5f, 0.27f - pull * 0.12f, 0.004f, 0.22f, 0.004f, RGB(230, 230, 220));
      if (a < 0.5f) { PART(-0.05f, 0.5f, 0.25f - pull * 0.05f, 0.012f, 0.012f, 0.2f, RGB(150, 110, 70)); PART(-0.05f, 0.5f, 0.46f - pull * 0.05f, 0.022f, 0.022f, 0.03f, RGB(210, 210, 220)); } } break;
    case WP_XBOW: PART(0, 0.48f, 0.28f, 0.03f, 0.03f, 0.22f, RGB(110, 75, 45)); PART(0, 0.5f, 0.42f, 0.2f, 0.02f, 0.025f, RGB(70, 60, 55)); break;
    case WP_BOMB: PART(hx, hy + 0.06f + a * 0.2f, hz + a * 0.1f, 0.08f, 0.08f, 0.08f, RGB(30, 30, 34)); PART(hx, hy + 0.16f + a * 0.2f, hz + a * 0.1f, 0.012f, 0.03f, 0.012f, RGB(255, 200, 60)); break;
  }
  if (shield) { PART(-0.29f, 0.48f, 0.12f, 0.03f, 0.15f, 0.13f, team); PART(-0.31f, 0.48f, 0.12f, 0.005f, 0.06f, 0.05f, RGB(230, 210, 120)); }
  switch (hat) {
    case HT_HELM: PART(0, 0.88f, -0.005f, 0.13f, 0.07f, 0.13f, RGB(170, 175, 185)); PART(0, 0.79f, 0.123f, 0.1f, 0.012f, 0.004f, RGB(60, 60, 66)); PART(0, 1.0f, -0.03f, 0.02f, 0.06f, 0.08f, team); break;
    case HT_HOOD: PART(0, 0.86f, -0.02f, 0.135f, 0.1f, 0.12f, mixc(body, RGB(0, 0, 0), 0.15f)); break;
    case HT_WIZARD: PART(0, 0.93f, 0, 0.17f, 0.015f, 0.17f, wcol); PART(0, 1.0f, 0, 0.1f, 0.07f, 0.1f, wcol); PART(0, 1.12f, -0.02f, 0.06f, 0.06f, 0.06f, wcol); PART(0, 1.22f, -0.04f, 0.03f, 0.05f, 0.03f, wcol); break;
    case HT_CROWN: PART(0, 0.95f, 0, 0.11f, 0.03f, 0.11f, RGB(240, 200, 50)); for (int k = 0; k < 4; k++) { float cx = (k & 1 ? 1 : -1) * 0.08f, cz = (k & 2 ? 1 : -1) * 0.08f; PART(cx, 0.99f, cz, 0.02f, 0.03f, 0.02f, RGB(250, 215, 60)); } break;
    case HT_EARS: PART(-0.16f, 0.84f, 0, 0.06f, 0.025f, 0.02f, skin); PART(0.16f, 0.84f, 0, 0.06f, 0.025f, 0.02f, skin); break;
    case HT_BEARD: PART(0, 0.73f, 0.1f, 0.1f, 0.07f, 0.03f, RGB(230, 225, 215)); PART(0, 0.95f, 0, 0.13f, 0.05f, 0.13f, RGB(200, 60, 50)); PART(0, 1.03f, 0, 0.07f, 0.05f, 0.07f, RGB(200, 60, 50)); break;
  }
}
// ---- модели ----
static void drawModel(int model, int team, float x, float y, float z, float yaw, float walk, float atk, float flash, bool frozen, float life01) {
  u32 tc = teamCol(team), td = teamDark(team);
  if (flash > 0) tc = mixc(tc, RGB(255, 255, 255), 0.6f);
  u32 skin = RGB(240, 200, 160);
  #define FZ(c) (frozen ? mixc((c), RGB(170, 220, 255), 0.55f) : (flash > 0 ? mixc((c), RGB(255, 255, 255), 0.35f) : (c)))
  float s = 1, US = model < M_CANNON ? 1.35f : 1.0f;            // войска крупнее: на телефоне их лучше видно
  switch (model) {
    case M_KNIGHT: s = 1.25f * US; gPads = FZ(RGB(185, 190, 200)); gBrow = RGB(110, 70, 40); humanoid(x, y, z, yaw, s, FZ(skin), FZ(RGB(150, 155, 165)), FZ(RGB(90, 90, 100)), FZ(tc), walk, atk, WP_SWORD, HT_HELM, 0, true); gPads = 0; gBrow = 0;
      PART(0, 0.42f, -0.125f, 0.16f, 0.26f, 0.012f, FZ(td)); PART(0, 0.36f, 0.118f, 0.03f, 0.022f, 0.004f, FZ(RGB(230, 200, 80))); break;   // плащ и пряжка
    case M_ARCHER: s = 1.05f * US; gHair = FZ(RGB(200, 90, 40)); humanoid(x, y, z, yaw, s, FZ(RGB(250, 215, 185)), FZ(RGB(90, 150, 80)), FZ(RGB(70, 100, 60)), FZ(tc), walk, atk, WP_BOW, HT_NONE, 0, false); gHair = 0;
      PART(0.0f, 0.95f, -0.05f, 0.05f, 0.02f, 0.05f, FZ(RGB(240, 230, 120)));   // заколка
      PART(0.12f, 0.55f, -0.17f, 0.045f, 0.14f, 0.045f, FZ(RGB(120, 80, 45))); for (int k = 0; k < 3; k++) PART(0.1f + k * 0.02f, 0.72f, -0.17f, 0.008f, 0.04f, 0.008f, FZ(RGB(240, 240, 240))); break;   // колчан
    case M_GOBLIN: s = 0.8f * US; humanoid(x, y, z, yaw, s, FZ(RGB(110, 180, 80)), FZ(RGB(120, 90, 60)), FZ(RGB(90, 70, 50)), FZ(tc), walk, atk, WP_DAGGER, HT_EARS, 0, false);
      PART(-0.23f, 0.87f, -0.01f, 0.04f, 0.018f, 0.015f, FZ(RGB(100, 170, 70))); PART(0.23f, 0.87f, -0.01f, 0.04f, 0.018f, 0.015f, FZ(RGB(100, 170, 70)));   // кончики ушей
      PART(0, 0.78f, 0.15f, 0.025f, 0.03f, 0.03f, FZ(RGB(95, 160, 65))); PART(-0.03f, 0.725f, 0.124f, 0.008f, 0.014f, 0.004f, RGB(250, 250, 240)); PART(0.03f, 0.725f, 0.124f, 0.008f, 0.014f, 0.004f, RGB(250, 250, 240));
      PART(0, 0.9f, -0.005f, 0.124f, 0.025f, 0.124f, FZ(tc)); break;   // нос, клыки, повязка
    case M_GIANT: s = 2.1f * US; humanoid(x, y, z, yaw, s, FZ(RGB(230, 180, 130)), FZ(RGB(150, 110, 70)), FZ(RGB(110, 80, 55)), FZ(tc), walk, atk, WP_CLUB, HT_NONE, 0, false);
      PART(0, 0.9f, 0, 0.125f, 0.035f, 0.125f, FZ(RGB(170, 110, 60)));     // волосы
      PART(0, 0.72f, 0.1f, 0.1f, 0.035f, 0.025f, FZ(RGB(150, 95, 55))); PART(0, 0.36f, 0.12f, 0.04f, 0.03f, 0.005f, FZ(RGB(220, 190, 90)));   // щетина, бляха
      PART(0, 0.55f, 0.113f, 0.1f, 0.012f, 0.003f, FZ(td)); break;
    case M_SKELETON: s = 0.85f * US; gEyes = RGB(120, 220, 255); humanoid(x, y, z, yaw, s, FZ(RGB(235, 235, 225)), FZ(RGB(225, 225, 215)), FZ(RGB(215, 215, 205)), FZ(tc), walk, atk, WP_SWORD, HT_NONE, 0, false); gEyes = 0;
      for (int k = 0; k < 3; k++) PART(0, 0.58f - k * 0.07f, 0.113f, 0.14f, 0.012f, 0.004f, RGB(90, 90, 95));   // рёбра
      PART(0, 0.74f, 0.121f, 0.06f, 0.012f, 0.003f, RGB(60, 60, 64)); for (int k = -2; k <= 2; k++) PART(k * 0.022f, 0.74f, 0.123f, 0.003f, 0.016f, 0.003f, RGB(235, 235, 225)); break;   // зубы
    case M_XBOW: s = 1.15f * US; gPads = FZ(RGB(90, 70, 110)); humanoid(x, y, z, yaw, s, FZ(skin), FZ(RGB(110, 90, 140)), FZ(RGB(70, 60, 80)), FZ(tc), walk, atk, WP_XBOW, HT_HELM, 0, false); gPads = 0;
      PART(-0.13f, 0.55f, -0.16f, 0.04f, 0.12f, 0.04f, FZ(RGB(110, 75, 45))); PART(0, 0.3f, 0.118f, 0.15f, 0.08f, 0.004f, FZ(td)); break;   // колчан, набедренник
    case M_LANCER: s = 1.3f * US; gPads = FZ(RGB(220, 200, 110)); humanoid(x, y, z, yaw, s, FZ(skin), FZ(RGB(200, 200, 210)), FZ(RGB(120, 120, 130)), FZ(tc), walk, atk, WP_SPEAR, HT_HELM, 0, true); gPads = 0;
      PART(0, 1.08f, -0.06f, 0.025f, 0.05f, 0.1f, FZ(tc)); PART(0, 0.42f, -0.125f, 0.17f, 0.27f, 0.012f, FZ(td)); break;   // высокий плюмаж и плащ
    case M_NECRO: s = 1.25f * US; gEyes = RGB(120, 255, 140); humanoid(x, y, z, yaw, s, FZ(RGB(200, 210, 190)), FZ(RGB(50, 40, 60)), FZ(RGB(40, 30, 50)), FZ(tc), walk, atk, WP_STAFF, HT_HOOD, FZ(RGB(120, 255, 140)), false);
      gEyes = 0; PART(0, 0.25f, 0, 0.2f, 0.25f, 0.14f, FZ(RGB(45, 35, 55)));            // мантия
      PART(0, 0.36f, 0.145f, 0.04f, 0.04f, 0.01f, FZ(RGB(225, 225, 210))); PART(-0.015f, 0.365f, 0.156f, 0.01f, 0.01f, 0.002f, RGB(30, 30, 30)); PART(0.015f, 0.365f, 0.156f, 0.01f, 0.01f, 0.002f, RGB(30, 30, 30));   // череп на поясе
      PART(0, 0.48f, 0.142f, 0.012f, 0.2f, 0.004f, FZ(RGB(120, 255, 140))); break;   // светящаяся вышивка
    case M_FIREMAGE: s = 1.2f * US; humanoid(x, y, z, yaw, s, FZ(skin), FZ(RGB(190, 50, 40)), FZ(RGB(150, 40, 30)), FZ(tc), walk, atk, WP_STAFF, HT_WIZARD, FZ(RGB(255, 140, 40)), false);
      PART(0, 0.25f, 0, 0.19f, 0.25f, 0.13f, FZ(RGB(170, 40, 35)));
      PART(0, 0.68f, 0.12f, 0.08f, 0.07f, 0.02f, FZ(RGB(235, 235, 230))); PART(0, 0.36f, 0.135f, 0.13f, 0.02f, 0.004f, FZ(RGB(255, 190, 60))); break;   // борода, золотой пояс
    case M_GOBKING: { s = 1.6f * US; gHair = 0;                     // король гоблинов: зелёный, в короне, с мантией и скипетром
      humanoid(x, y, z, yaw, s, FZ(RGB(110, 180, 80)), FZ(RGB(120, 60, 150)), FZ(RGB(80, 60, 50)), FZ(tc), walk, atk, WP_SCEPTER, HT_CROWN, 0, false);
      PART(-0.16f, 0.82f, 0, 0.07f, 0.03f, 0.025f, FZ(RGB(110, 180, 80))); PART(0.16f, 0.82f, 0, 0.07f, 0.03f, 0.025f, FZ(RGB(110, 180, 80)));   // уши
      PART(0, 0.42f, -0.14f, 0.2f, 0.32f, 0.025f, FZ(RGB(170, 30, 40))); PART(0, 0.73f, -0.1f, 0.2f, 0.04f, 0.06f, FZ(RGB(245, 240, 230)));        // мантия и мех
      PART(0, 0.43f, 0.02f, 0.165f, 0.11f, 0.135f, FZ(RGB(140, 75, 170)));                                                                         // живот
      PART(0, 0.76f, 0.12f, 0.03f, 0.03f, 0.03f, FZ(RGB(90, 150, 60))); PART(0, 0.7f, 0.12f, 0.06f, 0.012f, 0.004f, RGB(240, 240, 230)); } break;   // нос и клыки
    case M_DART: s = 0.82f * US; humanoid(x, y, z, yaw, s, FZ(RGB(110, 180, 80)), FZ(RGB(70, 110, 160)), FZ(RGB(80, 60, 45)), FZ(tc), walk, atk, WP_PIPE, HT_EARS, 0, false);
      PART(0, 0.9f, -0.01f, 0.125f, 0.035f, 0.125f, FZ(RGB(60, 70, 110))); PART(0, 0.86f, 0.122f, 0.11f, 0.02f, 0.003f, FZ(tc));   // повязка на голове
      PART(-0.15f, 0.55f, -0.13f, 0.05f, 0.14f, 0.05f, FZ(RGB(120, 85, 50))); break;                                           // колчан с дротиками
    case M_VOLAR: { s = 1.25f * US; float fl = sinf(walk * 1.4f) * 0.25f; u32 rock = FZ(RGB(120, 55, 40)), lava = FZ(RGB(255, 150, 40));   // лавовый малыш: круглый, с огоньками в трещинах
      PART(0, 0, 0, 0.36f, 0.3f, 0.4f, rock); PART(0, -0.05f, 0.03f, 0.3f, 0.22f, 0.34f, FZ(RGB(90, 50, 40)));
      for (int k = 0; k < 7; k++) { float cx = ((k * 37) % 5 - 2) * 0.12f, cz = ((k * 53) % 5 - 2) * 0.13f; PART(cx, 0.3f, cz, 0.08f, 0.01f, 0.035f, lava); }
      PART(-0.37f, 0.02f, 0.1f, 0.005f, 0.08f, 0.2f, lava); PART(0.37f, 0.02f, 0.1f, 0.005f, 0.08f, 0.2f, lava);                 // светящиеся трещины по бокам
      PART(0, -0.02f, 0.685f, 0.06f, 0.02f, 0.004f, RGB(255, 200, 80));                                                             // улыбка
      PART(0, 0.16f, 0.46f, 0.26f, 0.24f, 0.2f, rock); PART(0, 0.06f, 0.64f, 0.14f, 0.09f, 0.06f, FZ(RGB(95, 55, 45)));      // большая голова и мордочка
      PART(-0.1f, 0.24f, 0.665f, 0.06f, 0.06f, 0.004f, RGB(255, 230, 120)); PART(0.1f, 0.24f, 0.665f, 0.06f, 0.06f, 0.004f, RGB(255, 230, 120));
      PART(-0.1f, 0.23f, 0.67f, 0.03f, 0.035f, 0.004f, RGB(40, 20, 10)); PART(0.1f, 0.23f, 0.67f, 0.03f, 0.035f, 0.004f, RGB(40, 20, 10));   // глаза
      PART(-0.2f, 0.44f, 0.42f, 0.06f, 0.08f, 0.05f, rock); PART(0.2f, 0.44f, 0.42f, 0.06f, 0.08f, 0.05f, rock); PART(-0.2f, 0.5f, 0.43f, 0.03f, 0.03f, 0.03f, lava); PART(0.2f, 0.5f, 0.43f, 0.03f, 0.03f, 0.03f, lava);   // ушки
      PART(-0.5f, 0.12f + fl, -0.05f, 0.24f, 0.02f, 0.2f, FZ(tc)); PART(0.5f, 0.12f + fl, -0.05f, 0.24f, 0.02f, 0.2f, FZ(tc));
      PART(0, 0.05f, -0.5f, 0.06f, 0.06f, 0.14f, rock); PART(0, 0.08f, -0.66f, 0.05f, 0.05f, 0.04f, lava);
      for (int k = 0; k < 4; k++) { float lx = (k & 1 ? 0.2f : -0.2f), lz = (k & 2 ? 0.25f : -0.2f); PART(lx, -0.32f, lz, 0.06f, 0.08f, 0.06f, rock); } } break;
    case M_FSPIRIT: { s = 0.95f * US; float bob = fabsf(sinf(walk * 3.0f)) * 0.06f, lg = sinf(walk * 3.0f) * 0.07f; u32 fo = FZ(RGB(238, 100, 32)), fy = FZ(RGB(255, 190, 60));   // огненный дух
      PART(-0.1f, 0.06f, lg, 0.06f, 0.06f, 0.07f, FZ(RGB(200, 70, 25))); PART(0.1f, 0.06f, -lg, 0.06f, 0.06f, 0.07f, FZ(RGB(200, 70, 25)));
      PART(0, 0.3f + bob, 0, 0.22f, 0.22f, 0.2f, fo); PART(0, 0.26f + bob, 0.1f, 0.15f, 0.14f, 0.12f, fy); PART(0, 0.14f, 0, 0.225f, 0.03f, 0.205f, FZ(tc));
      PART(-0.09f, 0.36f + bob, 0.205f, 0.05f, 0.06f, 0.006f, RGB(255, 255, 240)); PART(0.09f, 0.36f + bob, 0.205f, 0.05f, 0.06f, 0.006f, RGB(255, 255, 240)); PART(-0.09f, 0.35f + bob, 0.212f, 0.025f, 0.035f, 0.004f, RGB(40, 20, 10)); PART(0.09f, 0.35f + bob, 0.212f, 0.025f, 0.035f, 0.004f, RGB(40, 20, 10));
      float fl = sinf(gTime * 14.0f + x * 3) * 0.04f; PART(0, 0.62f + bob, 0, 0.12f, 0.11f + fl, 0.11f, fy); PART(0, 0.78f + bob, 0, 0.07f, 0.09f + fl, 0.07f, FZ(RGB(255, 235, 120)));
      GLOW(x, y + 0.45f * s, z, 0.5f * s, RGB(255, 140, 40), 80); } break;
    case M_ISPIRIT: { s = 0.95f * US; float bob = fabsf(sinf(walk * 2.6f)) * 0.06f, lg = sinf(walk * 2.6f) * 0.07f; u32 ib = FZ(RGB(150, 214, 255)), iw = FZ(RGB(225, 245, 255));   // ледяной дух
      PART(-0.1f, 0.06f, lg, 0.06f, 0.06f, 0.07f, FZ(RGB(110, 170, 225))); PART(0.1f, 0.06f, -lg, 0.06f, 0.06f, 0.07f, FZ(RGB(110, 170, 225)));
      PART(0, 0.3f + bob, 0, 0.22f, 0.22f, 0.2f, ib); PART(0, 0.26f + bob, 0.1f, 0.15f, 0.14f, 0.12f, iw); PART(0, 0.14f, 0, 0.225f, 0.03f, 0.205f, FZ(tc));
      PART(-0.09f, 0.36f + bob, 0.205f, 0.05f, 0.06f, 0.006f, RGB(255, 255, 255)); PART(0.09f, 0.36f + bob, 0.205f, 0.05f, 0.06f, 0.006f, RGB(255, 255, 255)); PART(-0.09f, 0.35f + bob, 0.212f, 0.025f, 0.035f, 0.004f, RGB(20, 40, 80)); PART(0.09f, 0.35f + bob, 0.212f, 0.025f, 0.035f, 0.004f, RGB(20, 40, 80));
      PART(0, 0.62f + bob, 0, 0.07f, 0.13f, 0.07f, iw); PART(-0.12f, 0.58f + bob, 0, 0.045f, 0.1f, 0.045f, ib); PART(0.12f, 0.58f + bob, 0, 0.045f, 0.1f, 0.045f, ib); PART(0, 0.78f + bob, 0, 0.04f, 0.07f, 0.04f, RGB(250, 255, 255));   // ледяные кристаллы на голове
      GLOW(x, y + 0.45f * s, z, 0.5f * s, RGB(170, 225, 255), 70); } break;
    case M_PUP: { s = 0.6f * US; float fl = sinf(walk * 2.5f) * 0.25f; u32 rock = FZ(RGB(110, 50, 30)), lava = FZ(RGB(255, 150, 40));
      PART(0, 0, 0, 0.2f, 0.17f, 0.22f, rock); PART(0, 0.12f, 0.24f, 0.15f, 0.14f, 0.12f, rock);
      PART(-0.06f, 0.15f, 0.365f, 0.035f, 0.035f, 0.003f, RGB(255, 230, 120)); PART(0.06f, 0.15f, 0.365f, 0.035f, 0.035f, 0.003f, RGB(255, 230, 120));
      PART(0, 0.18f, 0, 0.1f, 0.01f, 0.03f, lava); PART(-0.28f, 0.06f + fl, 0, 0.14f, 0.015f, 0.12f, FZ(tc)); PART(0.28f, 0.06f + fl, 0, 0.14f, 0.015f, 0.12f, FZ(tc)); } break;
    case M_RECRUIT: { s = 1.2f * US; gPads = FZ(RGB(150, 150, 160)); gBrow = RGB(90, 60, 35);          // новобранец: кольчуга, котелок-шлем, копьё и щит
      humanoid(x, y, z, yaw, s, FZ(skin), FZ(RGB(130, 135, 145)), FZ(RGB(110, 85, 60)), FZ(tc), walk, atk, WP_SPEAR, HT_NONE, 0, false); gPads = 0; gBrow = 0;
      PART(0, 0.9f, 0, 0.13f, 0.05f, 0.13f, FZ(RGB(160, 165, 175))); PART(0, 0.86f, 0, 0.2f, 0.012f, 0.2f, FZ(RGB(140, 145, 155))); PART(0, 0.955f, 0, 0.05f, 0.02f, 0.05f, FZ(tc));   // шлем-котелок с широкими полями
      PART(0, 0.42f, 0.115f, 0.15f, 0.2f, 0.006f, FZ(tc)); PART(0, 0.5f, 0.122f, 0.05f, 0.05f, 0.003f, FZ(RGB(240, 220, 120)));                                // накидка цвета команды с гербом
      for (int k = 0; k < 4; k++) PART(0, 0.25f + k * 0.012f, 0.0f, 0.18f - k * 0.005f, 0.006f, 0.12f, FZ(RGB(110, 115, 125)));                             // подол кольчуги
      if (gShieldOn) { PART(-0.3f, 0.47f, 0.13f, 0.035f, 0.24f, 0.16f, FZ(RGB(150, 105, 60))); PART(-0.336f, 0.47f, 0.13f, 0.004f, 0.25f, 0.025f, FZ(RGB(120, 120, 130)));
        PART(-0.336f, 0.62f, 0.13f, 0.004f, 0.025f, 0.17f, FZ(RGB(120, 120, 130))); PART(-0.336f, 0.32f, 0.13f, 0.004f, 0.025f, 0.17f, FZ(RGB(120, 120, 130))); PART(-0.338f, 0.47f, 0.13f, 0.004f, 0.05f, 0.05f, FZ(tc)); } } break;   // ростовой щит с оковкой
    case M_ICEWIZ: { s = 1.2f * US; gBrow = RGB(235, 245, 255);                                            // ледяной маг: белая мантия, голубой капюшон, посох с кристаллом
      u32 robe = FZ(RGB(225, 238, 250)), ice = FZ(RGB(120, 200, 245));
      humanoid(x, y, z, yaw, s, FZ(RGB(225, 210, 200)), robe, FZ(RGB(180, 200, 225)), FZ(tc), walk, atk, WP_STAFF, HT_HOOD, RGB(170, 235, 255), false); gBrow = 0;
      PART(0, 0.25f, 0, 0.19f, 0.25f, 0.13f, robe); PART(0, 0.04f, 0, 0.2f, 0.03f, 0.14f, ice);                                                      // длинная мантия с голубой каймой
      PART(0, 0.88f, -0.02f, 0.14f, 0.08f, 0.125f, ice); PART(0, 0.98f, -0.06f, 0.08f, 0.05f, 0.08f, ice); PART(0, 1.06f, -0.1f, 0.035f, 0.04f, 0.035f, ice);   // острый голубой капюшон
      PART(0, 0.66f, 0.12f, 0.085f, 0.1f, 0.02f, FZ(RGB(240, 248, 255))); PART(0, 0.55f, 0.125f, 0.05f, 0.04f, 0.015f, FZ(RGB(240, 248, 255)));          // длинная белая борода
      PART(0, 0.36f, 0.135f, 0.13f, 0.02f, 0.004f, ice); PART(0, 0.5f, 0.112f, 0.06f, 0.06f, 0.004f, FZ(tc));
      { float hx2 = 0.23f, hy2 = 0.5f + atk * 0.12f - 0.16f + 0.7f, hz2 = 0.04f + atk * 0.18f + 0.06f; for (int k = 0; k < 3; k++) { float a = k * 2.094f + gTime * 2; PART(hx2 + cosf(a) * 0.07f, hy2 + 0.04f, hz2 + sinf(a) * 0.07f, 0.02f, 0.05f, 0.02f, RGB(200, 245, 255)); } } } break;   // осколки льда кружат у кристалла
    case M_BARB: { s = 1.2f * US; gBrow = RGB(110, 70, 30);                                              // варвар: голый торс, меховая жилетка, борода, широкий меч
      humanoid(x, y, z, yaw, s, FZ(skin), FZ(RGB(225, 180, 140)), FZ(RGB(110, 80, 55)), FZ(tc), walk, atk, WP_SWORD, HT_NONE, 0, false); gBrow = 0;
      PART(0, 0.52f, -0.02f, 0.175f, 0.14f, 0.1f, FZ(RGB(120, 85, 50))); PART(0, 0.52f, 0.08f, 0.06f, 0.14f, 0.035f, FZ(RGB(225, 180, 140)));    // меховая жилетка, открытая грудь
      PART(0, 0.37f, 0, 0.18f, 0.03f, 0.12f, FZ(RGB(70, 50, 35))); PART(0, 0.37f, 0.12f, 0.035f, 0.025f, 0.006f, FZ(RGB(220, 190, 90)));          // ремень с бляхой
      PART(0, 0.9f, -0.01f, 0.125f, 0.04f, 0.125f, FZ(RGB(120, 75, 35))); PART(0, 0.86f, -0.115f, 0.12f, 0.1f, 0.02f, FZ(RGB(120, 75, 35)));  // волосы
      PART(0, 0.88f, 0.121f, 0.125f, 0.012f, 0.004f, FZ(tc));                                                                                 // повязка цвета команды
      PART(0, 0.7f, 0.115f, 0.1f, 0.06f, 0.025f, FZ(RGB(120, 75, 35))); PART(0, 0.755f, 0.125f, 0.07f, 0.015f, 0.006f, FZ(RGB(120, 75, 35))); } break;   // борода и усы
    case M_EBARB: { s = 1.3f * US; gBrow = RGB(40, 30, 30); gPads = FZ(RGB(90, 90, 100));               // элитный варвар: рогатый шлем, наплечники, два меча
      humanoid(x, y, z, yaw, s, FZ(skin), FZ(RGB(220, 175, 135)), FZ(RGB(60, 55, 70)), FZ(tc), walk, atk, WP_SWORD, HT_NONE, 0, false); gBrow = 0; gPads = 0;
      PART(0, 0.5f, 0.0f, 0.176f, 0.17f, 0.111f, FZ(RGB(70, 70, 82))); PART(0, 0.5f, 0.113f, 0.1f, 0.12f, 0.004f, FZ(tc));                       // кожаный нагрудник цвета команды
      PART(0, 0.9f, -0.005f, 0.13f, 0.06f, 0.13f, FZ(RGB(110, 110, 120))); PART(0, 0.96f, 0, 0.05f, 0.02f, 0.05f, FZ(RGB(220, 190, 80)));       // шлем
      PART(-0.17f, 0.97f, 0, 0.05f, 0.025f, 0.025f, FZ(RGB(240, 230, 210))); PART(0.17f, 0.97f, 0, 0.05f, 0.025f, 0.025f, FZ(RGB(240, 230, 210)));
      PART(-0.21f, 1.04f, 0, 0.022f, 0.06f, 0.022f, FZ(RGB(240, 230, 210))); PART(0.21f, 1.04f, 0, 0.022f, 0.06f, 0.022f, FZ(RGB(240, 230, 210)));   // рога
      PART(0, 0.7f, 0.115f, 0.09f, 0.06f, 0.025f, FZ(RGB(60, 40, 35)));                                                                          // чёрная борода
      PART(-0.23f, 0.58f, 0.25f, 0.018f, 0.2f, 0.035f, RGB(215, 220, 230)); PART(-0.23f, 0.37f, 0.06f, 0.055f, 0.015f, 0.03f, RGB(140, 110, 50));   // второй меч в левой руке
      PART(-0.23f, 0.45f, 0.04f, 0.056f, 0.02f, 0.056f, FZ(RGB(220, 190, 80))); } break;                                                         // золотой браслет
    case M_HOG: { s = 1.15f * US; float sw = sinf(walk * 1.6f) * 0.08f, bob = fabsf(sinf(walk * 1.6f)) * 0.04f;     // всадник на кабане: кабан с клыками и наездник с булавой
      u32 hide = FZ(RGB(140, 95, 75)), dark = FZ(RGB(100, 65, 50));
      PART(0, 0.45f + bob, 0, 0.24f, 0.2f, 0.4f, hide); PART(0, 0.62f + bob, -0.05f, 0.18f, 0.06f, 0.3f, dark);                                  // туловище и щетина на спине
      PART(0, 0.48f + bob, 0.46f, 0.17f, 0.15f, 0.14f, hide); PART(0, 0.42f + bob, 0.63f, 0.09f, 0.07f, 0.05f, FZ(RGB(220, 160, 150)));         // голова и пятачок
      PART(-0.04f, 0.42f + bob, 0.685f, 0.015f, 0.02f, 0.004f, RGB(60, 30, 30)); PART(0.04f, 0.42f + bob, 0.685f, 0.015f, 0.02f, 0.004f, RGB(60, 30, 30));
      PART(-0.13f, 0.38f + bob, 0.6f, 0.025f, 0.07f, 0.025f, FZ(RGB(245, 240, 225))); PART(0.13f, 0.38f + bob, 0.6f, 0.025f, 0.07f, 0.025f, FZ(RGB(245, 240, 225)));   // клыки
      PART(-0.09f, 0.56f + bob, 0.6f, 0.025f, 0.025f, 0.004f, RGB(250, 220, 60)); PART(0.09f, 0.56f + bob, 0.6f, 0.025f, 0.025f, 0.004f, RGB(250, 220, 60));          // глаза
      PART(-0.12f, 0.66f + bob, 0.47f, 0.04f, 0.05f, 0.03f, dark); PART(0.12f, 0.66f + bob, 0.47f, 0.04f, 0.05f, 0.03f, dark);                   // уши
      for (int k = 0; k < 4; k++) { float lx = (k & 1 ? 0.15f : -0.15f), lz = (k & 2 ? 0.27f : -0.27f), o = (k == 0 || k == 3) ? sw : -sw; PART(lx, 0.14f, lz + o, 0.06f, 0.14f, 0.06f, dark); }
      PART(0, 0.66f + bob, 0.05f, 0.25f, 0.03f, 0.2f, FZ(tc));                                                                                    // попона цвета команды
      { float rs = 0.62f; float y0 = y + (0.48f + bob) * s; float s0 = s; s = s * rs; float yb = y; (void)yb;                                     // наездник сидит сверху
        float rx = x, rz = z; float ry = y0; float _sv = s;
        { float y = ry; PART(-0.17f, 0.2f, 0.05f, 0.055f, 0.06f, 0.15f, FZ(RGB(90, 70, 50))); PART(0.17f, 0.2f, 0.05f, 0.055f, 0.06f, 0.15f, FZ(RGB(90, 70, 50)));   // ноги по бокам
          PART(0, 0.48f, 0, 0.17f, 0.17f, 0.11f, FZ(RGB(220, 180, 140))); PART(0, 0.48f, 0.112f, 0.1f, 0.12f, 0.005f, FZ(tc)); PART(0, 0.34f, 0, 0.175f, 0.025f, 0.115f, FZ(RGB(70, 50, 35)));
          PART(0, 0.78f, 0, 0.12f, 0.12f, 0.12f, FZ(skin)); PART(-0.05f, 0.8f, 0.121f, 0.022f, 0.022f, 0.003f, RGB(250, 250, 250)); PART(0.05f, 0.8f, 0.121f, 0.022f, 0.022f, 0.003f, RGB(250, 250, 250));
          PART(-0.045f, 0.795f, 0.123f, 0.012f, 0.014f, 0.003f, RGB(25, 25, 35)); PART(0.045f, 0.795f, 0.123f, 0.012f, 0.014f, 0.003f, RGB(25, 25, 35));
          PART(0, 0.72f, 0.122f, 0.06f, 0.02f, 0.005f, FZ(RGB(90, 50, 30))); PART(0, 0.9f, 0, 0.13f, 0.05f, 0.13f, FZ(RGB(150, 150, 160))); PART(0, 0.97f, -0.01f, 0.03f, 0.04f, 0.1f, FZ(RGB(150, 150, 160)));   // усы и шлем с гребнем
          float a = atk; PART(0.23f, 0.5f + a * 0.15f, 0.05f + a * 0.15f, 0.05f, 0.14f, 0.05f, FZ(RGB(220, 180, 140))); PART(-0.23f, 0.5f, 0.06f, 0.05f, 0.14f, 0.05f, FZ(RGB(220, 180, 140)));
          PART(0.23f, 0.62f + a * 0.3f, 0.12f + a * 0.3f, 0.025f, 0.2f, 0.025f, RGB(110, 80, 50)); PART(0.23f, 0.86f + a * 0.3f, 0.14f + a * 0.35f, 0.09f, 0.07f, 0.09f, FZ(RGB(120, 120, 130))); }   // булава
        (void)rx; (void)rz; (void)_sv; s = s0; } } break;
    case M_MEGA: { s = 1.75f * US; gEyes = RGB(255, 170, 60); gPads = FZ(RGB(70, 80, 100));             // мегарыцарь: тяжёлые латы, рогатый шлем с горящими глазами, огромные кулаки
      u32 pl = FZ(RGB(90, 100, 120)), pd = FZ(RGB(55, 62, 78));
      humanoid(x, y, z, yaw, s, FZ(RGB(60, 66, 80)), pl, pd, FZ(tc), walk, atk, WP_NONE, HT_NONE, 0, false); gEyes = 0; gPads = 0;
      PART(0, 0.5f, 0.0f, 0.2f, 0.19f, 0.13f, pl); PART(0, 0.55f, 0.132f, 0.11f, 0.1f, 0.004f, FZ(tc)); PART(0, 0.36f, 0.0f, 0.205f, 0.03f, 0.135f, pd);   // кираса шире тела
      PART(0, 0.84f, 0, 0.135f, 0.13f, 0.135f, pd); PART(0, 0.82f, 0.136f, 0.1f, 0.02f, 0.004f, RGB(255, 150, 50));                                 // глухой шлем со светящейся щелью
      for (int k = -1; k <= 1; k += 2) { PART(k * 0.15f, 1.0f, 0, 0.03f, 0.08f, 0.03f, FZ(RGB(200, 200, 210))); PART(k * 0.18f, 1.1f, -0.02f, 0.02f, 0.05f, 0.02f, FZ(RGB(200, 200, 210))); }   // рога-шипы
      PART(0, 1.0f, 0, 0.02f, 0.06f, 0.06f, FZ(RGB(200, 200, 210)));
      float a = atk; PART(0.27f, 0.32f + a * 0.2f, 0.06f + a * 0.2f, 0.09f, 0.08f, 0.09f, pd); PART(-0.27f, 0.32f + a * 0.2f, 0.06f + a * 0.2f, 0.09f, 0.08f, 0.09f, pd);   // огромные кулаки
      PART(0.27f, 0.32f + a * 0.2f, 0.155f + a * 0.2f, 0.07f, 0.02f, 0.004f, FZ(RGB(200, 200, 210))); PART(-0.27f, 0.32f + a * 0.2f, 0.155f + a * 0.2f, 0.07f, 0.02f, 0.004f, FZ(RGB(200, 200, 210)));
      PART(0, 0.45f, -0.14f, 0.2f, 0.3f, 0.015f, FZ(td)); PART(0, 0.04f, 0.06f, 0.08f, 0.04f, 0.1f, pd); } break;                                    // плащ
    case M_STRONG: { s = 1.6f * US; gBrow = RGB(60, 40, 30); u32 sk = FZ(RGB(225, 170, 125));            // силач: огромные руки, майка цвета команды, ремень чемпиона
      float a = atk, slam = a > 0.5f ? (1 - a) * 2 : a * 2;                                                    // руки вверх, потом удар о землю
      humanoid(x, y, z, yaw, s, sk, FZ(tc), FZ(RGB(60, 60, 75)), FZ(tc), walk, 0, WP_NONE, HT_NONE, 0, false); gBrow = 0;
      PART(0, 0.5f, 0, 0.2f, 0.18f, 0.13f, FZ(tc)); PART(0, 0.62f, 0.12f, 0.12f, 0.05f, 0.012f, sk);                                         // майка, открытая грудь
      PART(0, 0.36f, 0, 0.21f, 0.045f, 0.14f, FZ(RGB(140, 90, 40))); PART(0, 0.36f, 0.142f, 0.07f, 0.05f, 0.006f, FZ(RGB(240, 200, 70)));      // пояс чемпиона
      PART(0, 0.73f, 0.118f, 0.07f, 0.02f, 0.008f, FZ(RGB(60, 40, 30)));                                                                      // усы
      PART(0, 0.9f, -0.02f, 0.124f, 0.03f, 0.124f, FZ(RGB(60, 40, 30)));                                                                      // короткая стрижка
      for (int sd = -1; sd <= 1; sd += 2) { float ay = 0.52f + slam * 0.45f, az = 0.08f + slam * 0.1f - (a > 0.5f ? 0 : 0) ;                 // огромные руки
        PART(sd * 0.29f, ay, az, 0.1f, 0.17f, 0.1f, sk); PART(sd * 0.29f, ay - 0.2f, az + 0.03f, 0.1f, 0.07f, 0.1f, sk);
        PART(sd * 0.29f, ay - 0.13f, az, 0.105f, 0.025f, 0.105f, FZ(RGB(200, 50, 40))); }                                                     // напульсники
      } break;
    case M_CGOLEM: { s = 1.45f * US; float sw = sinf(walk) * 0.08f; u32 cu = FZ(RGB(196, 110, 60)), cd = FZ(RGB(150, 80, 45)), pat = FZ(RGB(80, 170, 140));   // медный голем: клёпаный, с патиной, цепь с крюком
      PART(-0.17f, 0.2f, sw, 0.12f, 0.2f, 0.13f, cd); PART(0.17f, 0.2f, -sw, 0.12f, 0.2f, 0.13f, cd);
      PART(0, 0.68f, 0, 0.32f, 0.3f, 0.24f, cu); PART(0, 0.68f, 0.245f, 0.12f, 0.12f, 0.005f, FZ(tc));
      for (int k = 0; k < 4; k++) PART(-0.24f + k * 0.16f, 0.93f, 0.245f, 0.025f, 0.025f, 0.006f, FZ(RGB(230, 170, 110)));      // заклёпки
      PART(0.2f, 0.5f, 0.245f, 0.07f, 0.05f, 0.006f, pat); PART(-0.22f, 0.8f, -0.245f, 0.08f, 0.07f, 0.006f, pat);                // зелёная патина
      PART(0, 1.06f, 0.04f, 0.16f, 0.12f, 0.15f, cd); PART(0, 1.06f, 0.195f, 0.12f, 0.02f, 0.004f, RGB(255, 200, 80));          // голова с прорезью-глазами
      PART(0, 1.2f, 0.04f, 0.04f, 0.04f, 0.04f, FZ(RGB(230, 170, 110)));                                                          // гайка на макушке
      PART(-0.42f, 0.62f + atk * 0.1f, atk * 0.2f, 0.11f, 0.26f, 0.12f, cu); PART(-0.42f, 0.32f, atk * 0.25f, 0.13f, 0.08f, 0.13f, cd);   // левая рука-кувалда
      PART(0.42f, 0.66f, 0.05f, 0.1f, 0.22f, 0.1f, cu);                                                                           // правая рука держит цепь
      for (int k = 0; k < 5; k++) PART(0.42f, 0.4f - k * 0.08f, 0.1f + k * 0.03f, 0.03f, 0.035f, 0.02f, FZ(RGB(120, 120, 130)));  // цепь
      PART(0.42f, 0.0f, 0.26f, 0.07f, 0.03f, 0.02f, FZ(RGB(170, 170, 180))); PART(0.47f, 0.05f, 0.3f, 0.02f, 0.06f, 0.02f, FZ(RGB(170, 170, 180))); } break;   // крюк
    case M_BOMBER: s = 0.75f * US; humanoid(x, y, z, yaw, s, FZ(skin), FZ(RGB(90, 100, 160)), FZ(RGB(70, 60, 50)), FZ(tc), walk, atk, WP_BOMB, HT_BEARD, 0, false);
      PART(-0.05f, 0.93f, 0.12f, 0.035f, 0.03f, 0.012f, FZ(RGB(70, 60, 50))); PART(0.05f, 0.93f, 0.12f, 0.035f, 0.03f, 0.012f, FZ(RGB(70, 60, 50)));   // очки
      PART(-0.05f, 0.93f, 0.133f, 0.022f, 0.018f, 0.002f, RGB(150, 220, 255)); PART(0.05f, 0.93f, 0.133f, 0.022f, 0.018f, 0.002f, RGB(150, 220, 255));
      PART(0.14f, 0.36f, 0.08f, 0.04f, 0.04f, 0.04f, FZ(RGB(30, 30, 34))); break;   // запасная бомба на поясе
    case M_BAT: { s = 0.7f * US; float fl = sinf(walk * 3.0f) * 0.25f;
      PART(0, 0, 0, 0.12f, 0.1f, 0.14f, FZ(RGB(60, 45, 70))); PART(0, 0.08f, 0.14f, 0.08f, 0.07f, 0.06f, FZ(RGB(70, 55, 80)));
      PART(-0.06f, 0.17f, 0.15f, 0.025f, 0.05f, 0.015f, FZ(RGB(70, 55, 80))); PART(0.06f, 0.17f, 0.15f, 0.025f, 0.05f, 0.015f, FZ(RGB(70, 55, 80)));
      PART(-0.04f, 0.1f, 0.205f, 0.015f, 0.015f, 0.003f, RGB(255, 60, 60)); PART(0.04f, 0.1f, 0.205f, 0.015f, 0.015f, 0.003f, RGB(255, 60, 60));
      PART(-0.32f, fl, 0, 0.22f, 0.012f, 0.12f, FZ(tc)); PART(0.32f, fl, 0, 0.22f, 0.012f, 0.12f, FZ(tc));
      PART(-0.32f, fl - 0.005f, -0.06f, 0.2f, 0.01f, 0.05f, FZ(td)); PART(0.32f, fl - 0.005f, -0.06f, 0.2f, 0.01f, 0.05f, FZ(td));   // перепонки
      PART(-0.025f, 0.03f, 0.2f, 0.008f, 0.02f, 0.004f, RGB(250, 250, 250)); PART(0.025f, 0.03f, 0.2f, 0.008f, 0.02f, 0.004f, RGB(250, 250, 250)); } break;   // клыки
    case M_DRAGON: { s = 1.3f * US; float fl = sinf(walk * 1.5f) * 0.3f;
      PART(0, 0, 0, 0.22f, 0.2f, 0.32f, FZ(RGB(90, 170, 90))); PART(0, -0.08f, 0.02f, 0.18f, 0.12f, 0.26f, FZ(RGB(220, 200, 120)));
      PART(0, 0.18f, 0.36f, 0.1f, 0.1f, 0.12f, FZ(RGB(90, 170, 90))); PART(0, 0.26f, 0.54f, 0.13f, 0.11f, 0.14f, FZ(RGB(100, 180, 95)));
      PART(0, 0.2f, 0.69f, 0.09f, 0.05f, 0.05f, FZ(RGB(220, 200, 120)));
      PART(-0.07f, 0.31f, 0.67f, 0.025f, 0.025f, 0.005f, RGB(255, 220, 40)); PART(0.07f, 0.31f, 0.67f, 0.025f, 0.025f, 0.005f, RGB(255, 220, 40));
      PART(-0.08f, 0.4f, 0.48f, 0.025f, 0.08f, 0.025f, FZ(RGB(240, 230, 200))); PART(0.08f, 0.4f, 0.48f, 0.025f, 0.08f, 0.025f, FZ(RGB(240, 230, 200)));
      PART(0, 0.05f, -0.42f, 0.07f, 0.07f, 0.16f, FZ(RGB(80, 160, 80))); PART(0, 0.1f, -0.62f, 0.04f, 0.04f, 0.1f, FZ(RGB(80, 160, 80)));
      PART(-0.48f, 0.15f + fl, -0.02f, 0.3f, 0.02f, 0.22f, FZ(tc)); PART(0.48f, 0.15f + fl, -0.02f, 0.3f, 0.02f, 0.22f, FZ(tc));
      PART(-0.72f, 0.15f + fl * 1.6f, -0.08f, 0.1f, 0.018f, 0.14f, FZ(td)); PART(0.72f, 0.15f + fl * 1.6f, -0.08f, 0.1f, 0.018f, 0.14f, FZ(td));
      for (int k = 0; k < 4; k++) PART(0, 0.22f, 0.22f - k * 0.17f, 0.02f, 0.05f, 0.04f, FZ(RGB(230, 120, 50)));                      // шипы на спине
      for (int k = 0; k < 3; k++) PART(0, -0.2f, 0.16f - k * 0.14f, 0.15f, 0.005f, 0.04f, FZ(RGB(190, 165, 95)));                    // полосы на брюшке
      PART(-0.03f, 0.23f, 0.74f, 0.012f, 0.012f, 0.004f, RGB(40, 30, 20)); PART(0.03f, 0.23f, 0.74f, 0.012f, 0.012f, 0.004f, RGB(40, 30, 20));   // ноздри
      PART(-0.065f, 0.305f, 0.676f, 0.012f, 0.016f, 0.003f, RGB(20, 20, 20)); PART(0.065f, 0.305f, 0.676f, 0.012f, 0.016f, 0.003f, RGB(20, 20, 20));   // зрачки
      for (int k = 0; k < 2; k++) { float lx = k ? 0.12f : -0.12f; PART(lx, -0.24f, 0.12f, 0.04f, 0.06f, 0.04f, FZ(RGB(80, 155, 80))); PART(lx, -0.3f, 0.16f, 0.035f, 0.012f, 0.03f, RGB(240, 235, 220)); } } break;   // лапки с когтями
    case M_BULL: { s = 1.2f * US; float sw = sinf(walk * 1.3f) * 0.08f;
      PART(0, 0.55f, 0, 0.28f, 0.22f, 0.45f, FZ(RGB(110, 70, 45))); PART(0, 0.6f, 0.52f, 0.17f, 0.17f, 0.17f, FZ(RGB(100, 62, 40)));
      PART(0, 0.53f, 0.68f, 0.13f, 0.09f, 0.04f, FZ(RGB(180, 140, 120)));
      PART(-0.22f, 0.78f, 0.55f, 0.12f, 0.03f, 0.03f, FZ(RGB(240, 235, 220))); PART(0.22f, 0.78f, 0.55f, 0.12f, 0.03f, 0.03f, FZ(RGB(240, 235, 220)));
      PART(-0.32f, 0.88f, 0.55f, 0.025f, 0.08f, 0.025f, FZ(RGB(240, 235, 220))); PART(0.32f, 0.88f, 0.55f, 0.025f, 0.08f, 0.025f, FZ(RGB(240, 235, 220)));
      PART(0, 0.79f, 0, 0.29f, 0.04f, 0.3f, FZ(tc)); PART(0, 0.6f, 0.0f, 0.3f, 0.12f, 0.12f, FZ(td));    // попона
      for (int k = 0; k < 4; k++) { float lx = (k & 1 ? 0.18f : -0.18f), lz = (k & 2 ? 0.3f : -0.3f), o = (k == 0 || k == 3) ? sw : -sw; PART(lx, 0.18f, lz + o, 0.07f, 0.18f, 0.07f, FZ(RGB(90, 58, 38))); }
      PART(0, 0.6f, -0.48f, 0.02f, 0.1f, 0.02f, FZ(RGB(90, 58, 38))); PART(0, 0.48f, -0.49f, 0.04f, 0.04f, 0.03f, FZ(RGB(60, 40, 28)));   // хвост с кисточкой
      PART(-0.1f, 0.68f, 0.69f, 0.03f, 0.03f, 0.004f, RGB(250, 250, 250)); PART(0.1f, 0.68f, 0.69f, 0.03f, 0.03f, 0.004f, RGB(250, 250, 250));
      PART(-0.1f, 0.675f, 0.693f, 0.016f, 0.02f, 0.003f, RGB(30, 20, 20)); PART(0.1f, 0.675f, 0.693f, 0.016f, 0.02f, 0.003f, RGB(30, 20, 20));   // глаза
      PART(-0.1f, 0.73f, 0.69f, 0.035f, 0.01f, 0.004f, RGB(40, 25, 20)); PART(0.1f, 0.73f, 0.69f, 0.035f, 0.01f, 0.004f, RGB(40, 25, 20));       // злые брови
      PART(0, 0.47f, 0.72f, 0.04f, 0.035f, 0.008f, FZ(RGB(230, 200, 80))); } break;   // кольцо в носу
    case M_GOLEM: { s = 1.5f * US; float sw = sinf(walk) * 0.08f; u32 st = FZ(RGB(130, 125, 120)), sd = FZ(RGB(105, 100, 98));
      PART(-0.17f, 0.2f, sw, 0.12f, 0.2f, 0.13f, sd); PART(0.17f, 0.2f, -sw, 0.12f, 0.2f, 0.13f, sd);
      PART(0, 0.68f, 0, 0.32f, 0.3f, 0.24f, st); PART(0, 0.68f, 0.245f, 0.12f, 0.12f, 0.005f, FZ(tc));
      PART(0, 1.06f, 0.04f, 0.16f, 0.12f, 0.15f, sd); PART(-0.06f, 1.08f, 0.195f, 0.03f, 0.02f, 0.004f, RGB(255, 180, 60)); PART(0.06f, 1.08f, 0.195f, 0.03f, 0.02f, 0.004f, RGB(255, 180, 60));
      PART(-0.42f, 0.62f + atk * 0.1f, atk * 0.2f, 0.11f, 0.26f, 0.12f, st); PART(0.42f, 0.62f + atk * 0.1f, atk * 0.2f, 0.11f, 0.26f, 0.12f, st);
      PART(-0.42f, 0.32f, atk * 0.25f, 0.13f, 0.08f, 0.13f, sd); PART(0.42f, 0.32f, atk * 0.25f, 0.13f, 0.08f, 0.13f, sd);
      PART(0.15f, 0.9f, -0.2f, 0.06f, 0.04f, 0.06f, FZ(RGB(90, 150, 70)));   // мох
      PART(-0.4f, 0.92f, 0, 0.13f, 0.06f, 0.13f, sd); PART(0.4f, 0.92f, 0, 0.13f, 0.06f, 0.13f, sd); PART(-0.4f, 0.99f, 0.02f, 0.08f, 0.03f, 0.08f, FZ(RGB(90, 150, 70)));   // каменные плечи
      PART(-0.15f, 0.55f, 0.245f, 0.08f, 0.008f, 0.004f, FZ(RGB(70, 66, 64))); PART(0.18f, 0.8f, 0.245f, 0.006f, 0.08f, 0.004f, FZ(RGB(70, 66, 64)));            // трещины
      PART(0, 1.0f, 0.195f, 0.1f, 0.012f, 0.004f, FZ(RGB(70, 66, 64))); PART(0, 0.97f, 0.195f, 0.04f, 0.008f, 0.003f, RGB(255, 140, 40)); } break;   // брови-скала и светящийся рот
    case M_CANNON: { s = 1.0f; u32 wd = FZ(RGB(130, 90, 55));
      PART(0, 0.18f, 0, 0.42f, 0.18f, 0.42f, wd); PART(0, 0.37f, 0, 0.44f, 0.02f, 0.44f, FZ(tc));
      for (int k = -1; k <= 1; k += 2) PART(k * 0.28f, 0.5f, 0, 0.06f, 0.13f, 0.18f, FZ(RGB(90, 60, 40)));
      PART(0, 0.6f, 0.05f + atk * -0.08f, 0.13f, 0.13f, 0.38f, FZ(RGB(50, 50, 56))); PART(0, 0.6f, 0.44f + atk * -0.08f, 0.15f, 0.15f, 0.04f, FZ(RGB(40, 40, 45)));
      PART(0, 0.6f, 0.485f + atk * -0.08f, 0.08f, 0.08f, 0.003f, RGB(10, 10, 10)); } break;
    case M_ATOWER: { s = 1.0f; u32 wd = FZ(RGB(140, 100, 60));
      for (int k = 0; k < 4; k++) { float lx = (k & 1 ? 0.45f : -0.45f), lz = (k & 2 ? 0.45f : -0.45f); BX(x + lx, y + 1.0f, z + lz, 0.07f, 1.0f, 0.07f, 0, wd); }
      BX(x, y + 1.0f, z, 0.5f, 0.03f, 0.03f, 0, wd); BX(x, y + 2.05f, z, 0.6f, 0.06f, 0.6f, 0, FZ(RGB(120, 85, 50)));
      for (int k = 0; k < 4; k++) { float a = k * 1.5708f; float rx = x + sinf(a) * 0.58f, rz = z + cosf(a) * 0.58f; BX(rx, y + 2.3f, rz, k & 1 ? 0.03f : 0.6f, 0.2f, k & 1 ? 0.6f : 0.03f, 0, FZ(tc)); }
      humanoid(x, y + 2.11f, z, yaw, 0.85f, FZ(skin), FZ(RGB(80, 130, 70)), FZ(RGB(90, 70, 50)), FZ(tc), 0, atk, WP_BOW, HT_HOOD, 0, false);
      BX(x, y + 3.25f, z, 0.62f, 0.05f, 0.62f, 0, FZ(td)); BX(x, y + 3.38f, z, 0.4f, 0.08f, 0.4f, 0, FZ(td)); BX(x, y + 3.5f, z, 0.18f, 0.06f, 0.18f, 0, FZ(td));
      for (int k = 0; k < 4; k++) { float lx = (k & 1 ? 0.55f : -0.55f), lz = (k & 2 ? 0.55f : -0.55f); BX(x + lx, y + 2.75f, z + lz, 0.035f, 0.45f, 0.035f, 0, wd); } } break;
    case M_FURNACE: { s = 1.0f; u32 br = FZ(RGB(120, 62, 48)), brd = FZ(RGB(86, 44, 36)), st = FZ(RGB(78, 74, 80));   // печь: кирпичный корпус, огненное жерло, труба
      BX(x, y + 0.14f, z, 0.62f, 0.14f, 0.62f, 0, st); BX(x, y + 0.3f, z, 0.64f, 0.03f, 0.64f, 0, FZ(tc));
      BX(x, y + 0.75f, z, 0.5f, 0.45f, 0.5f, 0, br); for (int k = 0; k < 3; k++) BX(x, y + 0.45f + k * 0.28f, z, 0.52f, 0.018f, 0.52f, 0, brd);
      BX(x, y + 1.3f, z, 0.46f, 0.1f, 0.46f, 0, st); BX(x + 0.22f, y + 1.7f, z - 0.2f, 0.12f, 0.35f, 0.12f, 0, st); BX(x + 0.22f, y + 2.08f, z - 0.2f, 0.16f, 0.04f, 0.16f, 0, brd);
      float fl = 0.04f * sinf(gTime * 12 + x * 2 + z), ox = sinf(yaw) * 0.52f, oz = cosf(yaw) * 0.52f;   // жерло смотрит в сторону врага
      BX(x + ox, y + 0.62f, z + oz, 0.2f, 0.2f, 0.04f, yaw, FZ(RGB(30, 20, 20))); BX(x + ox * 1.03f, y + 0.6f, z + oz * 1.03f, 0.15f, 0.14f + fl, 0.03f, yaw, FZ(RGB(255, 140, 30)));
      GLOW(x + ox * 1.1f, y + 0.62f, z + oz * 1.1f, 0.4f + atk * 0.2f, RGB(255, 150, 50), 170); GLOW(x + 0.22f, y + 2.2f + fabsf(fl) * 4, z - 0.2f, 0.12f, RGB(255, 190, 80), 140);
      GLOW(x, y + 0.7f, z, 0.8f, RGB(255, 120, 40), 40); } break;
    case M_COLLECTOR: { s = 1.0f; u32 pk = FZ(RGB(215, 70, 225)), pd = FZ(RGB(140, 40, 160));
      BX(x, y + 0.15f, z, 0.6f, 0.15f, 0.6f, 0, FZ(RGB(120, 112, 105))); BX(x, y + 0.33f, z, 0.62f, 0.03f, 0.62f, 0, FZ(tc));
      BX(x, y + 0.9f, z, 0.34f, 0.55f, 0.34f, 0, pd); BX(x, y + 0.9f, z, 0.3f, 0.5f, 0.3f, 0, pk);   // бак с эликсиром
      for (int k = 0; k < 4; k++) { float a = k * 1.5708f + 0.785f; BX(x + sinf(a) * 0.36f, y + 0.9f, z + cosf(a) * 0.36f, 0.04f, 0.58f, 0.04f, a, FZ(RGB(150, 100, 60))); }
      BX(x, y + 1.5f, z, 0.4f, 0.05f, 0.4f, 0, FZ(RGB(150, 100, 60))); BX(x, y + 1.7f, z, 0.07f, 0.16f, 0.07f, 0, FZ(RGB(90, 80, 85)));
      BX(x + 0.5f, y + 0.55f, z, 0.1f, 0.05f, 0.05f, 0, FZ(RGB(90, 80, 85))); BX(x + 0.6f, y + 0.45f, z, 0.05f, 0.12f, 0.05f, 0, FZ(RGB(90, 80, 85)));
      for (int k = 0; k < 3; k++) { float t2 = fmodf(gTime * 0.7f + k * 0.33f, 1.0f); GLOW(x + sinf(k * 2.4f + gTime) * 0.12f, y + 0.5f + t2 * 0.75f, z + cosf(k * 2.4f) * 0.12f, 0.07f, RGB(255, 170, 255), 200); }
      GLOW(x, y + 0.9f, z, 0.6f + 0.08f * sinf(gTime * 4 + x), RGB(230, 90, 240), 45); } break;
    case M_INFERNO: { s = 1.0f;
      BX(x, y + 0.2f, z, 0.62f, 0.2f, 0.62f, 0, FZ(RGB(118, 110, 104))); BX(x, y + 0.45f, z, 0.64f, 0.05f, 0.64f, 0, FZ(tc));
      BX(x, y + 1.0f, z, 0.4f, 0.55f, 0.4f, 0, FZ(RGB(66, 58, 62)));
      for (int k = 0; k < 4; k++) { float a = k * 1.5708f + 0.785f; BX(x + sinf(a) * 0.42f, y + 1.0f, z + cosf(a) * 0.42f, 0.05f, 0.55f, 0.05f, a, FZ(RGB(150, 90, 55))); }
      BX(x, y + 0.85f, z, 0.46f, 0.04f, 0.46f, 0, FZ(RGB(170, 100, 55))); BX(x, y + 1.3f, z, 0.46f, 0.04f, 0.46f, 0, FZ(RGB(170, 100, 55)));
      BX(x, y + 1.75f, z, 0.3f, 0.2f, 0.3f, 0, FZ(RGB(50, 44, 48))); BX(x, y + 1.97f, z, 0.38f, 0.04f, 0.38f, 0, FZ(td));
      { float pl = 0.75f + 0.25f * sinf(gTime * 6 + x) + atk * 0.4f; GLOW(x, y + 1.75f, z, 0.3f * pl + 0.12f, RGB(255, 110, 30), 190); GLOW(x, y + 1.75f, z, 0.7f, RGB(255, 170, 70), 40); }
      PART(0, 1.75f, 0.28f, 0.08f, 0.08f, 0.22f, FZ(RGB(30, 28, 32))); PART(0, 1.75f, 0.52f, 0.1f, 0.1f, 0.03f, FZ(RGB(25, 22, 24)));
      BX(x, y + 2.25f, z, 0.07f, 0.28f, 0.07f, 0, FZ(RGB(40, 36, 40))); GLOW(x, y + 2.55f, z, 0.1f, RGB(255, 80, 30), 200); } break;
    case M_HUT: { s = 1.0f;
      BX(x, y + 0.45f, z, 0.62f, 0.45f, 0.55f, 0, FZ(RGB(150, 110, 70)));
      for (int k = 0; k < 4; k++) BX(x, y + 0.98f + k * 0.14f, z, 0.75f - k * 0.17f, 0.07f, 0.68f - k * 0.15f, 0, FZ(k & 1 ? RGB(200, 170, 80) : RGB(185, 155, 70)));
      float dx, dz; LP(x, z, yaw, 0, 0.56f, dx, dz); BX(dx, y + 0.32f, dz, 0.2f, 0.32f, 0.02f, yaw, RGB(40, 30, 25));
      BX(x + 0.45f, y + 1.55f, z - 0.2f, 0.07f, 0.25f, 0.07f, 0, FZ(RGB(90, 80, 75)));
      LP(x, z, yaw, 0.0f, 0.58f, dx, dz); BX(dx, y + 0.8f, dz, 0.24f, 0.08f, 0.01f, yaw, FZ(tc)); } break;
    case M_PRINCESS: { u32 st = FZ(RGB(175, 170, 160)), sd = FZ(RGB(140, 135, 128));
      BX(x, y + 1.2f, z, 1.2f, 1.2f, 1.2f, 0, st); BX(x, y + 1.2f, z, 1.0f, 1.21f, 1.0f, 0.785f, st);
      BX(x, y + 0.2f, z, 1.3f, 0.2f, 1.3f, 0, sd);
      for (int k = 0; k < 8; k++) { float a = k * 0.785f; BX(x + sinf(a) * 1.15f, y + 2.55f, z + cosf(a) * 1.15f, 0.22f, 0.18f, 0.22f, a, st); }
      BX(x, y + 2.42f, z, 1.25f, 0.05f, 1.25f, 0, sd);
      // флаги и полосы цвета команды
      BX(x, y + 1.7f, z + 1.21f * (viewTeam == 0 ? 1 : 1), 0.4f, 0.35f, 0.02f, 0, FZ(tc));
      BX(x + 0.9f, y + 3.4f, z + 0.9f, 0.025f, 0.7f, 0.025f, 0, FZ(RGB(100, 80, 60))); BX(x + 0.9f + 0.18f, y + 3.85f, z + 0.9f, 0.18f, 0.12f, 0.01f, 0, FZ(tc));
      gHair = FZ(RGB(240, 200, 90)); humanoid(x, y + 2.47f, z, yaw, 1.0f, FZ(RGB(250, 215, 185)), FZ(RGB(230, 120, 170)), FZ(RGB(180, 90, 140)), FZ(tc), 0, atk, WP_BOW, HT_CROWN, 0, false); gHair = 0;
      if (life01 < 0) { /* развалины */ } } break;
    case M_KING: { u32 st = FZ(RGB(180, 175, 165)), sd = FZ(RGB(145, 140, 132));
      BX(x, y + 1.3f, z, 1.7f, 1.3f, 1.6f, 0, st); BX(x, y + 0.2f, z, 1.85f, 0.2f, 1.75f, 0, sd);
      for (int k = 0; k < 6; k++) { BX(x - 1.45f + k * 0.58f, y + 2.78f, z - 1.48f, 0.18f, 0.2f, 0.12f, 0, st); BX(x - 1.45f + k * 0.58f, y + 2.78f, z + 1.48f, 0.18f, 0.2f, 0.12f, 0, st); }
      for (int k = 0; k < 5; k++) { BX(x - 1.6f, y + 2.78f, z - 1.16f + k * 0.58f, 0.12f, 0.2f, 0.18f, 0, st); BX(x + 1.6f, y + 2.78f, z - 1.16f + k * 0.58f, 0.12f, 0.2f, 0.18f, 0, st); }
      BX(x, y + 2.62f, z, 1.72f, 0.04f, 1.62f, 0, sd);
      BX(x, y + 1.2f, z + 1.61f, 0.9f, 0.6f, 0.02f, 0, FZ(tc)); BX(x, y + 1.2f, z + 1.63f, 0.25f, 0.25f, 0.01f, 0, FZ(RGB(240, 200, 60)));
      BX(x, y + 1.2f, z - 1.61f, 0.9f, 0.6f, 0.02f, 0, FZ(tc));
      // король и пушка
      humanoid(x - 0.6f, y + 2.66f, z, yaw, 1.15f, FZ(skin), FZ(tc), FZ(td), FZ(RGB(240, 200, 60)), 0, atk, WP_NONE, HT_CROWN, 0, false);
      if (B.duo) humanoid(x + 0.05f, y + 2.66f, z - 0.55f, yaw, 1.1f, FZ(skin), FZ(mixc(tc, RGB(160, 60, 200), 0.45f)), FZ(td), FZ(RGB(240, 200, 60)), 0, atk, WP_NONE, HT_CROWN, 0, false);
      { float cx2, cz2; LP(x + 0.5f, z, yaw, 0, 0.3f - atk * 0.1f, cx2, cz2); BX(cx2, y + 2.95f, cz2, 0.18f, 0.18f, 0.5f, yaw, FZ(RGB(50, 50, 56))); BX(x + 0.5f, y + 2.75f, z, 0.32f, 0.12f, 0.32f, 0, FZ(RGB(110, 80, 50))); } } break;
  }
  #undef FZ
  (void)life01;
}
static float modelTop0(int model);
static float modelTop(int model) { return modelTop0(model) * (model < M_CANNON ? 1.35f : 1.0f); }
static float modelTop0(int model) {
  switch (model) { case M_GIANT: return 2.2f; case M_GOLEM: return 1.95f; case M_PRINCESS: return 4.6f; case M_KING: return 4.3f; case M_ATOWER: return 3.7f; case M_INFERNO: return 2.6f; case M_FSPIRIT: return 0.8f; case M_ISPIRIT: return 0.8f; case M_COLLECTOR: return 1.9f; case M_FURNACE: return 2.0f; case M_HUT: return 1.7f; case M_CANNON: return 0.95f;
    case M_BULL: return 1.25f; case M_GOBKING: return 1.75f; case M_DART: return 0.95f; case M_VOLAR: return 0.8f; case M_PUP: return 0.35f; case M_ICEWIZ: return 1.45f; case M_HOG: return 1.55f; case M_MEGA: return 2.0f; case M_STRONG: return 1.9f; case M_CGOLEM: return 2.0f; case M_DRAGON: return 0.7f; case M_BAT: return 0.35f; case M_GOBLIN: return 0.85f; case M_BOMBER: return 0.85f; case M_SKELETON: return 0.85f; default: return 1.35f; }
}

// ---- арена ----
static void drawTree(float x, float z, float s, int kind) {
  if (kind == 2) { BX(x, 0.12f * s, z, 0.25f * s, 0.12f * s, 0.12f * s, x, RGB(225, 220, 205)); BX(x + 0.2f * s, 0.3f * s, z, 0.08f * s, 0.25f * s, 0.08f * s, 0, RGB(220, 215, 200)); return; }   // кости
  if (kind == 3) { BX(x, 0.4f * s, z, 0.35f * s, 0.4f * s, 0.35f * s, x * 3, RGB(200, 225, 245)); BX(x, 0.95f * s, z, 0.2f * s, 0.25f * s, 0.2f * s, x * 3 + 0.5f, RGB(225, 240, 252)); return; }   // лёд
  if (kind == 4) { BX(x, 0.35f * s, z, 0.4f * s, 0.35f * s, 0.35f * s, x, RGB(70, 50, 45)); GLOW(x, 0.75f * s, z, 0.25f * s, RGB(255, 120, 30), 70); return; }       // камни с лавой
  BX(x, 0.35f * s, z, 0.09f * s, 0.35f * s, 0.09f * s, 0, RGB(110, 75, 45));
  u32 lc = ARENAS[B.arena].deco;
  for (int k = 0; k < 3; k++) BX(x, (0.75f + k * 0.38f) * s, z, (0.5f - k * 0.14f) * s, 0.22f * s, (0.5f - k * 0.14f) * s, k * 0.4f, mixc(lc, RGB(0, 0, 0), k * 0.08f));
}

// ---- оформление арен под их названия: каждая арена - свои декорации по краям ----
// inw: +1 если предмет слева от поля (лицом вправо), -1 если справа; h - хэш для вариантов
static void drawProp(int kind, float x, float z, float s, unsigned h, float inw) {
  int v = (int)((h >> 6) % 7); float yaw = ((h >> 12) & 15) * 0.2f;
  if (kind == 0) {   // ТРЕНИРОВОЧНЫЙ ЛАГЕРЬ: чучела, мишени, палатки, сено, стойки с оружием
    u32 wood = RGB(120, 84, 50), straw = RGB(222, 190, 100);
    if (v == 0 || v == 1) {   // чучело для мечей
      BX(x, 0.55f * s, z, 0.05f * s, 0.55f * s, 0.05f * s, 0, wood); BX(x, 0.85f * s, z, 0.04f * s, 0.05f * s, 0.34f * s, 0, wood);
      BX(x, 0.78f * s, z, 0.13f * s, 0.26f * s, 0.14f * s, 0, straw); BX(x, 1.22f * s, z, 0.11f * s, 0.11f * s, 0.11f * s, 0, mixc(straw, RGB(255, 255, 255), 0.2f));
      BX(x + inw * 0.13f * s, 0.9f * s, z, 0.02f * s, 0.05f * s, 0.15f * s, 0, RGB(200, 50, 45)); }
    else if (v == 2) {   // мишень для лучников
      BX(x, 0.35f * s, z - 0.3f * s, 0.04f * s, 0.35f * s, 0.04f * s, 0, wood); BX(x, 0.35f * s, z + 0.3f * s, 0.04f * s, 0.35f * s, 0.04f * s, 0, wood);
      BX(x, 0.85f * s, z, 0.03f * s, 0.38f * s, 0.38f * s, 0, RGB(240, 235, 220)); BX(x, 0.85f * s, z, 0.035f * s, 0.27f * s, 0.27f * s, 0, RGB(210, 50, 45));
      BX(x, 0.85f * s, z, 0.04f * s, 0.15f * s, 0.15f * s, 0, RGB(240, 235, 220)); BX(x, 0.85f * s, z, 0.045f * s, 0.07f * s, 0.07f * s, 0, RGB(240, 200, 50)); }
    else if (v == 3) {   // палатка
      u32 cv = (h & 1) ? RGB(235, 225, 200) : RGB(190, 70, 60);
      BX(x, 0.3f * s, z, 0.65f * s, 0.3f * s, 0.55f * s, 0, cv); BX(x, 0.75f * s, z, 0.48f * s, 0.18f * s, 0.4f * s, 0, mixc(cv, RGB(0, 0, 0), 0.12f)); BX(x, 1.05f * s, z, 0.25f * s, 0.14f * s, 0.2f * s, 0, mixc(cv, RGB(0, 0, 0), 0.22f));
      BX(x, 1.5f * s, z, 0.025f * s, 0.3f * s, 0.025f * s, 0, wood); BX(x + 0.12f * s, 1.65f * s, z, 0.12f * s, 0.07f * s, 0.015f * s, 0, COL_ME);
      BX(x + inw * 0.66f * s, 0.2f * s, z, 0.02f * s, 0.2f * s, 0.2f * s, 0, RGB(40, 30, 25)); }
    else if (v == 4) {   // стог сена
      BX(x, 0.25f * s, z, 0.42f * s, 0.25f * s, 0.32f * s, yaw * 0.3f, straw); BX(x, 0.25f * s, z, 0.43f * s, 0.04f * s, 0.33f * s, yaw * 0.3f, RGB(150, 110, 60)); BX(x + 0.1f * s, 0.62f * s, z, 0.3f * s, 0.12f * s, 0.24f * s, yaw * 0.3f, mixc(straw, RGB(255, 255, 255), 0.12f)); }
    else if (v == 5) {   // стойка с оружием
      BX(x, 0.3f * s, z, 0.06f * s, 0.3f * s, 0.45f * s, 0, wood); BX(x, 0.62f * s, z, 0.07f * s, 0.03f * s, 0.5f * s, 0, wood);
      for (int k = 0; k < 4; k++) BX(x + inw * 0.04f * s, 0.8f * s, z - 0.33f * s + k * 0.22f * s, 0.02f * s, 0.32f * s, 0.025f * s, 0, k & 1 ? RGB(200, 205, 215) : RGB(150, 110, 70)); }
    else { drawTree(x, z, s, 0); }
    return; }
  if (kind == 1) {   // ГОБЛИНСКИЙ ЛЕС: тёмные ёлки, хижины гоблинов, грибы, пни, тотемы
    if (v <= 2) { drawTree(x, z, s * 1.15f, 1); return; }
    if (v == 3) {   // хижина гоблина
      BX(x, 0.35f * s, z, 0.5f * s, 0.35f * s, 0.5f * s, 0, RGB(104, 78, 48)); BX(x, 0.85f * s, z, 0.6f * s, 0.12f * s, 0.6f * s, 0, RGB(66, 96, 44)); BX(x, 1.1f * s, z, 0.42f * s, 0.14f * s, 0.42f * s, 0, RGB(58, 86, 40)); BX(x, 1.32f * s, z, 0.22f * s, 0.12f * s, 0.22f * s, 0, RGB(50, 76, 36));
      BX(x + inw * 0.51f * s, 0.25f * s, z, 0.02f * s, 0.25f * s, 0.16f * s, 0, RGB(30, 22, 16)); BX(x + inw * 0.51f * s, 0.5f * s, z + 0.3f * s, 0.02f * s, 0.1f * s, 0.1f * s, 0, RGB(255, 210, 90));
      GLOW(x + inw * 0.55f * s, 0.5f * s, z + 0.3f * s, 0.3f * s, RGB(255, 200, 80), 70);
      BX(x, 1.58f * s, z, 0.09f * s, 0.09f * s, 0.08f * s, 0, RGB(230, 225, 205)); return; }
    if (v == 4) {   // гриб
      BX(x, 0.25f * s, z, 0.1f * s, 0.25f * s, 0.1f * s, 0, RGB(235, 225, 200)); BX(x, 0.58f * s, z, 0.38f * s, 0.1f * s, 0.38f * s, 0, RGB(200, 60, 60)); BX(x, 0.7f * s, z, 0.24f * s, 0.06f * s, 0.24f * s, 0, RGB(215, 75, 70));
      BX(x + 0.15f * s, 0.77f * s, z + 0.1f * s, 0.05f * s, 0.02f * s, 0.05f * s, 0, RGB(255, 255, 240)); BX(x - 0.12f * s, 0.77f * s, z - 0.1f * s, 0.05f * s, 0.02f * s, 0.05f * s, 0, RGB(255, 255, 240)); return; }
    if (v == 5) {   // пень со светлячками
      BX(x, 0.2f * s, z, 0.24f * s, 0.2f * s, 0.24f * s, yaw, RGB(96, 66, 40)); BX(x, 0.41f * s, z, 0.2f * s, 0.02f * s, 0.2f * s, yaw, RGB(180, 140, 90));
      for (int k = 0; k < 2; k++) GLOW(x + sinf(gTime * 1.3f + k * 3 + x) * 0.5f, 0.9f + sinf(gTime * 2 + k) * 0.15f, z + cosf(gTime + k * 2 + z) * 0.5f, 0.08f * s, RGB(190, 255, 120), 190); return; }
    BX(x, 0.6f * s, z, 0.1f * s, 0.6f * s, 0.1f * s, 0, RGB(86, 62, 40));   // тотем с черепом
    BX(x, 1.0f * s, z, 0.14f * s, 0.14f * s, 0.14f * s, 0, RGB(90, 150, 70)); BX(x + inw * 0.15f * s, 1.02f * s, z, 0.02f * s, 0.04f * s, 0.09f * s, 0, RGB(20, 20, 20));
    BX(x, 1.3f * s, z, 0.1f * s, 0.1f * s, 0.1f * s, 0, RGB(235, 230, 210)); return; }
  if (kind == 2) {   // КОСТЯНАЯ ЯМА: черепа, рёбра, кости, мёртвые деревья, надгробия, огоньки
    u32 bone = RGB(226, 220, 202);
    if (v == 0 || v == 1) {   // череп
      BX(x, 0.2f * s, z, 0.24f * s, 0.2f * s, 0.2f * s, 0, bone); BX(x, 0.06f * s, z, 0.2f * s, 0.06f * s, 0.16f * s, 0, mixc(bone, RGB(0, 0, 0), 0.1f));
      BX(x + inw * 0.24f * s, 0.24f * s, z - 0.08f * s, 0.02f * s, 0.06f * s, 0.05f * s, 0, RGB(25, 22, 20)); BX(x + inw * 0.24f * s, 0.24f * s, z + 0.08f * s, 0.02f * s, 0.06f * s, 0.05f * s, 0, RGB(25, 22, 20)); return; }
    if (v == 2) {   // рёбра
      BX(x, 0.58f * s, z, 0.04f * s, 0.04f * s, 0.55f * s, 0, bone);
      for (int k = 0; k < 4; k++) { float zz = z - 0.4f * s + k * 0.27f * s; BX(x, 0.5f * s, zz, 0.2f * s, 0.04f * s, 0.04f * s, 0, bone); BX(x - 0.2f * s, 0.32f * s, zz, 0.04f * s, 0.28f * s, 0.04f * s, 0, bone); BX(x + 0.2f * s, 0.32f * s, zz, 0.04f * s, 0.28f * s, 0.04f * s, 0, bone); } return; }
    if (v == 3) {   // мёртвое дерево
      BX(x, 0.7f * s, z, 0.09f * s, 0.7f * s, 0.09f * s, 0, RGB(70, 56, 46)); BX(x + 0.25f * s, 1.2f * s, z, 0.3f * s, 0.04f * s, 0.04f * s, 0.3f, RGB(70, 56, 46)); BX(x - 0.25f * s, 1.0f * s, z + 0.1f * s, 0.28f * s, 0.04f * s, 0.04f * s, -0.4f, RGB(70, 56, 46)); BX(x, 1.45f * s, z, 0.04f * s, 0.18f * s, 0.04f * s, 0, RGB(70, 56, 46)); return; }
    if (v == 4) {   // надгробие
      BX(x, 0.35f * s, z, 0.07f * s, 0.35f * s, 0.26f * s, 0, RGB(130, 128, 120)); BX(x, 0.74f * s, z, 0.07f * s, 0.06f * s, 0.2f * s, 0, RGB(140, 138, 130)); BX(x + inw * 0.08f * s, 0.4f * s, z, 0.015f * s, 0.12f * s, 0.025f * s, 0, RGB(60, 58, 54)); BX(x + inw * 0.08f * s, 0.46f * s, z, 0.015f * s, 0.025f * s, 0.08f * s, 0, RGB(60, 58, 54)); return; }
    if (v == 5) { drawTree(x, z, s, 2); BX(x + 0.45f * s, 0.12f * s, z + 0.2f * s, 0.2f * s, 0.12f * s, 0.12f * s, 0.8f, bone); return; }
    BX(x, 0.12f * s, z, 0.04f * s, 0.12f * s, 0.04f * s, 0, RGB(220, 210, 180)); GLOW(x, 0.3f * s + sinf(gTime * 3 + x) * 0.04f, z, 0.2f * s, RGB(120, 255, 170), 120); return; }   // свеча-огонёк
  if (kind == 3) {   // ЛЕДЯНОЙ ПИК: заснеженные горы, кристаллы, снеговики, сугробы
    u32 rock = RGB(120, 150, 185), snow = RGB(245, 250, 255);
    if (v <= 1) {   // ледяная гора
      BX(x, 0.6f * s, z, 1.0f * s, 0.6f * s, 1.0f * s, yaw * 0.2f, rock); BX(x, 1.5f * s, z, 0.72f * s, 0.5f * s, 0.72f * s, yaw * 0.2f, mixc(rock, snow, 0.35f)); BX(x, 2.3f * s, z, 0.42f * s, 0.4f * s, 0.42f * s, yaw * 0.2f, mixc(rock, snow, 0.75f)); BX(x, 2.85f * s, z, 0.2f * s, 0.2f * s, 0.2f * s, yaw * 0.2f, snow); return; }
    if (v == 2 || v == 3) {   // ледяные кристаллы
      for (int k = 0; k < 4; k++) { float ox = ((int)((h >> (k * 3)) & 7) - 3) * 0.12f * s, oz = (k - 1.5f) * 0.28f * s, ht = (0.5f + ((h >> (k * 5)) & 7) * 0.12f) * s; BX(x + ox, ht, z + oz, 0.1f * s, ht, 0.1f * s, k * 0.7f, mixc(RGB(150, 215, 245), RGB(255, 255, 255), k * 0.12f)); }
      GLOW(x, 0.6f * s, z, 0.5f * s, RGB(160, 230, 255), 55); return; }
    if (v == 4) {   // снеговик
      BX(x, 0.25f * s, z, 0.28f * s, 0.25f * s, 0.28f * s, 0, snow); BX(x, 0.7f * s, z, 0.2f * s, 0.2f * s, 0.2f * s, 0, snow); BX(x, 1.05f * s, z, 0.15f * s, 0.15f * s, 0.15f * s, 0, snow);
      BX(x + inw * 0.17f * s, 1.05f * s, z, 0.1f * s, 0.025f * s, 0.025f * s, 0, RGB(240, 130, 40)); BX(x + inw * 0.15f * s, 1.12f * s, z - 0.06f * s, 0.01f * s, 0.02f * s, 0.02f * s, 0, RGB(20, 20, 24)); BX(x + inw * 0.15f * s, 1.12f * s, z + 0.06f * s, 0.01f * s, 0.02f * s, 0.02f * s, 0, RGB(20, 20, 24));
      BX(x, 1.24f * s, z, 0.13f * s, 0.03f * s, 0.13f * s, 0, RGB(60, 60, 70)); BX(x, 1.35f * s, z, 0.08f * s, 0.1f * s, 0.08f * s, 0, RGB(60, 60, 70)); return; }
    if (v == 5) { BX(x, 0.18f * s, z, 0.7f * s, 0.18f * s, 0.5f * s, yaw * 0.2f, snow); BX(x + 0.2f * s, 0.34f * s, z, 0.4f * s, 0.1f * s, 0.3f * s, yaw * 0.2f, snow); return; }   // сугроб
    BX(x, 0.35f * s, z, 0.07f * s, 0.35f * s, 0.07f * s, 0, RGB(95, 80, 70));   // заснеженная ёлка
    for (int k = 0; k < 3; k++) BX(x, (0.75f + k * 0.38f) * s, z, (0.5f - k * 0.14f) * s, 0.22f * s, (0.5f - k * 0.14f) * s, k * 0.4f, mixc(RGB(70, 120, 110), snow, 0.55f + k * 0.12f)); return; }
  if (kind == 4) {   // ОГНЕННАЯ ДОЛИНА: вулканы, обсидиановые шипы, жаровни, гейзеры огня
    u32 basalt = RGB(62, 46, 42), lava = RGB(255, 120, 30);
    if (v <= 1) {   // вулкан
      BX(x, 0.6f * s, z, 1.1f * s, 0.6f * s, 1.1f * s, yaw * 0.2f, basalt); BX(x, 1.5f * s, z, 0.8f * s, 0.5f * s, 0.8f * s, yaw * 0.2f, mixc(basalt, RGB(0, 0, 0), 0.15f)); BX(x, 2.3f * s, z, 0.48f * s, 0.4f * s, 0.48f * s, yaw * 0.2f, mixc(basalt, RGB(0, 0, 0), 0.25f));
      BX(x + inw * 0.5f * s, 1.4f * s, z, 0.04f * s, 0.8f * s, 0.12f * s, 0, lava);   // потёк лавы
      GLOW(x, 2.8f * s, z, 0.55f * s, lava, 150 + (int)(40 * sinf(gTime * 4 + x))); GLOW(x + sinf(gTime * 2 + z) * 0.3f, 3.2f * s + fabsf(sinf(gTime * 3 + x)) * 0.5f, z, 0.12f * s, RGB(255, 200, 80), 200); return; }
    if (v == 2 || v == 3) {   // обсидиановые шипы
      for (int k = 0; k < 4; k++) { float ht = (0.5f + ((h >> (k * 4)) & 7) * 0.13f) * s; BX(x + (k - 1.5f) * 0.25f * s, ht, z + ((int)((h >> (k * 3)) & 3) - 1) * 0.2f * s, 0.09f * s, ht, 0.09f * s, k * 0.8f, mixc(RGB(36, 28, 40), RGB(120, 60, 90), k * 0.08f)); }
      GLOW(x, 0.1f, z, 0.45f * s, lava, 70); return; }
    if (v == 4) {   // жаровня
      BX(x, 0.3f * s, z, 0.05f * s, 0.3f * s, 0.05f * s, 0, basalt); BX(x, 0.62f * s, z, 0.2f * s, 0.06f * s, 0.2f * s, 0, RGB(50, 40, 40));
      float fl = 0.05f * sinf(gTime * 12 + x * 3 + z); GLOW(x, 0.85f * s + fl, z, 0.28f * s, lava, 210); GLOW(x, 0.85f * s, z, 0.7f * s, RGB(255, 170, 70), 50); return; }
    if (v == 5) {   // гейзер огня
      BX(x, 0.12f * s, z, 0.3f * s, 0.12f * s, 0.3f * s, 0, basalt);
      for (int k = 0; k < 4; k++) { float t = fmodf(gTime * 0.8f + k * 0.25f + x, 1.0f); GLOW(x + sinf(k * 2.0f + x) * 0.1f, 0.3f + t * 1.6f * s, z, (0.2f - t * 0.1f) * s, mixc(RGB(255, 220, 90), lava, t), (int)(220 * (1 - t))); } return; }
    BX(x, 0.35f * s, z, 0.07f * s, 0.35f * s, 0.07f * s, 0, RGB(30, 24, 22)); BX(x + 0.2f * s, 0.7f * s, z, 0.22f * s, 0.04f * s, 0.04f * s, 0.4f, RGB(30, 24, 22)); GLOW(x, 0.5f * s, z, 0.1f * s, lava, 150); return; }   // обугленное дерево
  if (kind == 6) {   // ЛЕГЕНДАРНАЯ АРЕНА: кубки, золотые колонны, фиолетовые кристаллы, статуи чемпионов, пьедесталы ТОП-3
    u32 gold = RGB(240, 200, 80), goldD = RGB(190, 140, 40), marble = RGB(222, 218, 232), vio = RGB(130, 80, 220);
    if (v == 0) {   // кубок на постаменте
      BX(x, 0.2f * s, z, 0.3f * s, 0.2f * s, 0.3f * s, 0, marble); BX(x, 0.45f * s, z, 0.2f * s, 0.05f * s, 0.2f * s, 0, goldD); BX(x, 0.62f * s, z, 0.05f * s, 0.14f * s, 0.05f * s, 0, gold);
      BX(x, 0.95f * s, z, 0.2f * s, 0.2f * s, 0.2f * s, 0, gold); BX(x, 1.17f * s, z, 0.24f * s, 0.03f * s, 0.24f * s, 0, mixc(gold, RGB(255, 255, 255), 0.4f));
      BX(x, 0.95f * s, z + 0.27f * s, 0.04f * s, 0.1f * s, 0.04f * s, 0, gold); BX(x, 0.95f * s, z - 0.27f * s, 0.04f * s, 0.1f * s, 0.04f * s, 0, gold);
      GLOW(x, 1.0f * s, z, 0.55f * s, RGB(255, 220, 110), 70 + (int)(25 * sinf(gTime * 3 + z))); return; }
    if (v == 1) {   // золотая колонна
      BX(x, 0.15f * s, z, 0.28f * s, 0.15f * s, 0.28f * s, 0, goldD); BX(x, 1.2f * s, z, 0.16f * s, 1.05f * s, 0.16f * s, 0, marble); BX(x, 2.3f * s, z, 0.28f * s, 0.12f * s, 0.28f * s, 0, gold);
      GLOW(x, 2.6f * s, z, 0.4f * s, RGB(180, 120, 255), 150 + (int)(40 * sinf(gTime * 2 + x))); return; }
    if (v == 2) {   // фиолетовые кристаллы
      for (int k = 0; k < 3; k++) { float ht = (0.7f + ((h >> (k * 4)) & 7) * 0.14f) * s; BX(x + (k - 1) * 0.28f * s, ht, z + ((int)((h >> (k * 3)) & 3) - 1) * 0.15f * s, 0.1f * s, ht, 0.1f * s, k * 0.9f, mixc(vio, RGB(255, 255, 255), k * 0.12f)); }
      GLOW(x, 0.7f * s, z, 0.7f * s, RGB(170, 110, 255), 60); return; }
    if (v == 3) {   // статуя чемпиона с поднятым мечом
      BX(x, 0.18f * s, z, 0.32f * s, 0.18f * s, 0.32f * s, 0, marble); BX(x, 0.7f * s, z, 0.16f * s, 0.34f * s, 0.11f * s, 0, mixc(marble, gold, 0.25f)); BX(x, 1.2f * s, z, 0.1f * s, 0.11f * s, 0.1f * s, 0, marble);
      BX(x + inw * 0.05f * s, 1.05f * s, z + 0.2f * s, 0.03f * s, 0.55f * s, 0.03f * s, 0, gold); BX(x, 1.55f * s, z + 0.2f * s, 0.12f * s, 0.03f * s, 0.03f * s, 0, goldD);
      GLOW(x, 1.7f * s, z, 0.3f * s, RGB(255, 230, 130), 120); return; }
    if (v == 4) {   // знамя ТОП
      BX(x, 1.0f * s, z, 0.04f * s, 1.0f * s, 0.04f * s, 0, goldD); float wv = sinf(gTime * 2.5f + z) * 0.05f;
      BX(x + inw * 0.04f * s, 1.45f * s + wv, z + 0.3f * s, 0.015f * s, 0.5f * s, 0.28f * s, 0, RGB(90, 50, 170)); BX(x + inw * 0.05f * s, 1.55f * s + wv, z + 0.3f * s, 0.015f * s, 0.12f * s, 0.12f * s, 0, gold);
      BX(x + inw * 0.05f * s, 1.3f * s + wv, z + 0.3f * s, 0.015f * s, 0.05f * s, 0.2f * s, 0, gold); BX(x, 2.05f * s, z, 0.08f * s, 0.07f * s, 0.08f * s, 0, gold); return; }
    if (v == 5) {   // жаровня с фиолетовым огнём
      BX(x, 0.3f * s, z, 0.05f * s, 0.3f * s, 0.05f * s, 0, goldD); BX(x, 0.62f * s, z, 0.2f * s, 0.06f * s, 0.2f * s, 0, gold);
      float fl = 0.05f * sinf(gTime * 11 + x * 3 + z); GLOW(x, 0.85f * s + fl, z, 0.28f * s, RGB(190, 120, 255), 210); GLOW(x, 0.85f * s, z, 0.7f * s, RGB(150, 90, 255), 50); return; }
    BX(x, 0.15f * s, z - 0.5f * s, 0.25f * s, 0.15f * s, 0.25f * s, 0, RGB(205, 125, 70));   // пьедестал ТОП-3: бронза, золото, серебро
    BX(x, 0.3f * s, z, 0.25f * s, 0.3f * s, 0.25f * s, 0, gold); BX(x, 0.22f * s, z + 0.5f * s, 0.25f * s, 0.22f * s, 0.25f * s, 0, RGB(200, 205, 215));
    GLOW(x, 0.75f * s, z, 0.25f * s, RGB(255, 225, 120), 130); return; }
  // 5: КОРОЛЕВСКИЙ ЗАМОК: башни, статуи рыцарей, фонтаны, живая изгородь, знамёна
  u32 stone = RGB(176, 170, 160), stoneD = RGB(140, 135, 128), gold = RGB(235, 200, 80);
  if (v <= 1) {   // замковая башня с зубцами
    BX(x, 1.0f * s, z, 0.6f * s, 1.0f * s, 0.6f * s, 0, stone); BX(x, 2.1f * s, z, 0.7f * s, 0.12f * s, 0.7f * s, 0, stoneD);
    for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) BX(x + a * 0.6f * s, 2.4f * s, z + b * 0.6f * s, 0.12f * s, 0.18f * s, 0.12f * s, 0, stone);
    BX(x, 2.7f * s, z, 0.4f * s, 0.2f * s, 0.4f * s, 0, RGB(60, 90, 170)); BX(x, 3.1f * s, z, 0.02f * s, 0.25f * s, 0.02f * s, 0, RGB(90, 70, 50)); BX(x + 0.12f * s, 3.25f * s, z, 0.12f * s, 0.07f * s, 0.015f * s, 0, gold);
    BX(x + inw * 0.61f * s, 1.3f * s, z, 0.02f * s, 0.2f * s, 0.1f * s, 0, RGB(255, 215, 120)); GLOW(x + inw * 0.64f * s, 1.3f * s, z, 0.25f * s, RGB(255, 210, 110), 60); return; }
  if (v == 2) {   // статуя рыцаря
    BX(x, 0.15f * s, z, 0.3f * s, 0.15f * s, 0.3f * s, 0, stoneD); BX(x, 0.5f * s, z, 0.14f * s, 0.2f * s, 0.1f * s, 0, stone); BX(x, 0.88f * s, z, 0.18f * s, 0.18f * s, 0.12f * s, 0, stone); BX(x, 1.2f * s, z, 0.09f * s, 0.1f * s, 0.09f * s, 0, stone);
    BX(x + inw * 0.18f * s, 0.95f * s, z + 0.16f * s, 0.02f * s, 0.4f * s, 0.02f * s, 0, gold); return; }
  if (v == 3) {   // фонтан
    DISC(x, z, 0.75f * s, 0.01f, stoneD);
    for (int k = 0; k < 8; k++) { float a = k * 0.7854f; BX(x + cosf(a) * 0.72f * s, 0.12f * s, z + sinf(a) * 0.72f * s, 0.2f * s, 0.12f * s, 0.2f * s, -a, stone); }
    DISC(x, z, 0.6f * s, 0.12f * s, RGB(90, 170, 230)); BX(x, 0.3f * s, z, 0.07f * s, 0.3f * s, 0.07f * s, 0, stone);
    for (int k = 0; k < 3; k++) { float t = fmodf(gTime * 0.9f + k * 0.33f, 1.0f); GLOW(x + cosf(k * 2.1f) * t * 0.35f * s, 0.65f * s + sinf(t * 3.14f) * 0.3f * s, z + sinf(k * 2.1f) * t * 0.35f * s, 0.07f * s, RGB(190, 230, 255), 190); } return; }
  if (v == 4) {   // королевское знамя
    BX(x, 0.9f * s, z, 0.04f * s, 0.9f * s, 0.04f * s, 0, RGB(110, 80, 50)); float wv = sinf(gTime * 2.5f + z) * 0.05f; BX(x + inw * 0.04f * s, 1.3f * s + wv, z + 0.28f * s, 0.015f * s, 0.4f * s, 0.26f * s, 0, RGB(50, 90, 190)); BX(x + inw * 0.05f * s, 1.4f * s + wv, z + 0.28f * s, 0.015f * s, 0.09f * s, 0.09f * s, 0, gold); BX(x, 1.85f * s, z, 0.08f * s, 0.06f * s, 0.08f * s, 0, gold); return; }
  if (v == 5) {   // подстриженная изгородь с шаром
    for (int k = -1; k <= 1; k++) BX(x, 0.3f * s, z + k * 0.5f * s, 0.25f * s, 0.3f * s, 0.26f * s, 0, RGB(46, 120, 60)); BX(x, 0.75f * s, z, 0.2f * s, 0.2f * s, 0.2f * s, 0.5f, RGB(56, 140, 70)); return; }
  BX(x, 0.4f * s, z, 0.09f * s, 0.4f * s, 0.09f * s, 0, RGB(110, 75, 45)); BX(x, 1.1f * s, z, 0.5f * s, 0.45f * s, 0.5f * s, yaw, RGB(60, 140, 66)); BX(x, 1.15f * s, z, 0.38f * s, 0.55f * s, 0.38f * s, yaw + 0.8f, RGB(70, 155, 76));   // круглая липа
}
static bool onBridgeX(float x) { return fabsf(x - BRIDGE_X[0]) < BRIDGE_HW + 0.35f || fabsf(x - BRIDGE_X[1]) < BRIDGE_HW + 0.35f; }
static void drawArena(bool placing, int placeCard) {
  const Arena &ar = ARENAS[clampi(B.arena, 0, NARENA - 1)];
  // дальний фон
  u32 outer = ar.decoKind == 3 ? RGB(225, 235, 245) : (ar.decoKind == 4 ? RGB(55, 38, 34) : (ar.decoKind == 2 ? RGB(110, 100, 85) : mixc(ar.grass2, RGB(0, 30, 0), 0.3f)));
  GQ(-14, -14, AW + 14, AL + 14, -0.03f, outer);
  for (int k = 0; k < 26; k++) { unsigned h = hsh(k * 131u + 7u); float side = (k & 1) ? -1 : 1, px = side < 0 ? -1.2f - (h % 500) / 100.0f : AW + 1.2f + (h % 500) / 100.0f, pz = -3 + (h >> 10) % 38, rr = 0.6f + ((h >> 5) % 10) / 10.0f;   // пятна на земле вокруг
    GQ(px - rr, pz - rr * 0.7f, px + rr, pz + rr * 0.7f, -0.025f, mixc(outer, k % 3 ? RGB(0, 0, 0) : RGB(255, 255, 255), 0.08f)); }
  // клетки травы 2x2
  // клетки травы 1x1 с заметной шахматкой
  u32 gA = mixc(ar.grass, RGB(255, 255, 230), 0.07f), gB = mixc(ar.grass2, RGB(0, 20, 0), 0.1f);
  for (int cz = 0; cz < 32; cz++) for (int cx = 0; cx < 18; cx++) { float z0 = (float)cz, z1 = z0 + 1; if (z1 > RIVER_Z - RIVER_HW && z0 < RIVER_Z + RIVER_HW) continue;
    u32 c = ((cx + cz) & 1) ? gA : gB;
    if (placing && placeCard >= 0 && CARDS[placeCard].type != CT_SPELL) { float mz = zOf(viewTeam, z0 + 0.5f); bool ok = mz <= 15.0f;
      if (!ok && mz <= 21.5f) { int enemy = 1 - viewTeam; float wx = cx + 0.5f; if ((wx < 9 && !towerAlive(enemy, 1)) || (wx >= 9 && !towerAlive(enemy, 2))) ok = true; }
      if (!ok) c = mixc(c, RGB(200, 60, 50), 0.35f); }
    { unsigned th = hsh((unsigned)(cx * 73 + cz * 151 + 11)); c = mixc(c, (th & 1) ? RGB(255, 255, 220) : RGB(0, 0, 0), ((th >> 3) % 5) * 0.008f); }
    GQ((float)cx, z0, cx + 1.0f, z1, 0, c);
    GQ((float)cx, z0 + 0.9f, cx + 1.0f, z1, 0.0005f, mixc(c, RGB(255, 255, 255), 0.06f)); }   // светлая кромка клетки - объём
  // каменные площадки под башнями: плитка со швами
  { u32 st = mixc(ar.path, RGB(170, 165, 155), 0.5f), st2 = mixc(st, RGB(0, 0, 0), 0.12f), seam = mixc(st, RGB(0, 0, 0), 0.35f);
    for (int tm = 0; tm < 2; tm++) for (int k = 0; k < 3; k++) { float px = k == 0 ? 9.0f : BRIDGE_X[k - 1], pz = zOf(tm, k == 0 ? 3.0f : 6.5f), hr = k == 0 ? 2.5f : 2.0f;
      GQ(px - hr - 0.08f, pz - hr - 0.08f, px + hr + 0.08f, pz + hr + 0.08f, 0.002f, seam);
      for (int iz = 0; iz < (int)(hr * 2); iz++) for (int ix = 0; ix < (int)(hr * 2); ix++) GQ(px - hr + ix + 0.04f, pz - hr + iz + 0.04f, px - hr + ix + 0.96f, pz - hr + iz + 0.96f, 0.003f, ((ix + iz) & 1) ? st : st2); } }
  // дорожки: от башен к мостам
  for (int s = 0; s < 2; s++) { float bx = BRIDGE_X[s];
    GQ(bx - 0.6f, 6.5f + 1.3f, bx + 0.6f, RIVER_Z - RIVER_HW, 0.005f, ar.path); GQ(bx - 0.6f, RIVER_Z + RIVER_HW, bx + 0.6f, AL - 6.5f - 1.3f, 0.005f, ar.path); }
  GQ(BRIDGE_X[0], 3.2f, BRIDGE_X[1], 4.2f, 0.004f, ar.path); GQ(BRIDGE_X[0], AL - 4.2f, BRIDGE_X[1], AL - 3.2f, 0.004f, ar.path);
  // река с волнами
  static const u32 WATERS[7] = { RGB(64, 150, 215), RGB(78, 120, 66), RGB(70, 62, 78), RGB(190, 228, 248), RGB(240, 110, 30), RGB(70, 150, 225), RGB(112, 72, 196) };   // лагерь: речка, лес: болото, яма: мутная жижа, пик: лёд, долина: лава, замок: ров, легендарная: фиолетовая магия
  u32 water = WATERS[clampi(B.arena, 0, 6)];
  GQ(-14, RIVER_Z - RIVER_HW, AW + 14, RIVER_Z + RIVER_HW, -0.015f, water);
  GQ(-14, RIVER_Z - RIVER_HW * 0.35f, AW + 14, RIVER_Z + RIVER_HW * 0.35f, -0.014f, mixc(water, RGB(0, 0, 40), 0.18f));    // глубина посередине
  for (int k = 0; k < 8; k++) { float wx = fmodf(k * 2.7f + gTime * 0.6f, AW + 4) - 2; GQ(wx, RIVER_Z - 0.2f + (k % 3) * 0.25f - 0.25f, wx + 1.2f, RIVER_Z - 0.1f + (k % 3) * 0.25f - 0.25f, -0.012f, mixc(water, RGB(255, 255, 255), 0.35f)); }
  for (int k = 0; k < 14; k++) { float wx = fmodf(k * 1.9f - gTime * 0.9f + 50, AW + 4) - 2; float wz = RIVER_Z + ((k * 7) % 5 - 2) * 0.2f; GQ(wx, wz, wx + 0.5f, wz + 0.06f, -0.011f, mixc(water, RGB(255, 255, 255), 0.55f)); }   // блики
  for (int s = -1; s <= 1; s += 2) GQ(-14, RIVER_Z + s * RIVER_HW - (s > 0 ? 0 : 0.18f), AW + 14, RIVER_Z + s * RIVER_HW + (s > 0 ? 0.18f : 0), 0.0f, mixc(ar.path, RGB(0, 0, 0), 0.25f));
  // каменные берега
  for (int sd = -1; sd <= 1; sd += 2) for (int k = 0; k < 24; k++) { float bx = k * 0.78f + ((k * 37) % 7) * 0.03f; if (onBridgeX(bx)) continue; float bz = RIVER_Z + sd * (RIVER_HW + 0.05f);
    BX(bx, 0.05f, bz, 0.36f + (k % 3) * 0.03f, 0.06f, 0.14f, 0, mixc(ar.decoKind == 4 ? RGB(60, 45, 40) : RGB(140, 135, 128), RGB(0, 0, 0), (k % 4) * 0.06f)); }
  // мосты
  for (int s = 0; s < 2; s++) { float bx = BRIDGE_X[s];
    for (int k = 0; k < 6; k++) BX(bx, 0.03f, RIVER_Z - RIVER_HW - 0.3f + k * 0.52f, BRIDGE_HW, 0.06f, 0.24f, 0, k & 1 ? RGB(150, 105, 65) : RGB(135, 95, 58));
    for (int sd = -1; sd <= 1; sd += 2) { BX(bx + sd * (BRIDGE_HW + 0.05f), 0.32f, RIVER_Z, 0.06f, 0.05f, RIVER_HW + 0.4f, 0, RGB(110, 75, 45));
      for (int k = 0; k < 3; k++) BX(bx + sd * (BRIDGE_HW + 0.05f), 0.18f, RIVER_Z - RIVER_HW - 0.2f + k * (RIVER_HW + 0.2f), 0.07f, 0.18f, 0.07f, 0, RGB(100, 68, 40)); } }
  // ограда по краям
  u32 wall = ar.decoKind == 3 ? RGB(205, 220, 235) : (ar.decoKind == 4 ? RGB(80, 62, 56) : (ar.decoKind == 2 ? RGB(150, 140, 125) : RGB(160, 155, 145)));
  for (int sd = 0; sd < 2; sd++) { float bx = sd ? AW + 0.35f : -0.35f;                                     // каменная стена с зубцами по бокам
    for (int k = 0; k < 32; k++) { float z = k + 0.5f; if (fabsf(z - RIVER_Z) < RIVER_HW + 0.3f) continue; u32 c = mixc(wall, RGB(0, 0, 0), ((k * 7) % 3) * 0.06f);
      BX(bx, 0.3f, z, 0.32f, 0.3f, 0.49f, 0, c); if (k & 1) BX(bx, 0.7f, z, 0.3f, 0.1f, 0.35f, 0, mixc(c, RGB(255, 255, 255), 0.08f)); }
    for (int k = 0; k < 4; k++) { float z = k < 2 ? 4.0f + k * 8.0f : AL - 4.0f - (k - 2) * 8.0f; int tm = z < RIVER_Z ? 0 : 1; u32 fc = teamCol(viewTeam == 0 ? tm : 1 - tm);   // флаги команд
      BX(bx, 1.2f, z, 0.04f, 0.9f, 0.04f, 0, RGB(90, 70, 50)); float wv = sinf(gTime * 3 + k + sd) * 0.05f; BX(bx + (sd ? 0.3f : -0.3f), 1.75f + wv, z, 0.28f, 0.2f, 0.02f, 0, fc); BX(bx + (sd ? 0.3f : -0.3f), 1.57f + wv, z, 0.28f, 0.03f, 0.022f, 0, RGB(240, 210, 90)); } }
  for (int sd = 0; sd < 2; sd++) { float bz = sd ? AL + 0.35f : -0.35f; for (int k = 0; k < 18; k++) { u32 c = mixc(wall, RGB(0, 0, 0), ((k * 5) % 3) * 0.06f); BX(k + 0.5f, 0.3f, bz, 0.49f, 0.3f, 0.32f, 0, c); if (k & 1) BX(k + 0.5f, 0.7f, bz, 0.35f, 0.1f, 0.3f, 0, mixc(c, RGB(255, 255, 255), 0.08f)); } }
  for (int s2 = 0; s2 < 2; s2++) for (int e = -1; e <= 1; e += 2) { float tx = BRIDGE_X[s2] + (s2 ? 1 : -1) * (BRIDGE_HW + 0.5f), tz = RIVER_Z + e * (RIVER_HW + 0.55f);   // факелы у мостов
    BX(tx, 0.45f, tz, 0.07f, 0.45f, 0.07f, 0, RGB(80, 60, 45)); BX(tx, 0.92f, tz, 0.12f, 0.04f, 0.12f, 0, RGB(60, 55, 50));
    float fl = 0.06f * sinf(gTime * 13 + tx * 3 + tz); GLOW(tx, 1.08f + fl, tz, 0.22f, RGB(255, 140, 40), 200); GLOW(tx, 1.08f, tz, 0.55f, RGB(255, 180, 80), 45); }
  // трава с цветами и камешками на клетках
  for (int k = 0; k < 90; k++) { unsigned h = hsh(k * 2654435761u + B.arena * 97u); float px = 0.3f + (h % 1740) / 100.0f, pz = 0.3f + ((h >> 11) % 3140) / 100.0f;
    if (fabsf(pz - RIVER_Z) < RIVER_HW + 0.4f) continue; if (fabsf(px - BRIDGE_X[0]) < 0.8f || fabsf(px - BRIDGE_X[1]) < 0.8f) continue;
    bool nearT = false; for (int tm = 0; tm < 2; tm++) { if (fabsf(px - 9) < 2.8f && fabsf(pz - zOf(tm, 3.0f)) < 2.8f) nearT = true; for (int q = 0; q < 2; q++) if (fabsf(px - BRIDGE_X[q]) < 2.3f && fabsf(pz - zOf(tm, 6.5f)) < 2.3f) nearT = true; } if (nearT) continue;
    int kind = (h >> 24) % 10; u32 c;
    if (ar.decoKind == 3) c = kind < 6 ? RGB(250, 252, 255) : RGB(170, 210, 240);                          // снег и льдинки
    else if (ar.decoKind == 4) { c = kind < 3 ? RGB(255, 120, 30) : RGB(40, 30, 28); if (kind < 3) GLOW(px, 0.05f, pz, 0.12f, RGB(255, 120, 30), 60); }   // угли
    else if (ar.decoKind == 2) c = kind < 4 ? RGB(230, 225, 210) : RGB(95, 88, 76);                          // косточки и камешки
    else if (ar.decoKind == 1) c = kind < 3 ? RGB(200, 70, 70) : (kind < 5 ? RGB(170, 240, 120) : mixc(ar.grass, RGB(0, 30, 0), 0.4f));   // грибочки, светляки и тёмный мох
    else if (ar.decoKind == 6) c = kind < 2 ? RGB(240, 200, 80) : (kind < 4 ? RGB(170, 130, 250) : mixc(ar.grass, RGB(255, 255, 255), 0.12f));   // золотая и фиолетовая пыль
    else if (ar.decoKind == 0) c = kind < 4 ? RGB(205, 175, 110) : (kind < 6 ? RGB(250, 215, 60) : mixc(ar.grass, RGB(0, 40, 0), 0.2f));   // вытоптанная земля и сено
    else c = kind < 2 ? RGB(250, 245, 240) : (kind < 4 ? RGB(250, 215, 60) : (kind < 5 ? RGB(230, 90, 120) : mixc(ar.grass, RGB(0, 40, 0), 0.25f)));   // ромашки, лютики, тюльпаны, кустики травы
    if (kind >= 5 && ar.decoKind != 4) BX(px, 0.05f, pz, 0.05f, 0.05f, 0.02f, k, c); else GQ(px - 0.07f, pz - 0.07f, px + 0.07f, pz + 0.07f, 0.004f, c); }
  // украшения вокруг: у каждой арены свои, под её название
  for (int k = 0; k < 36; k++) { unsigned h = hsh(k * 31u + B.arena * 7u); float side = (k & 1) ? -1 : 1, x = side < 0 ? -2.2f - (h % 300) / 100.0f : AW + 2.2f + (h % 300) / 100.0f, z = (k / 2) * 1.8f - 0.5f + ((h >> 9) % 100) / 100.0f;
    float sc = 0.9f + ((h >> 4) & 7) / 14.0f; if (ar.decoKind == 3 || ar.decoKind == 4) sc *= 0.9f;
    drawProp(ar.decoKind, x, z, sc, h, side < 0 ? 1.0f : -1.0f); }
  if (ar.decoKind >= 5) for (int sd = 0; sd < 2; sd++) for (int k = 0; k < 5; k++) { float x = sd ? AW + 1.5f : -1.5f, z = 3.0f + k * 6.5f; bool lg = ar.decoKind == 6; BX(x, 1.2f, z, 0.5f, 1.2f, 0.5f, 0, lg ? RGB(224, 220, 236) : RGB(170, 165, 155)); BX(x, 2.6f, z, 0.25f, 0.2f, 0.4f, 0, sd ? COL_EN : COL_ME); if (lg) GLOW(x, 3.1f, z, 0.5f, RGB(255, 215, 100), 110); }
  // особенности самого поля
  if (ar.decoKind == 3) { for (int k = 0; k < 16; k++) { unsigned h = hsh(k * 977u + 5u); float wx = -13 + (h % 2600) / 100.0f, wz = RIVER_Z - RIVER_HW * 0.8f + ((h >> 12) % 100) / 100.0f * RIVER_HW * 1.6f; GQ(wx, wz, wx + 1.4f + (h % 3) * 0.4f, wz + 0.04f, -0.012f, RGB(255, 255, 255)); } }   // трещины на льду
  if (ar.decoKind == 4) { for (int k = 0; k < 22; k++) { unsigned h = hsh(k * 613u + 3u); float px = 0.5f + (h % 1700) / 100.0f, pz = 0.5f + ((h >> 11) % 3100) / 100.0f; if (fabsf(pz - RIVER_Z) < RIVER_HW + 0.5f) continue; if (fabsf(px - 9) < 3.0f && (pz < 6.0f || pz > AL - 6.0f)) continue;
      float len = 0.5f + (h % 5) * 0.2f; GQ(px, pz, px + len, pz + 0.06f, 0.0035f, RGB(255, 110, 30)); GQ(px + len * 0.5f, pz, px + len * 0.5f + 0.06f, pz + len * 0.6f, 0.0035f, RGB(255, 150, 50)); GLOW(px + len * 0.5f, 0.04f, pz, 0.2f, RGB(255, 120, 30), 45); } }   // трещины с лавой на земле
  if (ar.decoKind >= 5) { u32 carp = ar.decoKind == 6 ? RGB(96, 52, 170) : RGB(170, 40, 50); for (int s = 0; s < 2; s++) { float bx = BRIDGE_X[s];
      GQ(bx - 0.5f, 7.9f, bx + 0.5f, RIVER_Z - RIVER_HW, 0.006f, carp); GQ(bx - 0.5f, RIVER_Z + RIVER_HW, bx + 0.5f, AL - 7.9f, 0.006f, carp);   // красная дорожка к мостам
      for (int sd = -1; sd <= 1; sd += 2) { GQ(bx + sd * 0.5f - 0.05f, 7.9f, bx + sd * 0.5f + 0.05f, RIVER_Z - RIVER_HW, 0.0065f, RGB(235, 200, 80)); GQ(bx + sd * 0.5f - 0.05f, RIVER_Z + RIVER_HW, bx + sd * 0.5f + 0.05f, AL - 7.9f, 0.0065f, RGB(235, 200, 80)); } } }
}

// ---- полоски здоровья ----
static void hpBar(float x, float y, float z, float frac, int team, int w, bool lvlTag, int lvl, bool big) {
  int sx, sy; float zz; if (!projectPt(RP(x, y, z), sx, sy, zz)) return; if (sx < -IW || sx > 2 * IW || sy < -IH || sy > 2 * IH) return;
  int h = imax(2, fs * (big ? 3 : 2)), bw = w; int x0 = sx - bw / 2, y0 = sy - h;
  fillRect(x0 - 1, y0 - 1, bw + 2, h + 2, RGB(20, 20, 24)); fillRect(x0, y0, (int)(bw * clampf(frac, 0, 1)), h, team == viewTeam ? RGB(80, 160, 255) : RGB(240, 70, 60));
  if (lvlTag) { char b[8]; snprintf(b, sizeof b, "%d", lvl); int r = h + fs; fillRect(x0 - r * 2 - 1, y0 - r / 2, r * 2, h + r, team == viewTeam ? RGB(40, 90, 200) : RGB(180, 40, 35)); drawText(x0 - r, y0 - r / 2 + 1, b, imax(1, fs / 2 + (big ? 1 : 0)), RGB(255, 255, 255), 1); }
}

// ---- снаряды и эффекты ----
// след в полёте: отрезок между двумя точками на экране
static void STREAK(float x0, float y0, float z0, float x1, float y1, float z1, u32 col, int th) {
  int ax, ay, bx, by; float za, zb2; if (!projectPt(RP(x0, y0, z0), ax, ay, za) || !projectPt(RP(x1, y1, z1), bx, by, zb2)) return;
  if (ax < -IW || ax > 2 * IW || bx < -IW || bx > 2 * IW || ay < -IH || ay > 2 * IH || by < -IH || by > 2 * IH) return;
  drawLine(ax, ay, bx, by, col, th);
}
// стрела: древко по направлению полёта, наконечник и оперение
static void drawArrow(float x, float y, float z, float dx, float dy, float dz, float len, u32 shaft) {
  float l = sqrtf(dx * dx + dy * dy + dz * dz); if (l < 1e-4f) { dx = 0; dy = -1; dz = 0; l = 1; } dx /= l; dy /= l; dz /= l;
  int th = imax(2, (int)(focal / 170.0f));
  STREAK(x - dx * len, y - dy * len - 0.05f, z - dz * len, x, y - 0.05f, z, RGB(30, 22, 16), th + 1);                       // тёмная кромка - стрелу видно на траве
  STREAK(x - dx * len, y - dy * len, z - dz * len, x, y, z, shaft, th);
  STREAK(x - dx * len, y - dy * len + 0.04f, z - dz * len, x - dx * len * 0.75f, y - dy * len * 0.75f + 0.04f, z - dz * len * 0.75f, RGB(240, 240, 245), th);   // оперение
  STREAK(x - dx * 0.08f, y - dy * 0.08f, z - dz * 0.08f, x, y, z, RGB(220, 225, 235), th + 1);                                                                  // наконечник
}
static void projPos(const Proj &p, float k, float &x, float &y, float &z) { x = p.sx + (p.tx - p.sx) * k; z = p.sz + (p.tz - p.sz) * k; y = p.sy + (p.ty - p.sy) * k + (p.arc ? sinf(k * 3.14159f) * p.arcH * fminv(1.0f, p.T * 1.5f) : 0); }
static void drawProjs(float alpha) {
  for (size_t i = 0; i < B.projs.size(); i++) { const Proj &p = B.projs[i];
    float x = p.px + (p.x - p.px) * alpha, z = p.pz + (p.z - p.pz) * alpha, y = p.py + (p.y - p.py) * alpha;
    float dx = x - p.px, dy = y - p.py, dz = z - p.pz; if (dx * dx + dy * dy + dz * dz < 1e-6f) { dx = p.tx - p.sx; dz = p.tz - p.sz; dy = -0.15f * sqrtf(dx * dx + dz * dz); }
    switch (p.kind) {
      case PJ_ARROW: drawArrow(x, y, z, dx, dy, dz, 0.7f, RGB(215, 170, 95)); break;
      case PJ_BOLT: drawArrow(x, y, z, dx, dy, dz, 0.55f, RGB(70, 60, 55)); GLOW(x, y, z, 0.08f, RGB(255, 240, 200), 120); break;
      case PJ_FIRE: { for (int t = 1; t <= 4; t++) GLOW(x - dx * t * 1.2f, y - dy * t * 1.2f, z - dz * t * 1.2f, 0.22f - t * 0.035f, RGB(255, 120 - t * 15, 30), 150 - t * 30); GLOW(x, y, z, 0.3f, RGB(255, 150, 40), 220); } break;
      case PJ_DART: drawArrow(x, y, z, dx, dy, dz, 0.3f, RGB(120, 200, 90)); break;
      case PJ_LAVA: { for (int t = 1; t <= 3; t++) GLOW(x - dx * t * 1.2f, y - dy * t * 1.2f, z - dz * t * 1.2f, 0.14f - t * 0.03f, RGB(255, 110, 30), 140 - t * 35); GLOW(x, y, z, 0.17f, RGB(255, 160, 50), 210); } break;
      case PJ_ICE: { for (int t = 1; t <= 4; t++) GLOW(x - dx * t * 1.1f, y - dy * t * 1.1f, z - dz * t * 1.1f, 0.15f - t * 0.025f, RGB(170, 230, 255), 140 - t * 28); GLOW(x, y, z, 0.2f, RGB(200, 240, 255), 220); GLOW(x, y, z, 0.1f, RGB(255, 255, 255), 240); } break;
      case PJ_DARK: { for (int t = 1; t <= 3; t++) GLOW(x - dx * t * 1.2f, y - dy * t * 1.2f, z - dz * t * 1.2f, 0.16f - t * 0.03f, RGB(90, 220, 110), 120 - t * 30); GLOW(x, y, z, 0.22f, RGB(140, 255, 150), 200); } break;
      case PJ_BOMB: BX(x, y, z, 0.13f, 0.13f, 0.13f, gTime * 7, RGB(30, 30, 34)); BX(x, y + 0.17f, z, 0.02f, 0.05f, 0.02f, 0, RGB(120, 100, 80)); GLOW(x, y + 0.24f, z, 0.08f + 0.03f * sinf(gTime * 40), RGB(255, 200, 80), 230); break;
      case PJ_CANNON: BX(x, y, z, 0.12f, 0.12f, 0.12f, 0, RGB(40, 40, 46)); STREAK(x - dx * 1.5f, y - dy * 1.5f, z - dz * 1.5f, x, y, z, RGB(200, 200, 200), 1); break;
    } }
}
static void spellFrom(const Spell &s, float tx, float tz, float k, float peak, float &x, float &y, float &z) {   // полёт от короля по дуге
  x = s.sx + (tx - s.sx) * k; z = s.sz + (tz - s.sz) * k; y = 4.8f * (1 - k) + sinf(k * 3.14159f) * peak;
}
static void drawSpells() {
  for (size_t i = 0; i < B.spells.size(); i++) { const Spell &s = B.spells[i]; float k = s.delay > 0 ? clampf(s.t / s.delay, 0, 1) : 1, after = s.t - s.delay;
    u32 grass = ARENAS[B.arena].grass;
    switch (s.kind) {
      case SP_MAGEFALL:
      case SP_FIREBALL:
        if (!s.hit) { float x, y, z; spellFrom(s, s.x, s.z, k, 5.0f, x, y, z);
          for (int t = 6; t >= 1; t--) { float kk = fmaxv(0.0f, k - t * 0.035f), tx, ty, tz; spellFrom(s, s.x, s.z, kk, 5.0f, tx, ty, tz); GLOW(tx, ty, tz, 0.55f - t * 0.06f, t > 3 ? RGB(120, 110, 100) : RGB(255, 110 + t * 15, 30), 170 - t * 22); }
          GLOW(x, y, z, 0.75f + 0.08f * sinf(gTime * 30), RGB(255, 120, 20), 230); GLOW(x, y, z, 0.4f, RGB(255, 230, 140), 240);
          DISC(s.x, s.z, s.r * (0.4f + 0.6f * k), 0.02f, mixc(grass, RGB(255, 90, 40), 0.25f)); }
        break;
      case SP_ROCKET: if (!s.hit) {                                // ракета с королевской башни: дуга, дымный след, пламя из сопла
          float x, y, z, x2, y2, z2; spellFrom(s, s.x, s.z, k, 6.5f, x, y, z); spellFrom(s, s.x, s.z, fmaxv(0.0f, k - 0.02f), 6.5f, x2, y2, z2);
          float dx = x - x2, dy = y - y2, dz = z - z2, l = sqrtf(dx * dx + dy * dy + dz * dz); if (l < 1e-4f) { dx = 0; dy = 1; dz = 0; l = 1; } dx /= l; dy /= l; dz /= l;
          DISC(s.x, s.z, s.r, 0.015f, mixc(grass, RGB(255, 60, 40), 0.25f + 0.2f * sinf(gTime * 12)));
          for (int t = 1; t <= 10; t++) { float kk = fmaxv(0.0f, k - t * 0.025f), tx, ty, tz; spellFrom(s, s.x, s.z, kk, 6.5f, tx, ty, tz); GLOW(tx, ty, tz, 0.18f + t * 0.04f, mixc(RGB(230, 225, 220), RGB(120, 115, 110), t / 10.0f), 150 - t * 12); }   // дым
          int th = imax(3, (int)(focal / 70.0f));
          STREAK(x - dx * 1.1f, y - dy * 1.1f - 0.05f, z - dz * 1.1f, x + dx * 0.3f, y + dy * 0.3f - 0.05f, z + dz * 0.3f, RGB(30, 30, 34), th + 2);
          STREAK(x - dx * 1.0f, y - dy * 1.0f, z - dz * 1.0f, x, y, z, RGB(230, 230, 235), th);                                    // корпус
          STREAK(x - dx * 0.55f, y - dy * 0.55f, z - dz * 0.55f, x - dx * 0.35f, y - dy * 0.35f, z - dz * 0.35f, teamCol(s.team), th + 1);   // полоса
          STREAK(x, y, z, x + dx * 0.3f, y + dy * 0.3f, z + dz * 0.3f, RGB(220, 50, 40), th);                                            // нос
          STREAK(x - dx * 1.0f + dz * 0.2f, y - dy * 1.0f, z - dz * 1.0f - dx * 0.2f, x - dx * 0.75f, y - dy * 0.75f, z - dz * 0.75f, RGB(220, 50, 40), imax(2, th - 1));
          STREAK(x - dx * 1.0f - dz * 0.2f, y - dy * 1.0f, z - dz * 1.0f + dx * 0.2f, x - dx * 0.75f, y - dy * 0.75f, z - dz * 0.75f, RGB(220, 50, 40), imax(2, th - 1));   // стабилизаторы
          GLOW(x - dx * 1.15f, y - dy * 1.15f, z - dz * 1.15f, 0.35f + 0.08f * sinf(gTime * 50), RGB(255, 170, 50), 230); }
        break;
      case SP_ARROWS:                                              // залп летит от короля дугой и падает дождём
        for (int a = 0; a < 22; a++) { unsigned h = hsh(a * 13u + (unsigned)(s.x * 10) + (unsigned)(s.z * 100));
          float ox = (((h & 255) / 255.0f) - 0.5f) * s.r * 1.5f, oz = ((((h >> 8) & 255) / 255.0f) - 0.5f) * s.r * 1.5f, tx = s.x + ox, tz = s.z + oz;
          float lag = (h >> 16 & 31) / 31.0f * 0.12f, kk = clampf((s.t - lag) / fmaxv(0.01f, s.delay - lag), 0, 1);
          if (kk <= 0) continue;
          if (kk < 1) { float x, y, z, x2, y2, z2; spellFrom(s, tx, tz, kk, 7.0f, x, y, z); spellFrom(s, tx, tz, fmaxv(0, kk - 0.03f), 7.0f, x2, y2, z2); drawArrow(x, y, z, x - x2, y - y2, z - z2, 0.85f, RGB(215, 170, 95)); }
          else if (after < 0.8f) { float sink = fminv(1.0f, after * 6); STREAK(tx, 0.02f, tz, tx - 0.06f, 0.45f - sink * 0.15f, tz - 0.1f, RGB(130, 95, 60), 1); STREAK(tx - 0.06f, 0.45f - sink * 0.15f, tz - 0.1f, tx - 0.07f, 0.52f - sink * 0.15f, tz - 0.12f, RGB(240, 240, 245), 1); } }
        if (s.hit && after < 0.25f) DISC(s.x, s.z, s.r, 0.02f, mixc(grass, RGB(255, 240, 200), 0.3f * (1 - after * 4)));
        break;
      case SP_FREEZE: if (s.t < s.time) { float fade = clampf((s.time - s.t) / 0.5f, 0, 1), grow = clampf(s.t * 4, 0, 1);
          DISC(s.x, s.z, s.r * grow, 0.03f, mixc(grass, RGB(190, 230, 255), 0.6f * fade));
          for (int a = 0; a < 10; a++) { float aa = a * 0.628f; float rx = s.x + cosf(aa) * s.r * 0.92f * grow, rz = s.z + sinf(aa) * s.r * 0.92f * grow; BX(rx, 0.25f * fade, rz, 0.08f, 0.25f * fade + 0.01f, 0.08f, aa, RGB(200, 235, 255)); }   // ледяные иглы по краю
          for (int a = 0; a < 16; a++) { unsigned h = hsh(a * 7u + 99u); float ph = fmodf(gTime * 0.6f + (h & 255) / 255.0f, 1.0f), rr = ((h >> 8) & 255) / 255.0f * s.r * 0.9f, aa = ((h >> 16) & 255) / 255.0f * 6.28f;
            GLOW(s.x + cosf(aa) * rr, 4.0f * (1 - ph), s.z + sinf(aa) * rr, 0.07f, RGB(240, 250, 255), (int)(200 * fade)); } } break;   // снежинки
      case SP_BARREL: if (!s.hit) {                              // бочка летит от короля и крутится
          float bx, by, bz; spellFrom(s, s.x, s.z, k, 5.5f, bx, by, bz); float rot = s.t * 9.0f;
          DISC(s.x, s.z, s.r * (0.4f + 0.6f * k), 0.015f, mixc(grass, RGB(40, 30, 20), 0.25f));
          drawBox(RP(bx, by, bz), v3(0.32f, 0.4f, 0.32f), RY(rot), RGB(140, 95, 55), true);
          drawBox(RP(bx, by + 0.22f, bz), v3(0.345f, 0.04f, 0.345f), RY(rot), RGB(120, 120, 130), true); drawBox(RP(bx, by - 0.22f, bz), v3(0.345f, 0.04f, 0.345f), RY(rot), RGB(120, 120, 130), true);
          drawBox(RP(bx, by + 0.42f, bz), v3(0.12f, 0.06f, 0.12f), RY(rot), RGB(110, 180, 80), true); }   // торчит гоблинская голова
        break;
      case SP_HEAL: if (s.t < s.time) { DISC(s.x, s.z, s.r, 0.03f, mixc(grass, RGB(255, 240, 120), 0.45f));
          for (int a = 0; a < 12; a++) { unsigned h = hsh(a * 11u + 5u); float ph = fmodf(gTime * 0.8f + (h & 255) / 255.0f, 1.0f), rr = ((h >> 8) & 255) / 255.0f * s.r * 0.85f, aa = ((h >> 16) & 255) / 255.0f * 6.28f;
            float px = s.x + cosf(aa) * rr, pz = s.z + sinf(aa) * rr, py = 0.2f + ph * 2.2f; int sx, sy; float zz;
            if (projectPt(RP(px, py, pz), sx, sy, zz) && sx > 0 && sx < IW && sy > 0 && sy < IH) { int r = imax(2, (int)(focal * 0.12f / zz)); u32 c = mixc(RGB(255, 250, 160), RGB(120, 255, 140), ph); int al = (int)(230 * (1 - ph));
              fillRectA(sx - r, sy - r / 3, 2 * r + 1, (2 * r) / 3 + 1, c, al); fillRectA(sx - r / 3, sy - r, (2 * r) / 3 + 1, 2 * r + 1, c, al); } } } break;   // плюсики лечения
    } }
}
static void drawFx() {
  for (size_t i = 0; i < B.fx.size(); i++) { const Fx &f = B.fx[i]; float k = f.t / f.T;
    switch (f.kind) {
      case FX_HIT: GLOW(f.x, f.y, f.z, 0.25f * (1 - k), RGB(255, 230, 150), (int)(220 * (1 - k))); break;
      case FX_SPLASH: GLOW(f.x, f.y, f.z, f.r * 0.6f * (0.5f + k), RGB(255, 130, 40), (int)(170 * (1 - k))); break;
      case FX_BOOM: { DISC(f.x, f.z, f.r * (0.4f + 0.8f * k), 0.04f, mixc(RGB(255, 200, 80), RGB(90, 60, 40), k));
        for (int a = 0; a < 8; a++) { float aa = a * 0.785f; GLOW(f.x + cosf(aa) * f.r * k, 0.4f + 1.5f * k * (1 - k) * 2, f.z + sinf(aa) * f.r * k, 0.35f * (1 - k), RGB(255, 150, 50), (int)(200 * (1 - k))); } } break;
      case FX_DEATH: for (int a = 0; a < 6; a++) { float aa = a * 1.047f; BX(f.x + cosf(aa) * k * 0.7f, f.y + 0.3f + k * 0.8f, f.z + sinf(aa) * k * 0.7f, 0.08f * (1 - k), 0.08f * (1 - k), 0.08f * (1 - k), 0, RGB(200, 200, 205)); } break;
      case FX_DEPLOY: DISC(f.x, f.z, f.r * (0.6f + 0.6f * k), 0.025f, mixc(teamCol(f.team), ARENAS[B.arena].grass, k)); break;
      case FX_ZAP: { DISC(f.x, f.z, f.r * (0.7f + 0.3f * k), 0.03f, mixc(RGB(160, 220, 255), ARENAS[B.arena].grass, k)); GLOW(f.x, 2.0f, f.z, 1.2f * (1 - k), RGB(200, 240, 255), (int)(230 * (1 - k)));
        for (int br = 0; br < 3; br++) { float bx = f.x + (br - 1) * f.r * 0.5f, bz = f.z + ((br * 37) % 3 - 1) * f.r * 0.3f; int sx0, sy0, sx1, sy1; float zz;
          if (projectPt(RP(bx, 12, bz), sx0, sy0, zz) && projectPt(RP(bx, 0, bz), sx1, sy1, zz) && sx0 > -IW && sx0 < 2 * IW) { int px = sx0, py = sy0; for (int st = 1; st <= 7; st++) { int nx = sx0 + (sx1 - sx0) * st / 7 + (st < 7 ? (int)((hsh(st * 13u + br * 71u + (unsigned)(f.t * 40)) % 21) - 10) * fs : 0), ny = sy0 + (sy1 - sy0) * st / 7;
              drawLine(px, py, nx, ny, RGB(255, 250, 200), imax(1, fs - 1)); px = nx; py = ny; } } }
        int sx0, sy0, sx1, sy1; float zz; if (projectPt(RP(f.x, 9, f.z), sx0, sy0, zz) && projectPt(RP(f.x, 0, f.z), sx1, sy1, zz)) { int px = sx0, py = sy0; for (int s = 1; s <= 6; s++) { int nx = sx0 + (sx1 - sx0) * s / 6 + (s < 6 ? (int)((hsh(s * 7u + (unsigned)(f.t * 30)) % 17) - 8) * fs : 0), ny = sy0 + (sy1 - sy0) * s / 6; drawLine(px, py, nx, ny, RGB(230, 245, 255), imax(1, fs)); px = nx; py = ny; } } } break;
      case FX_TOWERDOWN: for (int a = 0; a < 10; a++) { unsigned h = hsh(a * 17u); float aa = a * 0.628f, r = k * 2.5f * (0.5f + (h & 7) / 10.0f); BX(f.x + cosf(aa) * r, 0.3f + 3.0f * k * (1 - k) * 2, f.z + sinf(aa) * r, 0.25f, 0.2f, 0.25f, aa, RGB(160, 155, 145)); } break;
    } }
}
// ---- весь бой ----
static bool gShowBars = true;
static void drawBattle3D(float alpha, bool placing, int placeCard, float gx, float gz, bool ghostOk) {
  fogCol = RGB(120, 160, 200); fogA = 1e9f; fogB = 2e9f; gLight = 1; gNight = 0; LDIR = vnorm(v3(-0.4f, 0.85f, -0.35f)); gNearZ = 0.3f;
  fillSky(); clearDepth();
  drawArena(placing, placeCard);
  // юниты и башни (тени - тёмные круги)
  for (size_t i = 0; i < B.units.size(); i++) { const Unit &u = B.units[i]; const UnitT &t = UT(u);
    float x = u.px + (u.x - u.px) * alpha, z = u.pz + (u.z - u.pz) * alpha;
    if (!u.alive) { if (t.kind == UT_TOWER) { BX(x, 0.25f, z, t.radius * 0.9f, 0.25f, t.radius * 0.9f, 0.3f, RGB(110, 105, 100)); BX(x + 0.4f, 0.55f, z - 0.3f, 0.5f, 0.3f, 0.4f, 0.7f, RGB(130, 125, 118)); } continue; }
    DISC(x, z, t.radius * (t.kind == UT_TOWER ? 1.0f : 0.9f), 0.012f, mixc(ARENAS[B.arena].grass, RGB(0, 0, 0), 0.35f));
    float y = t.air ? 2.0f + sinf(gTime * 3 + u.id) * 0.15f : 0;
    if (u.pullT > 0) STREAK(x, 0.9f, z, u.jx0, 1.4f, u.jz0, RGB(150, 150, 160), imax(2, (int)(focal / 120.0f)));   // натянутая цепь медного голема
    if (u.type == U_INFERNO && u.attacking && u.target >= 0 && u.deployT <= 0) { const Unit *bt = unitById(u.target);
      if (bt) { float bx = bt->px + (bt->x - bt->px) * alpha, bz = bt->pz + (bt->z - bt->pz) * alpha, by = UT(*bt).air ? bt->y : 0.8f; int th = imax(2, (int)(focal / 110.0f)); float fl = 0.5f + 0.5f * sinf(gTime * 40);
        if (u.infT >= INF_T3) { STREAK(x, 1.75f, z, bx, by, bz, RGB(150, 190, 255), th * 3); STREAK(x, 1.75f, z, bx, by, bz, RGB(230, 245, 255), th + 1); GLOW(bx, by, bz, 0.5f + 0.2f * fl, RGB(170, 210, 255), 210); }
        else if (u.infT >= INF_T2) { STREAK(x, 1.75f, z, bx, by, bz, RGB(255, 70, 40), th * 2); STREAK(x, 1.75f, z, bx, by, bz, RGB(255, 220, 150), th); GLOW(bx, by, bz, 0.35f + 0.1f * fl, RGB(255, 120, 50), 190); }
        else { STREAK(x, 1.75f, z, bx, by, bz, RGB(255, 150, 40), th + 1); STREAK(x, 1.75f, z, bx, by, bz, RGB(255, 235, 180), imax(1, th - 1)); GLOW(bx, by, bz, 0.22f, RGB(255, 170, 60), 170); } } }   // луч инферно: оранжевый > красный > бело-голубой
    if (u.jumpT > 0) y += sinf(clampf(1.0f - u.jumpT / 0.8f, 0, 1) * 3.14159f) * 2.5f;               // прыжок мегарыцаря
    if (riverJumper(u.type) && fabsf(z - RIVER_Z) < RIVER_HW + 0.8f) y += cosf((z - RIVER_Z) / (RIVER_HW + 0.8f) * 1.5708f) * 1.3f;   // прыжок через реку
    float dep = u.deployT > 0 ? clampf(1.0f - u.deployT, 0.2f, 1) : 1;
    if (u.deployT > 0) y += (1 - dep) * 1.2f;
    gShieldOn = u.shield > 0; drawModel(t.model, u.team, x, y, z, u.face, u.anim, u.atkAnim, u.flash, u.frozenT > 0 || u.slowT > 0, 1); gShieldOn = true;
    if (u.charging) GLOW(x, 0.8f, z, 0.5f, RGB(255, 230, 120), 90); }
  if (placing && placeCard >= 0) {                            // призрак на месте будущей карты
    const Card &c = CARDS[placeCard]; u32 rc = ghostOk ? RGB(255, 255, 255) : RGB(255, 80, 70);
    if (c.type == CT_SPELL) DISC(gx, gz, c.sR, 0.03f, mixc(ARENAS[B.arena].grass, rc, 0.45f));
    else { const UnitT &ut = UNITS[c.unit]; DISC(gx, gz, ut.radius + 0.5f, 0.03f, mixc(ARENAS[B.arena].grass, rc, 0.45f));
      if (ut.kind == UT_BUILD || ut.range > 2) { for (int a = 0; a < 24; a++) { float aa = a * 0.2618f; GQ(gx + cosf(aa) * (ut.range + ut.radius) - 0.06f, gz + sinf(aa) * (ut.range + ut.radius) - 0.06f, gx + cosf(aa) * (ut.range + ut.radius) + 0.06f, gz + sinf(aa) * (ut.range + ut.radius) + 0.06f, 0.035f, rc); } }
      if (ghostOk) drawModel(ut.model, viewTeam, gx, ut.air ? 2.0f : 0, gz, viewTeam == 0 ? 0 : 3.14159f, 0, 0, 0, false, 1); } }
  drawSpells(); drawProjs(alpha); drawFx();
  // полоски здоровья
  if (gShowBars) for (size_t i = 0; i < B.units.size(); i++) { const Unit &u = B.units[i]; if (!u.alive) continue; const UnitT &t = UT(u);
    float x = u.px + (u.x - u.px) * alpha, z = u.pz + (u.z - u.pz) * alpha, top = modelTop(t.model) + (t.air ? 2.0f : 0) + 0.25f;
    if (t.kind == UT_TOWER) { bool active = u.type != U_KING || B.side[u.team].kingActive;
      hpBar(x, top, z, u.hp / u.maxhp, u.team, (int)(focal * 0.075f) * (u.type == U_KING ? 2 : 1) / 1, true, u.lvl, true);
      int sx, sy; float zz; if (projectPt(RP(x, top, z), sx, sy, zz) && sx > -IW && sx < 2 * IW && sy > -IH && sy < 2 * IH) { char b[16]; snprintf(b, sizeof b, "%d", (int)ceilf(u.hp)); drawTextSh(sx, sy - fs * 12, b, fs, RGB(255, 255, 255), 1); if (!active) drawTextSh(sx, sy + fs * 2, "ZZZ", fs, RGB(220, 220, 255), 1); } }
    else if (u.hp < u.maxhp - 0.5f || t.kind == UT_BUILD) hpBar(x, top, z, u.hp / u.maxhp, u.team, (int)(focal * (t.radius > 0.7f ? 0.06f : 0.035f)), false, 0, false);
    if (u.deployT > 0) { int sx, sy; float zz; if (projectPt(RP(x, top + 0.3f, z), sx, sy, zz) && sx > -IW && sx < 2 * IW && sy > -IH && sy < 2 * IH) { int r = fs * 4; fillCircle(sx, sy, r, RGB(20, 20, 30), 150); fillRect(sx - r + fs, sy + r - fs * 2, (int)((2 * r - 2 * fs) * clampf(1 - u.deployT, 0, 1)), fs, teamCol(u.team)); } } }
}
#undef PART

// =====================================================================
//  БИТВА БАШЕН: интерфейс боя, меню, колода, сундуки, магазин, задания
// =====================================================================
enum { GS_MENU, GS_BATTLE, GS_RESULT, GS_CHEST, GS_LOBBY, GS_START };
static int gs = GS_MENU, menuTab = 2;
static char msg[160] = ""; static float msgT = 0;
static void say(const char *s, float t = 2.5f) { snprintf(msg, sizeof msg, "%s", s); msgT = t; }
static void drawFit(int x, int y, const char *s, int maxW, int sc, u32 col, int align) { while (sc > 1 && textW(s, sc) > maxW) sc--; drawTextSh(x, y, s, sc, col, align); }
static int colX = 0, colW = 480;
static void layoutCol() { colW = imin(IW, (int)(IH * 0.62f)); colX = (IW - colW) / 2; }
// звук (b6)
static void sfx(int kind, float vol = 1.0f);
enum { SFX_CLICK, SFX_PLAY, SFX_HIT, SFX_SHOOT, SFX_BOOM, SFX_TOWER, SFX_WIN, SFX_LOSE, SFX_CHEST, SFX_CARD, SFX_ZAP, SFX_DENY };

// ---- миниатюры карт: модель рисуется один раз в маленькую картинку ----
static const int TW = 96, TH = 96; static const u32 KEYC = 0xFF00FF;
static u32 thumbs[NCARD][TW * TH]; static bool thumbsOk = false;
static void thumbRender(int c, float zoom, float shx, float shy) {
  const Card &cd = CARDS[c]; const UnitT &ut = UNITS[cd.unit]; float top = modelTop(ut.model), dist = 1.0f + top * 1.6f + (cd.count > 1 ? 0.6f : 0);
  for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) fb[y * IW + x] = KEYC; clearDepth();
  float cy = top * 0.5f; setCamera(v3(dist * 0.42f, cy + dist * 0.45f, dist), v3(0, cy, 0)); focal = TW * 0.9f * zoom; vcx = TW * 0.5f + shx; vcy = TH * 0.5f + shy;
  gLight = 1.08f; gNight = 0; fogA = 1e9f; fogB = 2e9f; LDIR = vnorm(v3(0.45f, 0.8f, 0.7f));
  if (cd.count >= 10) { for (int k = 5; k >= 0; k--) { int row = k / 3; float ox = ((k % 3) - 1 + row * 0.5f - 0.25f) * 0.45f * top, oz = -row * 0.55f * top;   // армия: два ряда
      drawModel(ut.model, 0, ox, 0, oz, 0.35f, 0.9f, 0, 0, false, 1); } return; }
  int n = imin(cd.count, 3); for (int k = 0; k < imax(1, n); k++) { float ox = n > 1 ? (k - (n - 1) * 0.5f) * 0.5f * top : 0, oz = n > 1 ? -fabsf(ox) * 0.7f : 0;
    drawModel(ut.model, 0, ox, 0, oz, 0.35f, 0.9f, 0, 0, false, 1); }
}
static void genThumbs() {
  if (IW < TW || IH < TH) return;
  bool gl = gGL, ov = gOverlay; gGL = false; gOverlay = false; int vt = viewTeam; viewTeam = 0;
  setClipRect(0, 0, TW, TH);
  for (int c = 0; c < NCARD; c++) { const Card &cd = CARDS[c]; for (int i = 0; i < TW * TH; i++) thumbs[c][i] = KEYC;
    if (cd.type == CT_SPELL) continue;
    // первый проход - узнаём размер картинки, второй - рисуем крупно и по центру
    thumbRender(c, 1.0f, 0, 0);
    int x0 = TW, y0 = TH, x1 = -1, y1 = -1; for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) if ((fb[y * IW + x] & 0xFFFFFF) != KEYC) { x0 = imin(x0, x); y0 = imin(y0, y); x1 = imax(x1, x); y1 = imax(y1, y); }
    if (x1 > x0 && y1 > y0) { float w = (float)(x1 - x0 + 1), h = (float)(y1 - y0 + 1), z = fminv(TW * 0.86f / w, TH * 0.86f / h); z = clampf(z, 0.5f, 4.0f);
      float cx = (x0 + x1) * 0.5f - TW * 0.5f, cy = (y0 + y1) * 0.5f - TH * 0.5f; thumbRender(c, z, -cx * z, -cy * z); }
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) thumbs[c][y * TW + x] = fb[y * IW + x] & 0xFFFFFF;
    // тёмная обводка: силуэт виден на любом фоне
    static u32 tmp[TW * TH]; memcpy(tmp, thumbs[c], sizeof tmp);
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) { if (tmp[y * TW + x] != KEYC) continue; bool edge = false;
      for (int dy = -2; dy <= 2 && !edge; dy++) for (int dx = -2; dx <= 2 && !edge; dx++) { int xx = x + dx, yy = y + dy; if (xx >= 0 && yy >= 0 && xx < TW && yy < TH && tmp[yy * TW + xx] != KEYC && dx * dx + dy * dy <= 5) edge = true; }
      if (edge) thumbs[c][y * TW + x] = RGB(18, 16, 24); } }
  setClipRect(0, 0, IW, IH); gGL = gl; gOverlay = ov; viewTeam = vt; thumbsOk = true;
  memset(fb, 0, sizeof(u32) * IW * IH);
}
static void blitThumb(int c, int x, int y, int w, int h) {
  for (int j = 0; j < h; j++) { int yy = y + j; if (yy < 0 || yy >= IH) continue; int sy = j * TH / h;
    for (int i = 0; i < w; i++) { int xx = x + i; if (xx < 0 || xx >= IW) continue; u32 p = thumbs[c][sy * TW + i * TW / w]; if (p != KEYC) putpx(xx, yy, gOverlay ? (p | 0xFF000000u) : p); } }
}
static void spellIcon(int c, int x, int y, int w, int h) {
  int cx = x + w / 2, cy = y + h / 2, r = imin(w, h) / 3;
  switch (CARDS[c].spell) {
    case SP_FIREBALL: fillCircle(cx, cy, r, RGB(255, 120, 30), 255); fillCircle(cx - r / 4, cy - r / 4, r / 2, RGB(255, 220, 120), 255); for (int k = 0; k < 3; k++) fillCircle(cx + r + k * r / 3, cy + r + k * r / 3, r / 3 - k, RGB(255, 150, 40), 200); break;
    case SP_ARROWS: for (int k = 0; k < 5; k++) { int ax = x + w / 6 + k * w / 7, ay = y + h / 6 + (k & 1) * h / 8; drawLine(ax, ay, ax + w / 6, ay + h / 2, RGB(140, 100, 60), imax(1, fs)); fillRect(ax + w / 6 - fs, ay + h / 2 - fs, fs * 2 + 1, fs * 2 + 1, RGB(220, 220, 230)); } break;
    case SP_FREEZE: fillCircle(cx, cy, r, RGB(150, 210, 250), 255); for (int k = 0; k < 3; k++) { float a = k * 1.047f; drawLine(cx - (int)(cosf(a) * r * 1.2f), cy - (int)(sinf(a) * r * 1.2f), cx + (int)(cosf(a) * r * 1.2f), cy + (int)(sinf(a) * r * 1.2f), RGB(255, 255, 255), imax(1, fs)); } break;
    case SP_HEAL: fillCircle(cx, cy, r, RGB(255, 230, 120), 255); fillRect(cx - r / 4, cy - r * 2 / 3, r / 2, r * 4 / 3, RGB(255, 255, 255)); fillRect(cx - r * 2 / 3, cy - r / 4, r * 4 / 3, r / 2, RGB(255, 255, 255)); break;
    case SP_ROCKET: { for (int k = 0; k < 4; k++) fillCircle(cx - r / 2 - k * r / 3, cy + r / 2 + k * r / 3, r / 3 + k * r / 8, RGB(200, 195, 190), 200 - k * 40);   // дым
      fillCircle(cx - r / 3, cy + r / 3, r / 3, RGB(255, 160, 40), 255);
      for (int k = -r; k <= r; k++) { int px = cx + k * 7 / 10, py = cy - k * 7 / 10; fillRect(px - r / 4, py - r / 4, r / 2, r / 2, k > r * 2 / 3 ? RGB(220, 50, 40) : (abs(k) < r / 6 ? RGB(60, 120, 230) : RGB(230, 230, 235))); }
      fillRect(cx - r * 7 / 10 - r / 2, cy + r * 7 / 10 - r / 8, r / 2, r / 4, RGB(220, 50, 40)); fillRect(cx - r * 7 / 10 - r / 8, cy + r * 7 / 10, r / 4, r / 2, RGB(220, 50, 40)); } break;
    case SP_BARREL: { int bw = r * 3 / 2, bh = r * 2; fillRect(cx - bw / 2, cy - bh / 2 + r / 3, bw, bh, RGB(140, 95, 55)); for (int k = 0; k < 4; k++) fillRect(cx - bw / 2 + k * bw / 4, cy - bh / 2 + r / 3, imax(1, fs / 2), bh, RGB(110, 70, 40));
      fillRect(cx - bw / 2 - fs, cy - bh / 2 + r / 3 + bh / 6, bw + 2 * fs, imax(2, fs), RGB(130, 130, 140)); fillRect(cx - bw / 2 - fs, cy + bh / 2 + r / 3 - bh / 6, bw + 2 * fs, imax(2, fs), RGB(130, 130, 140));
      fillCircle(cx, cy - bh / 2 + r / 3, r * 2 / 3, RGB(110, 180, 80), 255); fillRect(cx - r, cy - bh / 2 + r / 6, r * 2, r / 4, RGB(110, 180, 80));   // голова и уши гоблина
      fillCircle(cx - r / 4, cy - bh / 2 + r / 4, imax(1, r / 6), RGB(255, 255, 255), 255); fillCircle(cx + r / 4, cy - bh / 2 + r / 4, imax(1, r / 6), RGB(255, 255, 255), 255);
      fillCircle(cx - r / 4, cy - bh / 2 + r / 4, imax(1, r / 12), RGB(20, 20, 20), 255); fillCircle(cx + r / 4, cy - bh / 2 + r / 4, imax(1, r / 12), RGB(20, 20, 20), 255); } break;
    case SP_ZAP: { int px = cx - r / 2, py = y + h / 8; for (int k = 0; k < 4; k++) { int nx = cx + ((k & 1) ? r / 2 : -r / 2), ny = py + h / 5; drawLine(px, py, nx, ny, RGB(255, 240, 120), imax(2, fs + 1)); px = nx; py = ny; } fillCircle(px, py, r / 3, RGB(200, 240, 255), 255); } break;
  }
}
static void elixirDrop(int x, int y, int r, int cost, bool ok) {
  fillCircle(x, y, r, RGB(30, 10, 40), 255); fillCircle(x, y, r - imax(1, fs / 2), ok ? RGB(210, 60, 220) : RGB(110, 80, 120), 255); fillCircle(x - r / 3, y - r / 3, r / 3, RGB(255, 180, 255), ok ? 180 : 60);
  char b[8]; snprintf(b, sizeof b, "%d", cost); drawTextSh(x, y - fs * 3, b, fs, RGB(255, 255, 255), 1);
}
// скруглённые углы карты: перед рисованием запоминаем фон в углах, после - возвращаем его за дугой и рисуем тёмную обводку и рамку по дуге
static u32 gCardSave[4][32 * 32];
static void cardSaveCorners(int ox, int oy, int ow, int oh, int r) {
  for (int k = 0; k < 4; k++) { int cx0 = ox + ((k & 1) ? ow - r : 0), cy0 = oy + ((k & 2) ? oh - r : 0);
    for (int j = 0; j < r; j++) for (int i = 0; i < r; i++) { int px = cx0 + i, py = cy0 + j; gCardSave[k][j * r + i] = (px >= 0 && px < IW && py >= 0 && py < IH) ? fb[py * IW + px] : 0; } }
}
static void cardRoundCorners(int ox, int oy, int ow, int oh, int r, int fw, u32 frame) {
  u32 ovl = gOverlay ? 0xFF000000u : 0, dark = RGB(15, 15, 20) | ovl, fr = frame | ovl; float rr = (float)r, ri = (float)(r - 1 - fw);
  for (int k = 0; k < 4; k++) { int cx0 = ox + ((k & 1) ? ow - r : 0), cy0 = oy + ((k & 2) ? oh - r : 0); float ccx = (k & 1) ? 0.0f : rr, ccy = (k & 2) ? 0.0f : rr;
    for (int j = 0; j < r; j++) for (int i = 0; i < r; i++) { int px = cx0 + i, py = cy0 + j; if (px < 0 || px >= IW || py < 0 || py >= IH) continue;
      float dx = i + 0.5f - ccx, dy = j + 0.5f - ccy, d2 = dx * dx + dy * dy;
      if (d2 > rr * rr) fb[py * IW + px] = gCardSave[k][j * r + i];
      else if (d2 > (rr - 1.0f) * (rr - 1.0f)) fb[py * IW + px] = dark;
      else if (ri > 0 && d2 > ri * ri) fb[py * IW + px] = fr; } }
}
static void drawCard(int c, int lvl, int x, int y, int w, int h, bool sel, bool afford, bool showLvl) {
  const Card &cd = CARDS[c]; u32 rc = RARC[cd.rarity];
  int cr = clampi(fs * 4, 3, imin(31, imin(w, h) / 3)); cardSaveCorners(x - 1, y - 1, w + 2, h + 2, cr);
  fillRect(x - 1, y - 1, w + 2, h + 2, RGB(15, 15, 20)); fillRect(x, y, w, h, sel ? mixc(rc, RGB(255, 255, 255), 0.5f) : rc);
  { u32 top = cd.type == CT_SPELL ? RGB(110, 80, 150) : (cd.type == CT_BUILD ? RGB(150, 120, 80) : RGB(90, 140, 200)), bot = mixc(top, RGB(10, 10, 25), 0.65f);
    int ih = h - 2 * fs; for (int j = 0; j < ih; j++) fillRect(x + fs, y + fs + j, w - 2 * fs, 1, mixc(mixc(top, rc, 0.2f), bot, j / (float)imax(1, ih))); }
  int ah = h - fs * (showLvl ? 9 : 2);
  if (cd.type == CT_SPELL) spellIcon(c, x + fs, y + fs, w - 2 * fs, ah); else if (thumbsOk) blitThumb(c, x + fs, y + fs, w - 2 * fs, ah);
  if (!afford) fillRectA(x, y, w, h, RGB(20, 20, 30), 130);
  if (showLvl) { char b[24]; snprintf(b, sizeof b, "УР. %d", lvl); fillRect(x + fs, y + h - fs * 8, w - 2 * fs, fs * 7, RGB(25, 25, 35)); int sc = fs; while (sc > 1 && textW(b, sc) > w - 4) sc--; drawText(x + w / 2, y + h - fs * 7, b, sc, RGB(255, 230, 150), 1); }
  { u32 fc = sel ? mixc(rc, RGB(255, 255, 255), 0.5f) : rc; if (!afford) fc = mixc(fc, RGB(20, 20, 30), 130.0f / 255.0f); cardRoundCorners(x - 1, y - 1, w + 2, h + 2, cr, fs, fc); }   // скруглённые углы
  elixirDrop(x + fs * 4, y + fs * 4, fs * 4, cd.cost, afford);
}
// ---- время ----
static void fmtTime(char *b, int n, long long sec) { if (sec < 0) sec = 0; if (sec >= 3600) snprintf(b, n, "%lldЧ %02lldМ", sec / 3600, (sec / 60) % 60); else if (sec >= 60) snprintf(b, n, "%lldМ %02lldС", sec / 60, sec % 60); else snprintf(b, n, "%lldС", sec); }
static void fmtNum(char *b, int n, long long v) { char t[32]; snprintf(t, sizeof t, "%lld", v < 0 ? -v : v); int l = (int)strlen(t), o = 0; if (v < 0 && o < n - 1) b[o++] = '-'; for (int i = 0; i < l && o < n - 2; i++) { b[o++] = t[i]; if ((l - i - 1) % 3 == 0 && i < l - 1) b[o++] = ' '; } b[o] = 0; }

// =====================================================================
//  БОЙ
// =====================================================================
static const char *BOTN[] = {"SIR THUNDER", "DARK LORD", "WOLF FANG", "LADY SPARK", "BARON STONE", "COUNT MIST", "RED WOLF", "SHIELD MASTER", "KING WALNUT", "MOON WITCH"};
struct BattleInfo { int mode; bool net, vsBot, duo; char enemy[48]; int myTeam, mySide, mateSide; int trophyDelta; int chest; bool done; int stars; int goldWon; };
static Bot bots[3]; static int nBots = 0;                 // боты в бою (в 2 на 2 их до трёх)
static BattleInfo BI;
static int selCard = -1; static bool dragging = false; static float ghostX = 0, ghostZ = 0; static bool ghostOn = false;
// отступ сверху под строку состояния телефона (часы, батарея), иначе таймер прячется под ней
static int safeTop() { return imax(fs * 6, IH * 45 / 1000); }
static int handY0 = 0, cardW = 0, cardH = 0, handX0 = 0, arenaY1 = 0, topH = 0;
static float simAlpha = 0; static bool previewDirty = true;
static void battleLayout() { layoutCol(); topH = safeTop() + fs * 20; int bh = (int)(IH * 0.21f); arenaY1 = IH - bh; cardW = (colW - fs * 10) * 10 / 46; cardH = cardW * 6 / 5; handX0 = colX + fs * 2 + (colW - fs * 4) - 4 * cardW - 3 * fs * 2; handY0 = arenaY1 + fs * 3; }
static int netSendPlay(int slot, float x, float z);         // в b4: клиент отправляет ход хосту
static bool netIsClient();
static void netSendQuit();
static void tryPlay(int slot, float x, float z) {
  int team = BI.myTeam; Side &s = B.side[BI.mySide]; int card = s.deck[s.hand[slot]];
  if (!canPlace(team, card, x, z)) { say("СЮДА НЕЛЬЗЯ"); sfx(SFX_DENY); return; }
  if (s.elixir + 1e-4f < CARDS[card].cost) { say("НЕ ХВАТАЕТ ЭЛИКСИРА"); sfx(SFX_DENY); return; }
  if (netIsClient()) { netSendPlay(slot, x, z); selCard = -1; return; }
  if (playCard(BI.mySide, slot, x, z)) { selCard = -1; sfx(SFX_PLAY); }
}
static void makeBotDeck(int arena, int *deck, int *lvl, int avgLvl) {
  int av[NCARD], n = cardsUnlocked(arena, av);
  for (int tries = 0; tries < 200; tries++) {
    for (int i = n - 1; i > 0; i--) { int j = rand() % (i + 1); int t = av[i]; av[i] = av[j]; av[j] = t; }
    int spells = 0, air = 0, win = 0; for (int i = 0; i < 8; i++) { const Card &c = CARDS[av[i]]; if (c.type == CT_SPELL) spells++; else { const UnitT &u = UNITS[c.unit]; if (u.canAir) air++; if (u.onlyBuild || u.hp * imax(1, c.count) > 1300) win++; } }
    if (spells >= 1 && spells <= 3 && air >= 2 && win >= 1) break; }
  for (int i = 0; i < 8; i++) { deck[i] = av[i]; lvl[i] = clampi(avgLvl + (rand() % 3) - 1, 1, MAXLVL); }
}
// ---- РЕЙТИНГ ТОП-100: у каждого бота английское имя и свои кубки; место игрока считается по кубкам ----
static const char *TOP100[100] = {
  "GOLDEN RAVEN", "STORM HUNTER", "ROYAL LION", "DARK TITAN", "WILD GHOST",
  "DARK KNIGHT", "STORM KNIGHT", "ROYAL RAVEN", "DARK DRAGON", "SHADOW KNIGHT",
  "STORM DRAGON", "SHADOW WOLF", "SILENT KNIGHT", "WILD RAVEN", "STORM TITAN",
  "FROST LION", "STORM PHOENIX", "FROST TITAN", "WILD PHOENIX", "WILD LION",
  "FROST VIPER", "SHADOW DRAGON", "BLOOD KNIGHT", "GOLDEN GHOST", "IRON TITAN",
  "FROST GHOST", "IRON WOLF", "GOLDEN DRAGON", "IRON PHOENIX", "ROYAL TITAN",
  "SILENT HUNTER", "SILENT LION", "FROST DRAGON", "BLOOD LION", "DARK GHOST",
  "ROYAL GHOST", "SILENT PHOENIX", "ROYAL KNIGHT", "BLOOD VIPER", "STORM LION",
  "STORM WOLF", "DARK VIPER", "GOLDEN TITAN", "SILENT WOLF", "GOLDEN KNIGHT",
  "IRON RAVEN", "SILENT RAVEN", "SHADOW RAVEN", "BLOOD PHOENIX", "DARK PHOENIX",
  "FROST HUNTER", "SILENT DRAGON", "BLOOD GHOST", "STORM RAVEN", "ROYAL DRAGON",
  "FROST WOLF", "GOLDEN PHOENIX", "SILENT VIPER", "WILD KNIGHT", "DARK WOLF",
  "FROST RAVEN", "BLOOD DRAGON", "GOLDEN HUNTER", "GOLDEN LION", "WILD HUNTER",
  "IRON GHOST", "ROYAL PHOENIX", "GOLDEN VIPER", "IRON VIPER", "SHADOW HUNTER",
  "ROYAL WOLF", "ROYAL HUNTER", "ROYAL VIPER", "WILD VIPER", "STORM GHOST",
  "IRON HUNTER", "WILD DRAGON", "DARK LION", "BLOOD TITAN", "WILD WOLF",
  "DARK HUNTER", "GOLDEN WOLF", "SHADOW GHOST", "BLOOD RAVEN", "BLOOD HUNTER",
  "IRON KNIGHT", "SHADOW TITAN", "STORM VIPER", "SILENT TITAN", "SHADOW VIPER",
  "WILD TITAN", "FROST PHOENIX", "IRON DRAGON", "SILENT GHOST", "SHADOW LION",
  "SHADOW PHOENIX", "DARK RAVEN", "BLOOD WOLF", "IRON LION", "FROST KNIGHT" };
static int topRating(int rank) { return 2500 - (rank - 1) * 24; }                                      // 1 место - 2500 рейтинга, 100 место - 124
static const char *topName(int rank) { return TOP100[((rank - 1) * 37 + (int)(weekOf(nowSec()) % 100) * 13) % 100]; }   // каждую неделю боты распределяются по местам заново
static int topRankAt(int tr) { int r = 1; for (int k = 1; k <= 100; k++) if (topRating(k) > tr) r++; return r; }   // 101 и больше - игрок вне ТОП-100
static int playerTopRank() { return topRankAt(pf.rating); }
static bool inLegend() { return isLegend(arenaOf(pf.trophies)); }                                   // на легендарной арене вместо кубков - РЕЙТИНГ и место в ТОП-100
static void rankStr(char *o, size_t n, int r) { if (r > 100) snprintf(o, n, "100+"); else snprintf(o, n, "%d", r); }
// ---- ЛЕГЕНДАРНАЯ АРЕНА: здесь играют ТОП-100 ботов. Чем выше место, тем сильнее соперник ----
static int topRank = 0;                                               // место текущего соперника в ТОП-100 (1 - сильнейший)
static void topBotPick(int *kl, int *lv, int *dif) {
  topRank = clampi(imin(playerTopRank(), 100) + rand() % 21 - 10, 1, 100);   // соперник из ТОП-100 близок по месту к игроку
  *lv = clampi(MAXLVL - topRank / 35, 1, MAXLVL); *kl = clampi(MAXLVL - topRank / 50, 1, MAXLVL); *dif = 3;   // 1-34: макс. уровень, 35-69: -1, 70-100: -2
}
static void topBotName(char *out, size_t n) { snprintf(out, n, "#%d %s", topRank, topName(topRank)); }
static void topBotTune() { if (topRank <= 10) { bot.react *= 0.75f; bot.smart = 0.95f; } else if (topRank <= 30) bot.react *= 0.88f; }   // топ-10 думают быстрее и умнее
static int deckAvgLvl() { int s = 0; for (int i = 0; i < 8; i++) s += pf.lvl[pf.deck[i]]; return (s + 4) / 8; }
static void startBotBattle() {
  int a = arenaOf(pf.trophies); int dk[8], lv[8], my[8]; for (int i = 0; i < 8; i++) my[i] = pf.lvl[pf.deck[i]];
  makeBotDeck(a, dk, lv, deckAvgLvl());
  int kl = clampi(pf.kingLvl + (rand() % 3 == 0 ? (rand() & 1 ? 1 : -1) : 0), 1, MAXLVL), dif = clampi(a / 2 + (pf.trophies > 400 ? 1 : 0), 0, 3); bool top = isLegend(a);
  if (top) { int tl; topBotPick(&kl, &tl, &dif); for (int i = 0; i < 8; i++) lv[i] = tl; }   // соперник - из ТОП-100
  startBattle(pf.deck, my, pf.kingLvl, dk, lv, kl, true, a, (unsigned)rand() * 2654435761u + 1u);
  botInit(1, dif); if (top) topBotTune(); bots[0] = bot; nBots = 1;
  memset(&BI, 0, sizeof BI); BI.vsBot = true; BI.myTeam = 0; BI.mySide = 0; BI.mateSide = -1; if (top) topBotName(BI.enemy, sizeof BI.enemy); else snprintf(BI.enemy, sizeof BI.enemy, "%s", BOTN[rand() % 10]);
  viewTeam = 0; selCard = -1; dragging = false; gs = GS_BATTLE; previewDirty = true; sfx(SFX_CLICK);
}
// ---- особые режимы: тройной эликсир, случайные колоды, турнир из 5 боёв ----
enum { MODE_NORMAL = 0, MODE_TRIPLE = 3, MODE_RANDOM = 4, MODE_TOUR = 5 };
static int tourStage = 0;                                       // 0 - турнир не начат, 1..5 - какой бой следующий
static void tourLoad() { FILE *f = fopen(pth("bashni_tour.sav"), "rb"); if (f) { int v = 0; if (fread(&v, 4, 1, f) == 1) tourStage = clampi(v, 0, 5); fclose(f); } }
static void tourSave() { FILE *f = fopen(pth("bashni_tour.sav"), "wb"); if (f) { fwrite(&tourStage, 4, 1, f); fclose(f); } }
static void randomDeck(int arena, int *dk, int *lv, int lvlDefault, bool mine) {
  int av[NCARD], n = cardsUnlocked(arena, av);
  for (int i = 0; i < 8 && i < n; i++) { int j = i + rand() % (n - i); int t = av[i]; av[i] = av[j]; av[j] = t; dk[i] = av[i]; lv[i] = mine && pf.have[dk[i]] ? pf.lvl[dk[i]] : lvlDefault; }
}
static void startSpecial(int mode) {
  int a = arenaOf(pf.trophies), avg = deckAvgLvl(); int dk[8], lv[8], my[8], md[8]; for (int i = 0; i < 8; i++) { md[i] = pf.deck[i]; my[i] = pf.lvl[pf.deck[i]]; }
  int dif = clampi(a / 2 + (pf.trophies > 400 ? 1 : 0), 0, 3), kl = pf.kingLvl;
  if (mode == MODE_RANDOM) { randomDeck(a, md, my, avg, true); randomDeck(a, dk, lv, avg, false); }
  else if (mode == MODE_TOUR) { if (tourStage < 1) tourStage = 1; int st = tourStage; int bl = clampi(avg - 2 + st, 1, MAXLVL);   // с каждым боем соперник сильнее
    makeBotDeck(imin(NARENA - 1, a + (st >= 4 ? 1 : 0)), dk, lv, bl); for (int i = 0; i < 8; i++) lv[i] = bl; dif = clampi(st - 2, 0, 3); kl = clampi(pf.kingLvl - 2 + st, 1, MAXLVL); }
  else makeBotDeck(a, dk, lv, avg);
  bool top = isLegend(a) && mode != MODE_TOUR; if (top) { int tl; topBotPick(&kl, &tl, &dif); if (mode != MODE_RANDOM) for (int i = 0; i < 8; i++) lv[i] = tl; }
  startBattle(md, my, pf.kingLvl, dk, lv, kl, true, a, (unsigned)rand() * 2654435761u + 7u);
  if (mode == MODE_TRIPLE) B.elixMul = 3.0f;
  botInit(1, dif); if (top) topBotTune(); bots[0] = bot; nBots = 1;
  memset(&BI, 0, sizeof BI); BI.mode = mode; BI.vsBot = true; BI.myTeam = 0; BI.mySide = 0; BI.mateSide = -1;
  if (mode == MODE_TOUR) snprintf(BI.enemy, sizeof BI.enemy, "ТУРНИР %d/5: %s", tourStage, BOTN[rand() % 10]); else if (top) topBotName(BI.enemy, sizeof BI.enemy); else snprintf(BI.enemy, sizeof BI.enemy, "%s", BOTN[rand() % 10]);
  viewTeam = 0; selCard = -1; dragging = false; gs = GS_BATTLE; previewDirty = true; sfx(SFX_CLICK);
}
static void setupDuo(int mySide, bool allyBot, unsigned seed, int arena, const int *mateDeck, const int *mateLvl, int mateKing) {
  int decks[4][8], lvls[4][8], avg = deckAvgLvl();
  for (int i = 0; i < 8; i++) { decks[mySide][i] = pf.deck[i]; lvls[mySide][i] = pf.lvl[pf.deck[i]]; }
  int mate = mySide == 0 ? 2 : 0;
  if (allyBot) makeBotDeck(arena, decks[mate], lvls[mate], avg); else for (int i = 0; i < 8; i++) { decks[mate][i] = mateDeck[i]; lvls[mate][i] = mateLvl[i]; }
  makeBotDeck(arena, decks[1], lvls[1], avg); makeBotDeck(arena, decks[3], lvls[3], avg);
  startBattle4(decks, lvls, pf.kingLvl, imax(1, (pf.kingLvl + (allyBot ? pf.kingLvl : mateKing)) / 2), arena, seed);
  int dif = clampi(arena / 2 + (pf.trophies > 400 ? 1 : 0), 0, 3);
  nBots = 0; botInit(1, dif); bots[nBots++] = bot; botInit(3, dif); bots[nBots++] = bot; if (allyBot) { botInit(mate, dif); bots[nBots++] = bot; }
  memset(&BI, 0, sizeof BI); BI.vsBot = true; BI.duo = true; BI.myTeam = 0; BI.mySide = mySide; BI.mateSide = mate;
  int e1 = (int)(seed % 10u), e2 = (e1 + 1 + (int)((seed / 10u) % 9u)) % 10;   // одинаково у хоста и у друга snprintf(BI.enemy, sizeof BI.enemy, "%s И %s", BOTN[e1], BOTN[e2]);
  viewTeam = 0; selCard = -1; dragging = false; gs = GS_BATTLE; previewDirty = true; sfx(SFX_CLICK);
}
static void startDuoBot() { setupDuo(0, true, (unsigned)rand() * 2654435761u + 3u, arenaOf(pf.trophies), NULL, NULL, 1); }
static void addCards(int c, int n) { if (!pf.have[c]) { pf.have[c] = true; n = imax(0, n - 1); } pf.cnt[c] += n; }
static void finishBattle() {
  BI.done = true; int me = BI.myTeam, en = 1 - me; bool win = B.winner == me, lose = B.winner == en;
  if (BI.mode == MODE_TOUR) {
    if (win) { BI.goldWon = 40 * tourStage; pf.gold += BI.goldWon; questAdd(Q_WIN, 1);
      if (tourStage >= 5) { BI.chest = -1; for (int s = 0; s < NSLOT; s++) if (pf.slots[s].type < 0) { pf.slots[s].type = CH_LEGEND; pf.slots[s].opening = false; BI.chest = CH_LEGEND; break; }
        if (BI.chest < 0) { pf.gems += 100; BI.stars = 100; } pf.gold += 500; BI.goldWon += 500; BI.trophyDelta = 99; tourStage = 0; }   // турнир пройден
      else tourStage++; }
    else { BI.trophyDelta = -tourStage; tourStage = 0; }                                    // проигрыш - турнир заканчивается
    tourSave(); }
  else if (BI.mode == MODE_TRIPLE || BI.mode == MODE_RANDOM) { if (win) { BI.goldWon = 20 + 10 * arenaOf(pf.trophies); pf.gold += BI.goldWon; questAdd(Q_WIN, 1); } pf.crowns = imin(10, pf.crowns + B.side[me].crowns); }
  else if (BI.duo) { if (win) { BI.goldWon = 15 + 10 * arenaOf(pf.trophies); pf.gold += BI.goldWon; questAdd(Q_WIN, 1); } pf.crowns = imin(10, pf.crowns + B.side[me].crowns); }
  else if (!BI.net) {
    int a0 = arenaOf(pf.trophies);
    if (win) { BI.trophyDelta = 26 + rand() % 7; pf.wins++; if (B.side[me].crowns == 3) { pf.threeCrowns++; questAdd(Q_THREE, 1); } questAdd(Q_WIN, 1);
      BI.chest = -1; for (int s = 0; s < NSLOT; s++) if (pf.slots[s].type < 0) { int t = CHEST_CYCLE[pf.chestN % 16]; if (rand() % 100 == 0) t = CH_LEGEND; pf.slots[s].type = t; pf.slots[s].opening = false; pf.chestN++; BI.chest = t; break; }
      BI.goldWon = 5 + 5 * a0; pf.gold += BI.goldWon; }
    else if (lose) { BI.trophyDelta = -(20 + rand() % 6); pf.losses++; }
    if (isLegend(a0) && (win || lose)) {                                          // Легендарная арена: рейтинг +100 за победу, -200 за поражение, кубки не меняются
      int nr = imax(0, pf.rating + (win ? RATING_WIN : -RATING_LOSS)); BI.trophyDelta = nr - pf.rating; pf.rating = nr; pf.ratingBest = imax(pf.ratingBest, pf.rating); }
    else {
      pf.trophies = imax(ARENAS[a0].trophies > 0 && !win ? ARENAS[a0].trophies : 0, pf.trophies + BI.trophyDelta);
      if (lose && pf.trophies < 0) pf.trophies = 0;
      pf.bestTrophies = imax(pf.bestTrophies, pf.trophies); }
    pf.crowns = imin(10, pf.crowns + B.side[me].crowns);
  }
  questAdd(Q_PLAY, B.side[BI.mySide].cardsPlayed); questAdd(Q_TOWERS, B.side[me].towersBroken); questAdd(Q_DMG, (int)B.side[me].towerDmg); questAdd(Q_SPELLS, B.side[BI.mySide].spells);
  saveProfile(); gs = GS_RESULT; sfx(win ? SFX_WIN : SFX_LOSE);
}
static void drawBattleHud() {
  int me = BI.myTeam, en = 1 - me; Side &s = B.side[BI.mySide]; char b[96];
  // верх: противник, время, короны
  fillRectA(0, 0, IW, topH, RGB(10, 14, 24), 200);
  int st = safeTop(); drawFit(colX + fs * 3, st + fs * 3, BI.enemy, colW * 62 / 100, fs, RGB(255, 150, 140), 0);
  snprintf(b, sizeof b, "КОРОНЫ %d", B.side[en].crowns); drawTextSh(colX + fs * 3, st + fs * 10, b, fs, RGB(255, 210, 120), 0);
  int tl = (int)ceilf(fmaxv(0, timeLeft())); snprintf(b, sizeof b, "%d:%02d", tl / 60, tl % 60);
  drawTextSh(colX + colW - fs * 3, st + fs * 2, B.overtime ? "ДОП. ВРЕМЯ" : "ДО КОНЦА", fs, RGB(200, 210, 230), 2);
  drawTextSh(colX + colW - fs * 3, st + fs * 10, b, fs + 1, tl <= 10 ? RGB(255, 90, 80) : RGB(255, 255, 255), 2);
  if (doubleElixir() || B.overtime) { bool on = fmodf(gTime, 1.0f) < 0.7f; if (on) drawTextSh(IW / 2, topH + fs * 2, "ЭЛИКСИР X2", fs + 1, RGB(240, 120, 255), 1); }
  // свои короны - над картами
  snprintf(b, sizeof b, "МОИ КОРОНЫ %d", B.side[me].crowns); drawTextSh(colX + fs * 3, arenaY1 - fs * 10, b, fs, RGB(140, 200, 255), 0);
  if (BI.duo && BI.mateSide >= 0) { Side &m = B.side[BI.mateSide]; snprintf(b, sizeof b, "НАПАРНИК%s: ЭЛИКСИР %d", BI.net ? "" : " (БОТ)", (int)m.elixir); drawTextSh(colX + colW - fs * 3, arenaY1 - fs * 10, b, fs, RGB(170, 220, 255), 2);
    int bw = fs * 50, bx = colX + colW - fs * 3 - bw; fillRect(bx, arenaY1 - fs * 2, bw, fs, RGB(40, 20, 50)); fillRect(bx, arenaY1 - fs * 2, (int)(bw * m.elixir / 10), fs, RGB(210, 60, 220)); }
  // нижняя панель: следующая карта, 4 карты, эликсир
  fillRect(0, arenaY1, IW, IH - arenaY1, RGB(40, 30, 55)); fillRect(0, arenaY1, IW, fs, RGB(90, 70, 120));
  int nx = colX + fs * 2, nw = cardW * 3 / 5, nh = cardH * 3 / 5;
  drawText(nx + nw / 2, handY0, "ДАЛЕЕ", imax(1, fs - 1), RGB(200, 190, 220), 1);
  drawCard(s.deck[s.q[0]], s.lvl[s.q[0]], nx, handY0 + fs * 8, nw, nh, false, true, false);
  for (int i = 0; i < 4; i++) { int c = s.deck[s.hand[i]], x = handX0 + i * (cardW + fs * 2), y = handY0 - (selCard == i ? fs * 4 : 0);
    bool ok = s.elixir + 1e-4f >= CARDS[c].cost; drawCard(c, s.lvl[s.hand[i]], x, y, cardW, cardH, selCard == i, ok, false);
    if (!ok) { float k = clampf(s.elixir / CARDS[c].cost, 0, 1); fillRect(x + fs, y + cardH - fs * 3, (int)((cardW - fs * 2) * k), fs * 2, RGB(210, 60, 220)); } }
  // эликсир
  int ey = handY0 + cardH + fs * 3, ex = handX0, ew = 4 * cardW + 3 * fs * 2, eh = fs * 6;
  fillRect(ex - 1, ey - 1, ew + 2, eh + 2, RGB(15, 10, 20)); fillRect(ex, ey, ew, eh, RGB(50, 30, 60));
  fillRect(ex, ey, (int)(ew * s.elixir / 10.0f), eh, RGB(210, 60, 220)); for (int k = 1; k < 10; k++) fillRect(ex + ew * k / 10, ey, 1, eh, RGB(40, 20, 50));
  elixirDrop(ex - fs * 5, ey + eh / 2, fs * 5, (int)s.elixir, true);
  if (msgT > 0) { int w = textW(msg, fs) + fs * 8; fillRectA(IW / 2 - w / 2, arenaY1 - fs * 26, w, fs * 11, RGB(0, 0, 0), 170); drawText(IW / 2, arenaY1 - fs * 24, msg, fs, RGB(255, 230, 160), 1); }
  // выход (сдаться)
  if (!B.over && button(colX + colW - fs * 22, topH + fs * 2, fs * 20, fs * 10, "СДАТЬСЯ", true, RGB(110, 50, 50))) { B.over = true; B.winner = en; bEvent(BE_END, en, 0, 0, 0); if (BI.net) netSendQuit(); }
}
static int cardAt(int x, int y) { for (int i = 0; i < 4; i++) { int cx = handX0 + i * (cardW + fs * 2); if (x >= cx && x < cx + cardW && y >= handY0 - fs * 6 && y < handY0 + cardH + fs * 2) return i; } return -1; }
// пальцем: тянуть карту на арену или выбрать и нажать на арену
static void battleInput() {
  ghostOn = false;
  if (B.over) return;
  if (P.pressed) { int c = cardAt(P.x, P.y); if (c >= 0) { if (selCard == c && !dragging) { /* повторное нажатие - отмена после отпускания */ } selCard = c; dragging = true; sfx(SFX_CARD); } else dragging = false; }
  if (selCard >= 0 && (dragging ? P.down : true)) { int py = dragging ? P.y : -1, px = P.x;
    if (dragging && py < arenaY1 - fs * 2) { float wx, wz; if (screenToArena(px, py, wx, wz)) { ghostX = roundf(wx * 2) * 0.5f; ghostZ = roundf(wz * 2) * 0.5f; ghostOn = true; } } }
  if (P.clicked) { int c = cardAt(P.cx, P.cy);
    if (dragging && P.cy < arenaY1 - fs * 2 && selCard >= 0) { float wx, wz; if (screenToArena(P.cx, P.cy, wx, wz)) tryPlay(selCard, roundf(wx * 2) * 0.5f, roundf(wz * 2) * 0.5f); P.clicked = false; }
    else if (!dragging && selCard >= 0 && P.cy < arenaY1 - fs * 2 && P.cy > topH) { float wx, wz; if (screenToArena(P.cx, P.cy, wx, wz)) tryPlay(selCard, roundf(wx * 2) * 0.5f, roundf(wz * 2) * 0.5f); P.clicked = false; }
    else if (c < 0 && P.cy >= arenaY1) selCard = -1;
    dragging = false; }
  if (selCard >= 0 && !dragging) { ghostOn = false; if (P.down && P.y < arenaY1 - fs * 2 && P.y > topH) { float wx, wz; if (screenToArena(P.x, P.y, wx, wz)) { ghostX = roundf(wx * 2) * 0.5f; ghostZ = roundf(wz * 2) * 0.5f; ghostOn = true; } } }
}
static int ghostCard() { if (selCard < 0) return -1; Side &s = B.side[BI.mySide]; return s.deck[s.hand[selCard]]; }
static void frameBattle(float dt);                    // в b6 (общая логика с сетью)

static void frameResult() {
  drawBattle3D(1, false, -1, 0, 0, false); fillRectA(0, 0, IW, IH, RGB(0, 0, 0), 150);
  int me = BI.myTeam; bool win = B.winner == me, draw = B.winner < 0; char b[96];
  drawTextSh(IW / 2, IH / 6, draw ? "НИЧЬЯ" : (win ? "ПОБЕДА!" : "ПОРАЖЕНИЕ"), fs * 4, draw ? RGB(230, 230, 230) : (win ? RGB(255, 220, 80) : RGB(255, 100, 90)), 1);
  snprintf(b, sizeof b, "%d : %d", B.side[me].crowns, B.side[1 - me].crowns); drawTextSh(IW / 2, IH / 6 + fs * 36, b, fs * 3, RGB(255, 255, 255), 1);
  drawText(IW / 2, IH / 6 + fs * 62, "КОРОНЫ", fs, RGB(200, 210, 230), 1);
  int y = IH / 6 + fs * 80;
  if (BI.mode == MODE_TOUR) {
    if (BI.trophyDelta == 99) { drawTextSh(IW / 2, y, "ТУРНИР ПРОЙДЕН!", fs * 2, RGB(255, 220, 90), 1); y += fs * 22; drawTextSh(IW / 2, y, BI.chest >= 0 ? "НАГРАДА: ЛЕГЕНДАРНЫЙ СУНДУК" : "НАГРАДА: 100 КРИСТАЛЛОВ", fs, RGB(120, 230, 220), 1); y += fs * 12; }
    else if (win) { snprintf(b, sizeof b, "ТУРНИР: ПОБЕДА %d ИЗ 5. ДАЛЬШЕ БОЙ %d", tourStage - 1, tourStage); drawFit(IW / 2, y, b, colW - fs * 4, fs, RGB(255, 230, 150), 1); y += fs * 12; }
    else { snprintf(b, sizeof b, "ТУРНИР ОКОНЧЕН НА БОЮ %d", -BI.trophyDelta); drawTextSh(IW / 2, y, b, fs, RGB(255, 160, 140), 1); y += fs * 12; }
    if (win) { snprintf(b, sizeof b, "ЗОЛОТО +%d", BI.goldWon); drawTextSh(IW / 2, y, b, fs + 1, RGB(255, 220, 90), 1); } }
  else if (BI.mode == MODE_TRIPLE || BI.mode == MODE_RANDOM) { drawTextSh(IW / 2, y, BI.mode == MODE_TRIPLE ? "ТРОЙНОЙ ЭЛИКСИР - КУБКИ НЕ МЕНЯЮТСЯ" : "СЛУЧАЙНЫЕ КОЛОДЫ - КУБКИ НЕ МЕНЯЮТСЯ", fs, RGB(200, 210, 230), 1); y += fs * 14; if (win) { snprintf(b, sizeof b, "ЗОЛОТО +%d", BI.goldWon); drawTextSh(IW / 2, y, b, fs + 1, RGB(255, 220, 90), 1); } }
  else if (BI.duo) { drawTextSh(IW / 2, y, "БОЙ 2 НА 2 - КУБКИ НЕ МЕНЯЮТСЯ", fs, RGB(200, 210, 230), 1); y += fs * 14; if (win) { snprintf(b, sizeof b, "ЗОЛОТО +%d", BI.goldWon); drawTextSh(IW / 2, y, b, fs + 1, RGB(255, 220, 90), 1); } }
  else if (!BI.net) { if (inLegend()) { char r0[16], r1[16]; rankStr(r0, sizeof r0, topRankAt(pf.rating - BI.trophyDelta)); rankStr(r1, sizeof r1, playerTopRank()); snprintf(b, sizeof b, "РЕЙТИНГ %s%d   ВСЕГО %d   МЕСТО #%s > #%s", BI.trophyDelta >= 0 ? "+" : "", BI.trophyDelta, pf.rating, r0, r1); } else snprintf(b, sizeof b, "КУБКИ %s%d   ВСЕГО %d", BI.trophyDelta >= 0 ? "+" : "", BI.trophyDelta, pf.trophies); drawTextSh(IW / 2, y, b, fs + 1, BI.trophyDelta >= 0 ? RGB(255, 220, 120) : RGB(255, 140, 120), 1); y += fs * 16;
    if (win) { if (BI.chest >= 0) { snprintf(b, sizeof b, "НАГРАДА: %s", CHESTS[BI.chest].name); drawTextSh(IW / 2, y, b, fs, CHESTS[BI.chest].col, 1); } else drawTextSh(IW / 2, y, "НЕТ МЕСТА ДЛЯ СУНДУКА", fs, RGB(255, 160, 140), 1); y += fs * 12;
      snprintf(b, sizeof b, "ЗОЛОТО +%d", BI.goldWon); drawTextSh(IW / 2, y, b, fs, RGB(255, 220, 90), 1); y += fs * 12; }
    int na = arenaOf(pf.trophies); if (na > pf.arenaSeen) { snprintf(b, sizeof b, "НОВАЯ АРЕНА: %s!", ARENAS[na].name); drawTextSh(IW / 2, y, b, fs, RGB(140, 255, 160), 1); y += fs * 12; } }
  else drawTextSh(IW / 2, y, "ДРУЖЕСКИЙ БОЙ - КУБКИ НЕ МЕНЯЮТСЯ", fs, RGB(200, 210, 230), 1);
  if (button(IW / 2 - fs * 40, IH * 3 / 4, fs * 80, fs * 18, "ОК", true, RGB(60, 150, 70))) { int na = arenaOf(pf.trophies); if (na > pf.arenaSeen) { pf.arenaSeen = na; saveProfile(); say("ОТКРЫТЫ НОВЫЕ КАРТЫ! СМОТРИ В КОЛОДЕ", 4); } gs = GS_MENU; menuTab = 2; sfx(SFX_CLICK); }
}

// =====================================================================
//  СУНДУКИ
// =====================================================================
struct Reward { int card, n; bool fresh; };
static std::vector<Reward> chestR; static int chestGold = 0, chestGems = 0, chestType = 0, chestShown = 0; static float chestAnim = 0;
static int pickCard(int arena, int rarity) { int av[NCARD], n = cardsUnlocked(arena, av), m = 0, ok[NCARD]; for (int i = 0; i < n; i++) if (CARDS[av[i]].rarity == rarity) ok[m++] = av[i]; if (!m) return -1; return ok[rand() % m]; }
static void openChest(int type) {
  const ChestT &ct = CHESTS[type]; int a = arenaOf(pf.trophies); float mul = 1.0f + 0.35f * a;
  chestR.clear(); chestType = type; chestShown = 0; chestAnim = 0;
  chestGold = (int)(ct.gold * mul * (0.85f + 0.3f * rand() / (float)RAND_MAX)); chestGems = ct.gems ? ct.gems + rand() % (ct.gems + 1) : 0;
  int total = (int)(ct.cards * mul); int leg = (rand() % 100) < ct.legendPct ? 1 : 0, epic = ct.minEpic + ((rand() % 100) < 15 ? 1 : 0), rare = ct.minRare + total / 10;
  if (type == CH_LEGEND) { leg = 1; total = 1; rare = epic = 0; }
  int common = imax(0, total - leg - epic - rare);
  int cnt[NRAR] = {common, rare, epic, leg};
  for (int r = NRAR - 1; r >= 0; r--) { int left = cnt[r]; int stacks = imax(1, imin(left, r == RA_COMMON ? 3 : 2)); if (left <= 0) continue;
    for (int s = 0; s < stacks && left > 0; s++) { int c = pickCard(a, r); if (c < 0) { c = pickCard(a, RA_COMMON); if (c < 0) break; } int n = s == stacks - 1 ? left : imax(1, left / (stacks - s)); left -= n;
      bool merged = false; for (size_t k = 0; k < chestR.size(); k++) if (chestR[k].card == c) { chestR[k].n += n; merged = true; } if (!merged) { Reward rw; rw.card = c; rw.n = n; rw.fresh = !pf.have[c]; chestR.push_back(rw); } } }
  for (size_t k = 0; k < chestR.size(); k++) addCards(chestR[k].card, chestR[k].n);
  pf.gold += chestGold; pf.gems += chestGems; questAdd(Q_CHEST, 1); saveProfile();
  gs = GS_CHEST; sfx(SFX_CHEST);
}
static void frameChest(float dt) {
  chestAnim += dt; fillRect(0, 0, IW, IH, RGB(25, 20, 45)); char b[96];
  for (int k = 0; k < 14; k++) { float a = k * 0.4488f + gTime * 0.2f; drawLine(IW / 2, IH / 3, IW / 2 + (int)(cosf(a) * IW), IH / 3 + (int)(sinf(a) * IW), mixc(RGB(25, 20, 45), CHESTS[chestType].col, 0.25f), fs * 3); }
  drawTextSh(IW / 2, safeTop() + fs * 10, CHESTS[chestType].name, fs + 1, CHESTS[chestType].col, 1);
  int n = (int)chestR.size();
  if (chestShown < n) { const Reward &r = chestR[chestShown]; int w = colW / 2, h = w * 6 / 5, x = IW / 2 - w / 2, y = IH / 3 - h / 3; float k = clampf(chestAnim * 3, 0, 1);
    int ww = (int)(w * (0.3f + 0.7f * k)), hh = (int)(h * (0.3f + 0.7f * k));
    drawCard(r.card, pf.lvl[r.card], IW / 2 - ww / 2, y + (h - hh) / 2, ww, hh, false, true, true);
    snprintf(b, sizeof b, "X%d", r.n); drawTextSh(IW / 2, y + h + fs * 6, b, fs * 3, RGB(255, 255, 255), 1);
    drawTextSh(IW / 2, y + h + fs * 32, CARDS[r.card].name, fs + 1, RARC[CARDS[r.card].rarity], 1);
    drawText(IW / 2, y + h + fs * 46, RARN[CARDS[r.card].rarity], fs, RGB(200, 200, 220), 1);
    if (r.fresh) { bool on = fmodf(gTime, 0.6f) < 0.4f; if (on) drawTextSh(IW / 2, y - fs * 14, "НОВАЯ КАРТА!", fs + 1, RGB(140, 255, 160), 1); }
    snprintf(b, sizeof b, "ОСТАЛОСЬ: %d", n - chestShown - 1); drawText(IW / 2, IH - fs * 40, b, fs, RGB(200, 200, 220), 1);
    drawText(IW / 2, IH - fs * 28, "НАЖМИ, ЧТОБЫ ДАЛЬШЕ", fs, RGB(255, 230, 150), 1);
    if (P.clicked) { P.clicked = false; chestShown++; chestAnim = 0; sfx(SFX_CARD); } }
  else { int y = IH / 5; drawTextSh(IW / 2, y, "ПОЛУЧЕНО", fs * 2, RGB(255, 255, 255), 1); y += fs * 24;
    if (chestGold) { snprintf(b, sizeof b, "ЗОЛОТО +%d", chestGold); drawTextSh(IW / 2, y, b, fs + 1, RGB(255, 220, 90), 1); y += fs * 14; }
    if (chestGems) { snprintf(b, sizeof b, "КРИСТАЛЛЫ +%d", chestGems); drawTextSh(IW / 2, y, b, fs + 1, RGB(120, 230, 140), 1); y += fs * 14; }
    int cw = colW / 4 - fs * 3, ch = cw * 6 / 5;
    for (int i = 0; i < n; i++) { int x = colX + fs * 2 + (i % 4) * (cw + fs * 3), yy = y + (i / 4) * (ch + fs * 12); drawCard(chestR[i].card, pf.lvl[chestR[i].card], x, yy, cw, ch, false, true, false); snprintf(b, sizeof b, "X%d", chestR[i].n); drawTextSh(x + cw / 2, yy + ch + fs, b, fs, RGB(255, 255, 255), 1); }
    if (button(IW / 2 - fs * 40, IH - fs * 34, fs * 80, fs * 18, "ЗАБРАТЬ", true, RGB(60, 150, 70))) { gs = GS_MENU; sfx(SFX_CLICK); } }
}

// =====================================================================
//  МЕНЮ
// =====================================================================
static float deckScroll = 0; static int infoCard = -1, swapCard = -1;
static int tabH() { return fs * 20; }
static void topBar() {
  char b[64], m[32]; int st = safeTop(); fillRect(0, 0, IW, st + fs * 16, devMode ? RGB(70, 30, 80) : RGB(20, 26, 44));   // в режиме разработчика полоса фиолетовая
  { int pw = colW / 3 - fs * 6, ph = fs * 10, py = st + fs * 3, r = ph / 2 + fs;                          // таблетки ресурсов, как в больших играх
    for (int k = 0; k < 3; k++) { int px = colX + fs * 6 + k * (colW / 3); fillRect(px, py, pw, ph, RGB(10, 12, 24)); fillRect(px, py, pw, imax(1, fs / 2), RGB(60, 66, 90)); }
    int px0 = colX + fs * 6; fillCircle(px0, py + ph / 2, r, RGB(70, 110, 220), 255); fillCircle(px0, py + ph / 2, r - fs, RGB(110, 160, 250), 255); snprintf(m, sizeof m, "%d", pf.kingLvl); drawText(px0, py + ph / 2 - fs * 3, m, fs, RGB(255, 255, 255), 1);
    { float xp = pf.kingLvl < MAXLVL ? clampf(pf.kingXp / (float)imax(1, KING_XP[pf.kingLvl]), 0, 1) : 1; fillRect(px0 + r, py + ph - fs * 2, (int)((pw - r) * xp), fs * 2, RGB(90, 200, 240)); }
    snprintf(m, sizeof m, "%d", pf.trophies); drawText(px0 + r + (pw - r) / 2, py + fs * 2, m, fs + 1, RGB(255, 210, 120), 1);
    int px1 = colX + fs * 6 + colW / 3; fillCircle(px1, py + ph / 2, r, RGB(200, 150, 30), 255); fillCircle(px1, py + ph / 2, r - fs, RGB(255, 210, 60), 255); fmtNum(m, sizeof m, pf.gold); drawText(px1 + r + (pw - r) / 2, py + fs * 2, m, fs + 1, RGB(255, 255, 255), 1);
    int px2 = colX + fs * 6 + colW * 2 / 3; fillCircle(px2, py + ph / 2, r, RGB(30, 130, 60), 255); fillCircle(px2, py + ph / 2, r - fs, RGB(90, 220, 110), 255); fillRect(px2 - fs, py + ph / 2 - fs * 2, fs * 2, fs * 2, RGB(200, 255, 210)); snprintf(m, sizeof m, "%d", pf.gems); drawText(px2 + r + (pw - r) / 2, py + fs * 2, m, fs + 1, RGB(255, 255, 255), 1);
    return; }
  fmtNum(m, sizeof m, pf.gold); snprintf(b, sizeof b, "ЗОЛОТО %s", m); drawTextSh(colX + fs * 3, st + fs * 3, b, fs, RGB(255, 215, 80), 0);
  snprintf(b, sizeof b, "КРИСТАЛЛЫ %d", pf.gems); drawTextSh(colX + fs * 3, st + fs * 10, b, fs, RGB(120, 230, 140), 0);
  if (inLegend()) { char rk[16]; rankStr(rk, sizeof rk, playerTopRank()); snprintf(b, sizeof b, "РЕЙТИНГ %d  #%s", pf.rating, rk); drawTextSh(colX + colW - fs * 3, st + fs * 3, b, fs, RGB(255, 225, 120), 2); }
  else { snprintf(b, sizeof b, "КУБКИ %d", pf.trophies); drawTextSh(colX + colW - fs * 3, st + fs * 3, b, fs, RGB(255, 200, 120), 2); }
  snprintf(b, sizeof b, "КОРОЛЬ УР. %d", pf.kingLvl); drawTextSh(colX + colW - fs * 3, st + fs * 10, b, fs, RGB(160, 200, 255), 2);
}
// ---- экран при входе: обычная игра или режим разработчика ----
static void frameStart() {
  layoutCol(); fillRect(0, 0, IW, IH, RGB(30, 38, 62));
  for (int y = 0; y < IH; y += fs * 4) fillRect(0, y, IW, fs * 4, ((y / (fs * 4)) & 1) ? RGB(36, 45, 70) : RGB(34, 42, 66));
  int y = safeTop() + IH / 8; drawTextSh(IW / 2, y, "БИТВА", fs * 4, RGB(255, 220, 90), 1); drawTextSh(IW / 2, y + fs * 34, "БАШЕН", fs * 4, RGB(255, 120, 90), 1);
  y = IH * 45 / 100; int bw = colW * 3 / 4, bh = fs * 24; char b[96];
  snprintf(b, sizeof b, "КУБКИ %d   КОРОЛЬ УР. %d", realPf.trophies, realPf.kingLvl);
  if (button(IW / 2 - bw / 2, y, bw, bh, "ИГРАТЬ", true, RGB(60, 150, 70))) { exitDev(); previewDirty = true; gs = GS_MENU; menuTab = 2; sfx(SFX_CLICK); return; }
  drawText(IW / 2, y + bh + fs * 3, b, fs, RGB(200, 210, 230), 1); y += bh + fs * 16;
  if (button(IW / 2 - bw / 2, y, bw, bh, "РЕЖИМ РАЗРАБОТЧИКА", true, RGB(150, 70, 170))) { enterDev(); previewDirty = true; gs = GS_MENU; menuTab = 2; sfx(SFX_CLICK); say("РЕЖИМ РАЗРАБОТЧИКА: ВСЕ КАРТЫ МАКСИМАЛЬНОГО УРОВНЯ", 4); return; }
  y += bh + fs * 4;
  const char *ln[4] = {"ВСЕ КАРТЫ ОТКРЫТЫ", "И ПРОКАЧАНЫ ДО МАКСИМУМА.", "ТВОИ НАСТОЯЩИЕ КУБКИ", "И КАРТЫ НЕ МЕНЯЮТСЯ."};
  for (int i = 0; i < 4; i++) { drawText(IW / 2, y, ln[i], fs, RGB(220, 190, 240), 1); y += fs * 10; }
  drawText(IW / 2, IH - fs * 14, "КНОПКА НАЗАД В МЕНЮ - ВЕРНУТЬСЯ СЮДА", imax(1, fs - 1), RGB(160, 170, 190), 1);
}
static void tabsBar() {
  static const char *TN[4] = {"МАГАЗИН", "КОЛОДА", "БОЙ", "ЗАДАНИЯ"};
  int h = tabH(), y = IH - h, w = colW / 4; fillRect(0, y - fs * 3, IW, h + fs * 3, RGB(18, 22, 36)); fillRect(0, y - fs * 3, IW, imax(1, fs / 2), RGB(60, 70, 110));
  for (int i = 0; i < 4; i++) { int x = colX + i * w; bool on = menuTab == i; bool dot = false;
    if (i == 3) for (int q = 0; q < 3; q++) if (pf.quests[q].done && !pf.quests[q].claimed) dot = true;
    if (i == 1) for (int c = 0; c < NCARD; c++) if (pf.have[c] && pf.lvl[c] < MAXLVL && pf.cnt[c] >= needCards(CARDS[c].rarity, pf.lvl[c] + 1) && pf.gold >= needGold(CARDS[c].rarity, pf.lvl[c] + 1)) dot = true;
    int bx = x + fs, by = y + fs - (on ? fs * 3 : 0), bw = w - fs * 2, bh = h - fs * 2 + (on ? fs * 3 : 0);       // выбранная вкладка приподнята
    fillRect(bx, by, bw, bh, on ? RGB(70, 110, 200) : RGB(40, 46, 70)); fillRect(bx, by, bw, imax(1, fs), on ? RGB(120, 160, 240) : RGB(60, 68, 96));
    int ix = bx + bw / 2, iy = by + bh * 38 / 100, r = imin(bw, bh) / 5;
    if (i == 0) { fillRect(ix - r, iy - r / 2, r * 2, r * 3 / 2, RGB(150, 100, 50)); fillRect(ix - r, iy - r / 2, r * 2, r / 2, RGB(180, 125, 60)); fillCircle(ix + r, iy - r / 2, r / 2, RGB(255, 210, 60), 255); }
    else if (i == 1) { fillRect(ix - r, iy - r, r * 3 / 2, r * 2, RGB(60, 90, 160)); fillRect(ix - r / 2, iy - r * 3 / 4, r * 3 / 2, r * 2, RGB(240, 240, 250)); fillRect(ix - r / 3, iy - r / 2, r * 7 / 6, r, RGB(110, 160, 230)); }
    else if (i == 2) { drawLine(ix - r, iy - r, ix + r, iy + r, RGB(225, 230, 240), imax(2, fs + 1)); drawLine(ix + r, iy - r, ix - r, iy + r, RGB(225, 230, 240), imax(2, fs + 1)); fillRect(ix - r, iy + r * 2 / 3, r / 2, r / 2, RGB(230, 180, 60)); fillRect(ix + r / 2, iy + r * 2 / 3, r / 2, r / 2, RGB(230, 180, 60)); }
    else { fillRect(ix - r * 3 / 4, iy - r, r * 3 / 2, r * 2, RGB(235, 215, 170)); for (int k = 0; k < 3; k++) fillRect(ix - r / 2, iy - r / 2 + k * r / 2, r, imax(1, fs / 2 + 1), RGB(120, 90, 60)); fillRect(ix - r, iy - r - fs, r * 2, fs * 2, RGB(200, 170, 120)); }
    drawFit(ix, by + bh - fs * 9, TN[i], bw - fs * 2, imax(1, fs - 1), on ? RGB(255, 255, 255) : RGB(170, 180, 200), 1);
    if (hitR(bx, by, bw, bh)) { menuTab = i; infoCard = -1; swapCard = -1; sfx(SFX_CLICK); }
    if (dot) fillCircle(x + w - fs * 4, y + fs * 4, fs * 3, RGB(255, 70, 60), 255); }
}
// ---- превью арены в меню (с башнями) ----
static void drawArenaPreview(int y0, int y1) {
  static int lastArena = -1;
  int a = arenaOf(pf.trophies);
  if (lastArena != a || B.units.empty() || previewDirty) { previewDirty = false; int d[8], l[8]; for (int i = 0; i < 8; i++) { d[i] = START_DECK[i]; l[i] = 1; } startBattle(d, l, pf.kingLvl, d, l, pf.kingLvl, false, a, 1); lastArena = a; }
  B.arena = a; viewTeam = 0;
  setClipRect(0, y0, IW, y1); setupArenaCam(y0, y1); gShowBars = false; drawBattle3D(1, false, -1, 0, 0, false); gShowBars = true; setClipRect(0, 0, IW, IH);
}
static void slotsRow(int y) {
  int w = (colW - fs * 10) / 4, h = w * 5 / 4; char b[96];
  bool anyOpening = false; for (int s = 0; s < NSLOT; s++) if (pf.slots[s].type >= 0 && pf.slots[s].opening) anyOpening = true;
  for (int s = 0; s < NSLOT; s++) { int x = colX + fs * 2 + s * (w + fs * 2); Slot &sl = pf.slots[s];
    fillRect(x, y, w, h, RGB(30, 36, 56)); fillRect(x + fs, y + fs, w - fs * 2, h - fs * 2, RGB(40, 48, 72));
    if (sl.type < 0) { drawText(x + w / 2, y + h / 2 - fs * 3, "ПУСТО", imax(1, fs - 1), RGB(110, 120, 150), 1); continue; }
    const ChestT &ct = CHESTS[sl.type]; int cx = x + w / 2, cy = y + h / 2 - fs * 2, cw = w * 3 / 5, chh = w * 2 / 5;
    fillRect(cx - cw / 2, cy - chh / 2, cw, chh, ct.col2); fillRect(cx - cw / 2 + fs, cy - chh / 2 + fs, cw - fs * 2, chh / 2 - fs, ct.col); fillRect(cx - fs * 2, cy - fs, fs * 4, fs * 4, RGB(240, 220, 120));
    long long left = sl.opening ? sl.unlockAt - nowSec() : (long long)ct.unlockMin * 60;
    if (sl.opening && left <= 0) { bool on = fmodf(gTime, 0.8f) < 0.5f; drawText(cx, y + h - fs * 10, "ОТКРЫТЬ!", imax(1, fs - 1), on ? RGB(140, 255, 160) : RGB(255, 255, 255), 1); }
    else { fmtTime(b, sizeof b, left); drawText(cx, y + h - fs * 10, b, imax(1, fs - 1), sl.opening ? RGB(255, 220, 120) : RGB(200, 200, 220), 1); }
    if (hitR(x, y, w, h)) { sfx(SFX_CLICK);
      if (sl.opening && left <= 0) { int t = sl.type; sl.type = -1; sl.opening = false; openChest(t); return; }
      if (!sl.opening) { if (!anyOpening) { sl.opening = true; sl.unlockAt = nowSec() + ct.unlockMin * 60; saveProfile(); say("СУНДУК ОТКРЫВАЕТСЯ"); }
        else { int cost = imax(1, (int)((ct.unlockMin + 5) / 6)); if (pf.gems >= cost) { pf.gems -= cost; int t = sl.type; sl.type = -1; openChest(t); return; } else { snprintf(b, sizeof b, "УЖЕ ОТКРЫВАЕТСЯ ДРУГОЙ. ЗА КРИСТАЛЛЫ: %d", cost); say(b); } } }
      else { int cost = imax(1, (int)((left / 60 + 5) / 6)); if (pf.gems >= cost) { pf.gems -= cost; int t = sl.type; sl.type = -1; sl.opening = false; openChest(t); return; } snprintf(b, sizeof b, "ОТКРЫТЬ СЕЙЧАС: %d КРИСТАЛЛОВ", cost); say(b); } } }
}
static void startLobby();                                  // b4
static int previewY0() { return safeTop() + fs * 16; }
static int previewY1() { return previewY0() + (IH - tabH() - previewY0()) * 45 / 100; }
// ---- главный экран: картина-фон, плашка игрока, выбор режима и большая кнопка БОЙ ----
static int menuMode = 0;                                         // 0 - 1 на 1 с ботом, 1 - 2 на 2 с ботом, 2 - с другом по Wi-Fi
static void bigButton(int x, int y, int w, int h, u32 col) {      // объёмная кнопка: тень, обводка, блик
  fillRect(x + fs, y + fs * 2, w, h, RGB(10, 10, 20)); fillRect(x - 1, y - 1, w + 2, h + 2, RGB(20, 16, 10));
  fillRect(x, y, w, h, shadec(col, 0.72f)); fillRect(x + fs, y + fs, w - fs * 2, h - fs * 3, col);
  fillRect(x + fs, y + fs, w - fs * 2, imax(1, h / 3), shadec(col, 1.18f)); fillRect(x + fs * 2, y + fs + h / 10, w / 3, imax(1, fs), shadec(col, 1.35f));
}
static void menuArt(int y0, int y1) {
  int a = arenaOf(pf.trophies); const Arena &ar = ARENAS[a]; int h = imax(1, y1 - y0);
  // небо: от тёмно-синего вверху к сиреневому у горизонта
  for (int y = y0; y < y1; y++) { float t = (y - y0) / (float)h; fillRect(0, y, IW, 1, t < 0.62f ? mixc(RGB(26, 30, 78), RGB(120, 70, 150), t / 0.62f) : mixc(RGB(120, 70, 150), RGB(60, 40, 90), (t - 0.62f) / 0.38f)); }
  int hz = y0 + h * 60 / 100, cx = IW / 2;
  for (int k = 0; k < 9; k++) { float an = -1.5708f + (k - 4) * 0.2f + sinf(gTime * 0.3f) * 0.03f; int len = h;   // лучи света из-за замка
    for (int t = 0; t < len; t += imax(2, fs)) { int px = cx + (int)(cosf(an) * t), py = hz - h / 6 + (int)(sinf(an) * t); if (py < y0) break; fillRectA(px - fs * 3, py, fs * 6, imax(2, fs), RGB(255, 220, 160), 18); } }
  fillCircle(cx, hz - h / 6, h / 5, RGB(255, 200, 140), 40); fillCircle(cx, hz - h / 6, h / 9, RGB(255, 230, 190), 60);
  for (int k = 0; k < 40; k++) { unsigned hh = hsh(k * 977u); int sx = (int)(hh % (unsigned)IW), sy = y0 + (int)((hh >> 12) % (unsigned)imax(1, h / 2)); int tw = (int)(sinf(gTime * 2 + k) * 60 + 150); fillRectA(sx, sy, imax(1, fs / 2 + 1), imax(1, fs / 2 + 1), RGB(255, 255, 230), tw); }   // звёзды
  // горы двумя слоями
  for (int x = 0; x < IW; x += imax(1, fs)) { float fx = x / (float)IW;
    int m1 = hz - (int)((0.22f + 0.1f * sinf(fx * 7.0f + 1.0f) + 0.06f * sinf(fx * 17.0f)) * h * 0.55f); fillRect(x, m1, imax(1, fs), hz - m1, RGB(70, 50, 110));
    int m2 = hz - (int)((0.1f + 0.07f * sinf(fx * 11.0f + 3.0f) + 0.03f * sinf(fx * 29.0f)) * h * 0.55f); fillRect(x, m2, imax(1, fs), hz - m2, RGB(52, 40, 86)); }
  // замок-силуэт на горизонте
  { int cw = IW * 36 / 100, chh = h * 22 / 100, x0 = cx - cw / 2, top = hz - chh; u32 cc = RGB(40, 30, 66), hl = RGB(255, 200, 110);
    fillRect(x0, top + chh / 3, cw, chh * 2 / 3, cc);
    for (int t = 0; t < 4; t++) { int tw = cw / 7, tx = x0 + t * (cw - tw) / 3, th = chh * (t == 1 || t == 2 ? 100 : 75) / 100; fillRect(tx, hz - th, tw, th, cc);
      for (int z = 0; z < 3; z++) fillRect(tx + z * tw / 3, hz - th - fs * 3, tw / 5, fs * 3, cc);
      fillRect(tx + tw / 2 - fs, hz - th + th / 4, fs * 2, fs * 3, hl); fillRect(tx + tw / 2, hz - th - fs * 12, imax(1, fs / 2), fs * 9, cc); fillRect(tx + tw / 2, hz - th - fs * 12, fs * 5, fs * 3, t & 1 ? RGB(220, 70, 60) : RGB(70, 120, 230)); }
    fillRect(cx - cw / 14, hz - chh / 4, cw / 7, chh / 4, RGB(20, 14, 34)); }
  // земля арены
  for (int y = hz; y < y1; y++) { float t = (y - hz) / (float)imax(1, y1 - hz); fillRect(0, y, IW, 1, mixc(mixc(ar.grass2, RGB(40, 30, 70), 0.55f), ar.grass, t)); }
  // герои на переднем плане: свои 3D-модели, крупно
  setClipRect(0, y0, IW, y1); clearDepth(); gLight = 1.1f; gNight = 0; fogA = 1e9f; fogB = 2e9f; LDIR = vnorm(v3(0.35f, 0.8f, 0.6f)); int vt = viewTeam; viewTeam = 0;
  setCamera(v3(9, 2.4f, 7.5f), v3(9, 1.5f, 0)); focal = IW * 1.15f; vcx = IW * 0.5f; vcy = y1 - h * 0.36f;
  int heroes[3] = {M_KNIGHT, M_GOBKING, M_ICEWIZ}; float hx[3] = {7.0f, 9.0f, 11.0f}, hzz[3] = {0.8f, 0.0f, 0.8f}, hyaw[3] = {0.45f, 0.0f, -0.45f};
  for (int i = 0; i < 3; i++) { float at = fmodf(gTime * 0.5f + i * 0.33f, 1.0f); float atk = at > 0.85f ? (at - 0.85f) / 0.15f : 0;
    DISC(hx[i], hzz[i], 0.9f, 0.01f, mixc(ar.grass, RGB(0, 0, 0), 0.35f)); drawModel(heroes[i], 0, hx[i], 0, hzz[i], hyaw[i], 0, atk, 0, false, 1); }
  viewTeam = vt; setClipRect(0, 0, IW, IH);
  for (int k = 0; k < fs * 10; k++) fillRectA(0, y1 - k, IW, 1, RGB(14, 16, 34), 250 - k * 250 / (fs * 10));   // плавный переход в панель
}
static void menuBattle() {
  char b[96]; int a = arenaOf(pf.trophies);
  int tabsTop = IH - tabH(), sw = (colW - fs * 10) / 4, slH = sw * 5 / 4;
  int slotsY = tabsTop - slH - fs * 3, btnH = fs * 36, btnY = slotsY - btnH - fs * 5, modeY = btnY - fs * 15;
  int bannerY = safeTop() + fs * 18, bannerH = fs * 24;
  menuArt(bannerY, modeY);
  // плашка игрока
  { int w = colW * 62 / 100, x = colX + fs * 2; fillRectA(x, bannerY + fs * 2, w, bannerH, RGB(10, 14, 40), 190); fillRect(x, bannerY + fs * 2, fs, bannerH, RGB(255, 200, 70));
    drawTextSh(x + fs * 4, bannerY + fs * 4, pf.name, fs + 1, RGB(255, 255, 255), 0);
    snprintf(b, sizeof b, "АРЕНА %d: %s", a + 1, ARENAS[a].name); drawFit(x + fs * 4, bannerY + fs * 14, b, w - fs * 6, fs, RGB(255, 220, 140), 0); }
  // сундук корон - справа на картине
  { int w = colW * 30 / 100, x = colX + colW - w - fs * 2, y = bannerY + fs * 2; bool ok = pf.crowns >= 10 && pf.crownDay >= 0;
    if (pf.crowns < 0) snprintf(b, sizeof b, "СУНДУК КОРОН\nЗАВТРА"); else snprintf(b, sizeof b, "СУНДУК КОРОН\n%d / 10", pf.crowns);
    if (button(x, y, w, bannerH, ok ? "СУНДУК КОРОН\nОТКРЫТЬ!" : b, ok, RGB(70, 110, 220))) { pf.crowns = -1000; openChest(CH_CROWN); return; } }
  if (a + 1 < NARENA) { snprintf(b, sizeof b, "СЛЕДУЮЩАЯ АРЕНА - С %d КУБКОВ", ARENAS[a + 1].trophies); drawTextSh(IW / 2, bannerY + bannerH + fs * 6, b, fs, RGB(220, 225, 245), 1); }
  // панель внизу
  fillRect(0, modeY, IW, tabsTop - modeY, RGB(14, 16, 34));
  // выбор режима
  { static const char *MN[6] = {"1 НА 1 С БОТОМ", "2 НА 2 С БОТОМ", "С ДРУГОМ (WI-FI)", "ТРОЙНОЙ ЭЛИКСИР", "СЛУЧАЙНЫЕ КОЛОДЫ", "ТУРНИР ИЗ 5 БОЁВ"}; int w = colW * 70 / 100, x = IW / 2 - w / 2, h = fs * 12;
    fillRect(x, modeY + fs, w, h, RGB(70, 50, 130)); fillRect(x, modeY + fs, w, imax(1, h / 4), RGB(100, 75, 170));
    snprintf(b, sizeof b, "РЕЖИМ: %s", MN[menuMode]); drawFit(IW / 2, modeY + fs * 4, b, w - fs * 26, fs, RGB(255, 255, 255), 1);
    if (button(x, modeY + fs, fs * 12, h, "<", true, RGB(90, 70, 160))) { menuMode = (menuMode + 5) % 6; sfx(SFX_CLICK); }
    if (button(x + w - fs * 12, modeY + fs, fs * 12, h, ">", true, RGB(90, 70, 160))) { menuMode = (menuMode + 1) % 6; sfx(SFX_CLICK); } }
  // колода слева, БОЙ по центру, бесплатный сундук справа
  int sideW = colW * 22 / 100, midW = colW - sideW * 2 - fs * 8, x0 = colX + fs * 2;
  { bigButton(x0, btnY, sideW, btnH, RGB(60, 90, 170)); int ch = btnH - fs * 14, cw = imin(sideW * 45 / 100, ch * 5 / 6);
    for (int k = 0; k < 2; k++) { int cx = x0 + sideW / 2 - cw / 2 + (k ? fs * 3 : -fs * 3), cy = btnY + fs * 3 + (k ? 0 : fs); if (k) drawCard(pf.deck[0], pf.lvl[pf.deck[0]], cx, cy, cw, ch, false, true, false); else fillRect(cx, cy, cw, ch, RGB(40, 50, 90)); }
    drawText(x0 + sideW / 2, btnY + btnH - fs * 9, "КОЛОДА", imax(1, fs - 1), RGB(255, 255, 255), 1);
    if (hitR(x0, btnY, sideW, btnH)) { menuTab = 1; sfx(SFX_CLICK); return; } }
  { int x = x0 + sideW + fs * 4; bigButton(x, btnY, midW, btnH, RGB(240, 175, 40));
    drawTextSh(x + midW / 2, btnY + fs * 4, "БОЙ", fs * 2, RGB(255, 255, 255), 1);
    char sb[48]; static const char *SUB[6] = {"ПРОТИВ БОТА", "ТЫ И БОТ ПРОТИВ ДВУХ", "ПО WI-FI", "ЭЛИКСИР В 3 РАЗА БЫСТРЕЕ", "КОЛОДЫ НАУГАД", ""}; if (menuMode == 5) snprintf(sb, sizeof sb, "БОЙ %d ИЗ 5", imax(1, tourStage)); else snprintf(sb, sizeof sb, "%s", SUB[menuMode]); drawText(x + midW / 2, btnY + btnH - fs * 9, sb, imax(1, fs - 1), RGB(90, 50, 10), 1);
    if (hitR(x, btnY, midW, btnH)) { if (menuMode == 0) startBotBattle(); else if (menuMode == 1) startDuoBot(); else if (menuMode == 2) startLobby(); else startSpecial(menuMode); return; } }
  { int x = x0 + sideW + midW + fs * 8; long long left = pf.freeAt - nowSec(); bool ok = left <= 0;
    bigButton(x, btnY, sideW, btnH, ok ? RGB(60, 160, 80) : RGB(70, 80, 110));
    int bh = btnH - fs * 16, bw = imin(sideW * 6 / 10, bh * 4 / 3), bx = x + sideW / 2 - bw / 2, by = btnY + fs * 4;                                       // сундучок
    fillRect(bx, by + bh / 3, bw, bh * 2 / 3, RGB(150, 100, 50)); fillRect(bx, by, bw, bh / 3, RGB(175, 120, 60)); fillRect(bx, by + bh / 3 - fs / 2, bw, imax(1, fs), RGB(230, 190, 70)); fillRect(bx + bw / 2 - fs, by + bh / 3 - fs, fs * 2, fs * 3, RGB(240, 210, 90));
    if (ok) drawText(x + sideW / 2, btnY + btnH - fs * 9, "ОТКРЫТЬ!", imax(1, fs - 1), RGB(255, 255, 255), 1); else { char t[32]; fmtTime(t, sizeof t, left); drawText(x + sideW / 2, btnY + btnH - fs * 9, t, imax(1, fs - 1), RGB(210, 215, 230), 1); }
    if (hitR(x, btnY, sideW, btnH)) { if (ok) { pf.freeAt = nowSec() + 4 * 3600; openChest(CH_FREE); return; } else say("БЕСПЛАТНЫЙ СУНДУК ЕЩЁ НЕ ГОТОВ"); } }
  slotsRow(slotsY);
}
static int cardStatLine(int c, int lvl, char lines[][64]) {
  const Card &cd = CARDS[c]; int n = 0; float k = lvlK(lvl);
  if (cd.type == CT_SPELL && cd.spell == SP_BARREL) { const UnitT &g = UNITS[cd.unit]; snprintf(lines[n++], 64, "ГОБЛИНОВ: %d", cd.count); if (cd.sDmg > 0) snprintf(lines[n++], 64, "УРОН ПРИ ПАДЕНИИ: %d", (int)(cd.sDmg * k)); snprintf(lines[n++], 64, "ГОБЛИН: ЗДОРОВЬЕ %d, УРОН %d", (int)(g.hp * k), (int)(g.dmg * k)); snprintf(lines[n++], 64, "РАДИУС: %.1f", cd.sR); snprintf(lines[n++], 64, "ЛЕТИТ КУДА УГОДНО"); return n; }
  if (cd.type == CT_SPELL) { if (cd.spell == SP_HEAL) snprintf(lines[n++], 64, "ЛЕЧЕНИЕ: %d", (int)(cd.sDmg * k)); else snprintf(lines[n++], 64, "УРОН: %d (ПО БАШНЯМ %d)", (int)(cd.sDmg * k), (int)(cd.sDmg * k * cd.sTowerK));
    snprintf(lines[n++], 64, "РАДИУС: %.1f", cd.sR); if (cd.sTime > 0) snprintf(lines[n++], 64, "ДЛИТСЯ: %.0f С", cd.sTime); return n; }
  const UnitT &u = UNITS[cd.unit];
  snprintf(lines[n++], 64, "ЗДОРОВЬЕ: %d", (int)(u.hp * k));
  if (u.shield > 0) snprintf(lines[n++], 64, "ЩИТ: %d", (int)(u.shield * k));
  if (cd.unit == U_STRONG) snprintf(lines[n++], 64, "ПЛОЩАДЬ УДАРА: 2 x 6 КЛЕТОК");
  if (cd.unit == U_FSPIRIT) snprintf(lines[n++], 64, "ВЗРЫВ ПО ПЛОЩАДИ: %d, ПО ЗЕМЛЕ И ВОЗДУХУ", (int)(u.dmg * k));
  if (cd.unit == U_ISPIRIT) snprintf(lines[n++], 64, "ВЗРЫВ %d И ЗАМОРОЗКА НА %.0f С", (int)(u.dmg * k), SPIRIT_FREEZE);
  if (cd.unit == U_FURNACE) snprintf(lines[n++], 64, "ВЫПУСКАЕТ ОГНЕННОГО ДУХА КАЖДЫЕ %.0f С", UNITS[U_FURNACE].spawnT);
  if (cd.unit == U_COLLECTOR) { snprintf(lines[n++], 64, "ЭЛИКСИР: +1 КАЖДЫЕ %.0f С", COLL_T); snprintf(lines[n++], 64, "ПРИ ГИБЕЛИ: +1 ЭЛИКСИР"); }
  if (cd.unit == U_INFERNO) { float ds = u.dmg * k / u.hitT; snprintf(lines[n++], 64, "УРОН В СЕК: %d > %d > %d", (int)ds, (int)(ds * INF_M2), (int)(ds * INF_M3)); snprintf(lines[n++], 64, "РАЗОГРЕВ: %.0f С И %.0f С НА ОДНОЙ ЦЕЛИ", INF_T2, INF_T3); }
  if (cd.unit == U_CGOLEM) { snprintf(lines[n++], 64, "ПО ПЛОЩАДИ: %d (ЕСЛИ ВРАГОВ 2+)", (int)(u.dmg * CG_SPLASH_K * k)); snprintf(lines[n++], 64, "КАЖДОМУ СЛЕДУЮЩЕМУ В ТОЛПЕ УРОН +10%%"); snprintf(lines[n++], 64, "ТЯНЕТ ЦЕПЬЮ: 8 КЛЕТОК, РАЗ В 5 С"); }
  if (cd.unit == U_MEGA) { snprintf(lines[n++], 64, "УРОН ПРИ ПОЯВЛЕНИИ: %d", (int)(UNITS[U_MEGA].dmg * 1.604f * k)); snprintf(lines[n++], 64, "УРОН ПРЫЖКОМ: %d (68%% УДАРА)", (int)(UNITS[U_MEGA].dmg * MEGA_JUMP_K * k)); }
  if (cd.unit == U_ICEWIZ) { snprintf(lines[n++], 64, "УРОН ПРИ ПОЯВЛЕНИИ: %d", (int)(ICEWIZ_SPAWN_DMG * k)); snprintf(lines[n++], 64, "ЗАМОРОЗКА ВОКРУГ: 2 С (2 КЛЕТКИ)"); snprintf(lines[n++], 64, "ЗАМЕДЛЕНИЕ ШАРАМИ: 2.5 С"); }
  if (u.dmg > 0) snprintf(lines[n++], 64, "УРОН: %d%s", (int)(u.dmg * k), u.splash > 0 || cd.unit == U_STRONG ? " ПО ПЛОЩАДИ" : "");
  if (u.dmg > 0) snprintf(lines[n++], 64, "СКОРОСТЬ УДАРА: %.1f С", u.hitT);
  if (cd.type == CT_TROOP) snprintf(lines[n++], 64, "СКОРОСТЬ: %s", u.speed >= 1.9f ? "ОЧЕНЬ БЫСТРО" : (u.speed >= 1.3f ? "БЫСТРО" : (u.speed >= 0.95f ? "СРЕДНЕ" : (u.speed >= 0.65f ? "МЕДЛЕННО" : "ОЧЕНЬ МЕДЛЕННО"))));
  if (u.dmg > 0) snprintf(lines[n++], 64, "ДАЛЬНОСТЬ: %s", u.range < 1.5f ? "ВБЛИЗИ" : (u.range < 4 ? "СРЕДНЯЯ" : "ДАЛЕКО"));
  snprintf(lines[n++], 64, "ЦЕЛИ: %s", u.onlyBuild ? "ТОЛЬКО ЗДАНИЯ" : (u.canAir ? "НАЗЕМНЫЕ И ВОЗДУШНЫЕ" : "НАЗЕМНЫЕ"));
  if (cd.count > 1) snprintf(lines[n++], 64, "КОЛИЧЕСТВО: %d", cd.count);
  if (u.lifeT > 0) snprintf(lines[n++], 64, "ВРЕМЯ ЖИЗНИ: %.0f С", u.lifeT);
  return n;
}
static void cardInfo() {
  int c = infoCard; const Card &cd = CARDS[c]; char b[96], lines[14][64];
  int pw = colW - fs * 6, ph = IH * 3 / 4, x0 = colX + fs * 3, y0 = IH / 8; fillRectA(0, 0, IW, IH, RGB(0, 0, 0), 150); fillRect(x0, y0, pw, ph, RGB(28, 34, 54)); fillRect(x0, y0, pw, fs * 2, RARC[cd.rarity]);
  int cw = pw / 3, ch = cw * 6 / 5; drawCard(c, pf.lvl[c], x0 + fs * 4, y0 + fs * 6, cw, ch, false, true, true);
  int tx = x0 + cw + fs * 8; drawTextSh(tx, y0 + fs * 6, cd.name, fs + 1, RGB(255, 255, 255), 0); drawText(tx, y0 + fs * 18, RARN[cd.rarity], fs, RARC[cd.rarity], 0);
  snprintf(b, sizeof b, "УРОВЕНЬ %d", pf.lvl[c]); drawText(tx, y0 + fs * 28, b, fs, RGB(255, 230, 150), 0);
  int lvl = pf.lvl[c];
  if (lvl < MAXLVL) { int need = needCards(cd.rarity, lvl + 1); snprintf(b, sizeof b, "КАРТ: %d / %d", pf.cnt[c], need); int bw = pw - cw - fs * 12; fillRect(tx, y0 + fs * 38, bw, fs * 6, RGB(20, 20, 30)); fillRect(tx, y0 + fs * 38, (int)(bw * clampf(pf.cnt[c] / (float)need, 0, 1)), fs * 6, pf.cnt[c] >= need ? RGB(80, 200, 90) : RGB(60, 120, 220)); drawText(tx + bw / 2, y0 + fs * 38, b, imax(1, fs - 1), RGB(255, 255, 255), 1); }
  else drawText(tx, y0 + fs * 38, "МАКСИМАЛЬНЫЙ УРОВЕНЬ", fs, RGB(140, 255, 160), 0);
  // описание (переносим по словам)
  { int y = y0 + ch + fs * 12, maxw = pw - fs * 8; const char *p = cd.desc; char line[160] = ""; int ln = 0;
    while (*p) { const char *sp = strchr(p, ' '); int wl = sp ? (int)(sp - p) : (int)strlen(p); char word[64]; snprintf(word, sizeof word, "%.*s", imin(wl, 63), p);
      char test[240]; snprintf(test, sizeof test, "%s%s%s", line, ln ? " " : "", word); if (ln && textW(test, fs) > maxw) { drawText(x0 + fs * 4, y, line, fs, RGB(220, 225, 240), 0); y += fs * 10; snprintf(line, sizeof line, "%s", word); } else snprintf(line, sizeof line, "%s", test); ln = 1;
      p += wl; while (*p == ' ') p++; }
    if (ln) { drawText(x0 + fs * 4, y, line, fs, RGB(220, 225, 240), 0); y += fs * 14; }
    int n = cardStatLine(c, lvl, lines); for (int i = 0; i < n; i++) { drawText(x0 + fs * 4, y, lines[i], fs, RGB(190, 200, 225), 0); y += fs * 10; } }
  int by = y0 + ph - fs * 22, bw = (pw - fs * 12) / 3;
  bool inDeck = false; for (int i = 0; i < 8; i++) if (pf.deck[i] == c) inDeck = true;
  if (lvl < MAXLVL && pf.have[c]) { int need = needCards(cd.rarity, lvl + 1), gold = needGold(cd.rarity, lvl + 1); snprintf(b, sizeof b, "УЛУЧШИТЬ\n%d ЗОЛОТА", gold);
    if (button(x0 + fs * 3, by, bw, fs * 18, b, pf.cnt[c] >= need && pf.gold >= gold, RGB(60, 150, 70))) { pf.cnt[c] -= need; pf.gold -= gold; pf.lvl[c]++; addKingXp(XP_UP[pf.lvl[c] - 1]); questAdd(Q_UPGRADE, 1); saveProfile(); sfx(SFX_CHEST); say("КАРТА УЛУЧШЕНА!"); } }
  if (!inDeck && pf.have[c] && button(x0 + fs * 6 + bw, by, bw, fs * 18, "В КОЛОДУ", true, RGB(70, 110, 190))) { swapCard = c; infoCard = -1; say("ВЫБЕРИ КАРТУ В КОЛОДЕ ДЛЯ ЗАМЕНЫ", 3); sfx(SFX_CLICK); return; }
  if (button(x0 + fs * 9 + bw * 2, by, bw, fs * 18, "ЗАКРЫТЬ", true, RGB(90, 90, 110))) { infoCard = -1; sfx(SFX_CLICK); }
}
static void menuDeck() {
  int y0 = safeTop() + fs * 22, y1 = IH - tabH(); char b[64];
  bool popup = infoCard >= 0, clk = P.clicked; if (popup) P.clicked = false;   // пока открыто окно карты, нажатия идут только в него (иначе ЗАКРЫТЬ попадало в карту под ним)
  int cw = (colW - fs * 10) / 4, ch = cw * 6 / 5;
  float avg = 0; for (int i = 0; i < 8; i++) avg += CARDS[pf.deck[i]].cost; avg /= 8;
  snprintf(b, sizeof b, "КОЛОДА  (СРЕДНИЙ ЭЛИКСИР %.1f)", avg); drawTextSh(IW / 2, y0, b, fs, RGB(255, 230, 150), 1);
  int y = y0 + fs * 10;
  for (int i = 0; i < 8; i++) { int x = colX + fs * 2 + (i % 4) * (cw + fs * 2), yy = y + (i / 4) * (ch + fs * 2); int c = pf.deck[i];
    drawCard(c, pf.lvl[c], x, yy, cw, ch, swapCard >= 0, true, true);
    if (hitR(x, yy, cw, ch)) { sfx(SFX_CLICK); if (swapCard >= 0) { pf.deck[i] = swapCard; swapCard = -1; saveProfile(); say("КОЛОДА ИЗМЕНЕНА"); } else infoCard = c; } }
  y += 2 * (ch + fs * 2) + fs * 4;
  drawTextSh(IW / 2, y, "КОЛЛЕКЦИЯ", fs, RGB(255, 230, 150), 1); y += fs * 10;
  // коллекция с прокруткой
  int listY0 = y, listY1 = y1 - fs * 2; static int dragY = -1; static float dragS = 0;
  if (P.pressed && !popup && P.y > listY0 && P.y < listY1) { dragY = P.y; dragS = deckScroll; }
  if (P.down && dragY >= 0) deckScroll = dragS + (P.y - dragY);
  if (!P.down) dragY = -1;
  int ord[NCARD], n = 0; for (int c = 0; c < NCARD; c++) { bool in = false; for (int i = 0; i < 8; i++) if (pf.deck[i] == c) in = true; if (!in) ord[n++] = c; }
  std::sort(ord, ord + n, [](int a, int b2) { if (pf.have[a] != pf.have[b2]) return pf.have[a]; if (CARDS[a].arena != CARDS[b2].arena) return CARDS[a].arena < CARDS[b2].arena; return CARDS[a].cost < CARDS[b2].cost; });
  int rows = (n + 3) / 4, totalH = rows * (ch + fs * 10); deckScroll = clampf(deckScroll, (float)imin(0, (listY1 - listY0) - totalH), 0.0f);
  setClipRect(0, listY0, IW, listY1);
  for (int k = 0; k < n; k++) { int c = ord[k], x = colX + fs * 2 + (k % 4) * (cw + fs * 2), yy = listY0 + (int)deckScroll + (k / 4) * (ch + fs * 10);
    if (yy + ch < listY0 || yy > listY1) continue;
    if (pf.have[c]) { drawCard(c, pf.lvl[c], x, yy, cw, ch, false, true, true);
      if (pf.lvl[c] < MAXLVL) { int need = needCards(CARDS[c].rarity, pf.lvl[c] + 1); fillRect(x, yy + ch + fs, cw, fs * 3, RGB(20, 20, 30)); fillRect(x, yy + ch + fs, (int)(cw * clampf(pf.cnt[c] / (float)need, 0, 1)), fs * 3, pf.cnt[c] >= need ? RGB(80, 200, 90) : RGB(60, 120, 220)); } }
    else { fillRect(x, yy, cw, ch, RGB(35, 38, 50)); if (CARDS[c].arena <= arenaOf(pf.trophies)) { drawCard(c, 1, x, yy, cw, ch, false, false, false); drawText(x + cw / 2, yy + ch / 2, "НЕ НАЙДЕНА", imax(1, fs - 1), RGB(220, 220, 230), 1); }
      else { snprintf(b, sizeof b, "АРЕНА %d", CARDS[c].arena + 1); drawText(x + cw / 2, yy + ch / 2 - fs * 3, b, imax(1, fs - 1), RGB(150, 155, 175), 1); } }
    if (yy >= listY0 && yy + ch <= listY1 && hitR(x, yy, cw, ch) && fabsf((float)(P.cy - P.sy)) < fs * 6) { if (pf.have[c]) { infoCard = c; swapCard = -1; } else say(CARDS[c].arena <= arenaOf(pf.trophies) ? "ЭТА КАРТА ВЫПАДАЕТ ИЗ СУНДУКОВ" : "ОТКРОЕТСЯ НА СЛЕДУЮЩЕЙ АРЕНЕ"); sfx(SFX_CLICK); } }
  setClipRect(0, 0, IW, IH);
  if (popup) P.clicked = clk;
  if (infoCard >= 0) cardInfo();
}
static void menuShop() {
  int y = safeTop() + fs * 24; char b[96];
  drawTextSh(IW / 2, y, "КАРТЫ ДНЯ", fs + 1, RGB(255, 230, 150), 1); y += fs * 12;
  int cw = (colW - fs * 12) / 3, ch = cw * 6 / 5;
  for (int i = 0; i < 3; i++) { int c = pf.shopCard[i], x = colX + fs * 3 + i * (cw + fs * 3); drawCard(c, pf.lvl[c], x, y, cw, ch, false, !pf.shopBought[i], true);
    snprintf(b, sizeof b, "X%d", shopCount(c)); drawTextSh(x + cw / 2, y + ch + fs * 2, b, fs, RGB(255, 255, 255), 1);
    if (pf.shopBought[i]) drawText(x + cw / 2, y + ch + fs * 12, "КУПЛЕНО", fs, RGB(150, 160, 180), 1);
    else { snprintf(b, sizeof b, "%d ЗОЛ.", shopPrice(c)); if (button(x, y + ch + fs * 11, cw, fs * 13, b, pf.gold >= shopPrice(c), RGB(60, 150, 70))) { pf.gold -= shopPrice(c); addCards(c, shopCount(c)); pf.shopBought[i] = true; saveProfile(); sfx(SFX_CHEST); say("КУПЛЕНО!"); } } }
  y += ch + fs * 32;
  drawTextSh(IW / 2, y, "СУНДУКИ", fs + 1, RGB(255, 230, 150), 1); y += fs * 12;
  struct SC { int type, price; }; static const SC sc[3] = {{CH_GOLD, 120}, {CH_MAGIC, 360}, {CH_LEGEND, 900}};
  for (int i = 0; i < 3; i++) { int x = colX + fs * 3 + i * (cw + fs * 3); const ChestT &ct = CHESTS[sc[i].type];
    fillRect(x, y, cw, cw * 2 / 3, RGB(30, 36, 56)); fillRect(x + cw / 5, y + cw / 6, cw * 3 / 5, cw * 2 / 5, ct.col2); fillRect(x + cw / 5 + fs, y + cw / 6 + fs, cw * 3 / 5 - fs * 2, cw / 5 - fs, ct.col);
    { int sc2 = fs; while (sc2 > 1 && textW(ct.name, sc2) > cw) sc2--; drawText(x + cw / 2, y + cw * 2 / 3 + fs * 2, ct.name, sc2, ct.col, 1); }
    snprintf(b, sizeof b, "%d КРИСТ.", sc[i].price); if (button(x, y + cw * 2 / 3 + fs * 11, cw, fs * 13, b, pf.gems >= sc[i].price, RGB(70, 110, 190))) { pf.gems -= sc[i].price; openChest(sc[i].type); return; } }
  y += cw * 2 / 3 + fs * 32;
  drawTextSh(IW / 2, y, "ЗОЛОТО", fs + 1, RGB(255, 230, 150), 1); y += fs * 12;
  struct GP { int gold, gems; }; static const GP gp[3] = {{500, 30}, {2000, 100}, {10000, 450}};
  for (int i = 0; i < 3; i++) { int x = colX + fs * 3 + i * (cw + fs * 3); snprintf(b, sizeof b, "%d ЗОЛОТА\n%d КРИСТ.", gp[i].gold, gp[i].gems);
    if (button(x, y, cw, fs * 18, b, pf.gems >= gp[i].gems, RGB(170, 130, 40))) { pf.gems -= gp[i].gems; pf.gold += gp[i].gold; saveProfile(); sfx(SFX_CHEST); } }
  y += fs * 24; long long left = 86400 - (nowSec() % 86400); char t[32]; fmtTime(t, sizeof t, left); snprintf(b, sizeof b, "НОВЫЕ КАРТЫ ДНЯ ЧЕРЕЗ %s", t); drawText(IW / 2, y, b, fs, RGB(170, 180, 200), 1);
}
static bool showTop = false; static float topScroll = 0;
static void menuTop100() {
  int y0 = safeTop() + fs * 24, rowH = fs * 14; char b[96];
  drawTextSh(IW / 2, y0, "РЕЙТИНГ ТОП-100", fs + 1, RGB(255, 230, 150), 1);
  int pr = playerTopRank();
  if (pr <= 100) snprintf(b, sizeof b, "ТЫ НА %d МЕСТЕ  (РЕЙТИНГ %d)", pr, pf.rating); else snprintf(b, sizeof b, "ТЫ ВНЕ ТОПА (РЕЙТИНГ %d). ДО ТОП-100: %d", pf.rating, topRating(100) - pf.rating + 1);
  drawText(IW / 2, y0 + fs * 11, b, fs, pr <= 100 ? RGB(140, 255, 160) : RGB(220, 225, 245), 1);
  { long long left = weekLeftSec(); snprintf(b, sizeof b, "СБРОС РЕЙТИНГА ЧЕРЕЗ %lld Д %lld Ч", left / 86400, (left % 86400) / 3600); drawText(IW / 2, y0 + fs * 21, b, imax(1, fs - 1), RGB(255, 200, 120), 1);
    snprintf(b, sizeof b, "ПОБЕДА +%d   ПОРАЖЕНИЕ -%d", RATING_WIN, RATING_LOSS); drawText(IW / 2, y0 + fs * 31, b, imax(1, fs - 1), RGB(190, 200, 225), 1); }
  int backY = IH - tabH() - fs * 15, listY0 = y0 + fs * 42, listY1 = backY - fs * 2;
  static int dragY = -1; static float dragS = 0;
  if (P.pressed && P.y > listY0 && P.y < listY1) { dragY = P.y; dragS = topScroll; }
  if (P.down && dragY >= 0) topScroll = dragS + (P.y - dragY);
  if (!P.down) dragY = -1;
  int total = 100; topScroll = clampf(topScroll, (float)imin(0, (listY1 - listY0) - total * rowH), 0.0f);
  setClipRect(0, listY0, IW, listY1);
  int bi = 0;   // номер следующего бота; игрок вставляется на своё место, последний бот выпадает из списка
  for (int k = 1; k <= 100; k++) { int yy = listY0 + (int)topScroll + (k - 1) * rowH; bool me = (k == pr), vis = !(yy + rowH < listY0 || yy > listY1);
    const char *nm = me ? "YOU" : topName(bi + 1); int tr = me ? pf.rating : topRating(bi + 1); if (!me) bi++;
    if (!vis) continue;
    u32 base = me ? RGB(40, 110, 60) : (k == 1 ? RGB(120, 96, 24) : (k == 2 ? RGB(92, 98, 112) : (k == 3 ? RGB(110, 70, 40) : ((k & 1) ? RGB(30, 36, 56) : RGB(38, 45, 68)))));
    int x = colX + fs * 3, w = colW - fs * 6; fillRect(x, yy, w - 0, rowH - fs, base); if (k <= 3) fillRect(x, yy, fs * 3, rowH - fs, k == 1 ? RGB(255, 215, 80) : (k == 2 ? RGB(220, 225, 235) : RGB(215, 140, 80)));
    snprintf(b, sizeof b, "#%d", k); drawText(x + fs * 6, yy + fs * 3, b, fs, k <= 3 ? RGB(255, 235, 150) : RGB(190, 200, 225), 0);
    drawText(x + fs * 24, yy + fs * 3, nm, fs, me ? RGB(255, 255, 255) : RGB(235, 235, 245), 0);
    snprintf(b, sizeof b, "%d", tr); drawText(x + w - fs * 4, yy + fs * 3, b, fs, RGB(255, 215, 90), 2); }
  setClipRect(0, 0, IW, IH);
  if (button(IW / 2 - fs * 30, backY, fs * 60, fs * 13, "НАЗАД", true, RGB(90, 90, 110))) { showTop = false; sfx(SFX_CLICK); }
}
static void menuQuests() {
  int y = safeTop() + fs * 24; char b[96];
  drawTextSh(IW / 2, y, "ЗАДАНИЯ ДНЯ", fs + 1, RGB(255, 230, 150), 1); y += fs * 12;
  for (int i = 0; i < 3; i++) { Quest &q = pf.quests[i]; const QuestT &qt = QUESTS[q.type]; int h = fs * 30, x = colX + fs * 3, w = colW - fs * 6;
    fillRect(x, y, w, h, RGB(30, 36, 56)); snprintf(b, sizeof b, "%s: %d", qt.text, q.need); drawText(x + fs * 4, y + fs * 3, b, fs, RGB(235, 235, 245), 0);
    fillRect(x + fs * 4, y + fs * 13, w * 3 / 5, fs * 5, RGB(20, 20, 30)); fillRect(x + fs * 4, y + fs * 13, (int)(w * 3 / 5 * clampf(q.prog / (float)q.need, 0, 1)), fs * 5, q.done ? RGB(80, 200, 90) : RGB(60, 120, 220));
    snprintf(b, sizeof b, "%d / %d", q.prog, q.need); drawText(x + fs * 4 + w * 3 / 10, y + fs * 13, b, imax(1, fs - 1), RGB(255, 255, 255), 1);
    if (qt.gold) snprintf(b, sizeof b, "+%d ЗОЛ.", qt.gold); else snprintf(b, sizeof b, "+%d КРИСТ.", qt.gems); if (qt.gold && qt.gems) snprintf(b, sizeof b, "+%d ЗОЛ. +%d КР.", qt.gold, qt.gems);
    drawText(x + fs * 4, y + fs * 21, b, imax(1, fs - 1), RGB(255, 215, 90), 0);
    if (q.claimed) drawText(x + w - fs * 4, y + fs * 14, "ГОТОВО", fs, RGB(140, 255, 160), 2);
    else if (button(x + w - fs * 34, y + fs * 10, fs * 30, fs * 14, "ЗАБРАТЬ", q.done, RGB(60, 150, 70))) { q.claimed = true; pf.gold += qt.gold; pf.gems += qt.gems; saveProfile(); sfx(SFX_CHEST); }
    y += h + fs * 4; }
  long long left = 86400 - (nowSec() % 86400); char t[32]; fmtTime(t, sizeof t, left); snprintf(b, sizeof b, "НОВЫЕ ЗАДАНИЯ ЧЕРЕЗ %s", t); drawText(IW / 2, y, b, fs, RGB(170, 180, 200), 1); y += fs * 14;
  drawTextSh(colX + fs * 4, y + fs, "ПУТЬ АРЕН", fs + 1, RGB(255, 230, 150), 0);
  { int bw = colW * 42 / 100; if (button(colX + colW - fs * 3 - bw, y - fs, bw, fs * 12, "РЕЙТИНГ ТОП-100", true, RGB(150, 110, 30))) { showTop = true; topScroll = 0; sfx(SFX_CLICK); } }
  y += fs * 12;
  int a = arenaOf(pf.trophies);
  for (int i = 0; i < NARENA; i++) { int h = fs * 15, x = colX + fs * 3, w = colW - fs * 6; u32 c = i <= a ? ARENAS[i].grass : RGB(50, 54, 66);
    fillRect(x, y, w, h, mixc(c, RGB(0, 0, 0), 0.35f)); fillRect(x, y, fs * 3, h, i == a ? RGB(255, 220, 80) : c);
    snprintf(b, sizeof b, "%d. %s", i + 1, ARENAS[i].name); drawText(x + fs * 6, y + fs * 2, b, fs, i <= a ? RGB(255, 255, 255) : RGB(150, 155, 170), 0);
    if (isLegend(i)) snprintf(b, sizeof b, "ОТ %d КУБКОВ. ВМЕСТО КУБКОВ - РЕЙТИНГ ТОП-100", ARENAS[i].trophies); else snprintf(b, sizeof b, "ОТ %d КУБКОВ", ARENAS[i].trophies); drawText(x + fs * 6, y + fs * 9, b, imax(1, fs - 1), RGB(220, 210, 160), 0);
    int k = 0; for (int cc = 0; cc < NCARD; cc++) if (CARDS[cc].arena == i) k++; snprintf(b, sizeof b, "КАРТ: %d", k); drawText(x + w - fs * 4, y + fs * 5, b, imax(1, fs - 1), RGB(200, 210, 230), 2);
    y += h + fs; }
  y += fs * 4; snprintf(b, sizeof b, inLegend() ? "ПОБЕД %d   ПОРАЖЕНИЙ %d   ЛУЧШИЙ РЕЙТИНГ %d" : "ПОБЕД %d   ПОРАЖЕНИЙ %d   ЛУЧШЕЕ %d КУБКОВ", pf.wins, pf.losses, inLegend() ? pf.ratingBest : pf.bestTrophies); drawText(IW / 2, y, b, imax(1, fs - 1), RGB(190, 200, 225), 1);
  y += fs * 12; if (button(IW / 2 - fs * 30, y, fs * 60, fs * 12, pf.soundOn ? "ЗВУК: ВКЛ" : "ЗВУК: ВЫКЛ", true, RGB(70, 80, 110))) { pf.soundOn = !pf.soundOn; saveProfile(); }
}
// одна страница меню (вкладка или экран рейтинга) - рисуется и как текущая, и как уходящая при анимации перехода
static void drawMenuPage(int tab, bool top) {
  for (int y = 0; y < IH && tab != 2; y += fs * 4) fillRect(0, y, IW, fs * 4, ((y / (fs * 4)) & 1) ? RGB(36, 45, 70) : RGB(34, 42, 66));
  if (tab == 2) menuBattle(); else if (tab == 1) menuDeck(); else if (tab == 0) menuShop(); else if (top) menuTop100(); else menuQuests();
}
static void frameMenu() {
  layoutCol(); dailyRefresh();
  if (P.clicked && infoCard < 0) { int dx = P.cx - P.sx, dy = P.cy - P.sy;                 // свайп влево-вправо - соседняя вкладка
    if (abs(dx) > IW / 5 && abs(dx) > abs(dy) * 2) { int nt = clampi(menuTab + (dx < 0 ? 1 : -1), 0, 3); if (nt != menuTab) { menuTab = nt; swapCard = -1; sfx(SFX_CLICK); } P.clicked = false; } }
  if (weekJustReset) { weekJustReset = false; say("НОВАЯ НЕДЕЛЯ: РЕЙТИНГ СБРОШЕН"); }
  if (menuTab != 3) showTop = false;
  // анимация перехода: новая страница выезжает сбоку, старая уезжает; шапка и вкладки остаются на месте
  static int lastPid = -1, animFrom = -1; static Uint32 animT0 = 0, lastTick = 0; static std::vector<u32> snap, rowBuf;
  int pidNow = menuTab * 2 + (showTop ? 1 : 0); Uint32 tk = SDL_GetTicks();
  if (lastPid >= 0 && pidNow != lastPid && tk - lastTick < 300) { animFrom = lastPid; animT0 = tk; }
  lastPid = pidNow; lastTick = tk;
  float at = animFrom >= 0 ? (tk - animT0) / 240.0f : 2.0f; if (at >= 1.0f) animFrom = -1;
  if (animFrom >= 0) {   // уходящую страницу рисуем во второй буфер без нажатий
    snap.assign((size_t)IW * IH, 0); u32 *sv = fb; fb = snap.data(); Ptr sp = P; P.clicked = P.pressed = P.down = false; int sT = menuTab; bool sTop = showTop; int sInfo = infoCard; infoCard = -1;
    drawMenuPage(animFrom / 2, (animFrom & 1) != 0);
    menuTab = sT; showTop = sTop; infoCard = sInfo; P = sp; fb = sv; }
  drawMenuPage(menuTab, showTop);
  if (gs == GS_MENU && animFrom >= 0) {
    float e = 1.0f - (1.0f - at) * (1.0f - at) * (1.0f - at); int s = clampi((int)(IW * (1.0f - e)), 0, IW), dir = pidNow > animFrom ? 1 : -1, ya = safeTop() + fs * 16, yb = IH - tabH();
    rowBuf.resize(IW);
    for (int y = ya; y < yb; y++) { u32 *r = fb + (size_t)y * IW; const u32 *o = snap.data() + (size_t)y * IW; memcpy(rowBuf.data(), r, sizeof(u32) * IW);
      if (dir > 0) { memcpy(r, o + (IW - s), sizeof(u32) * s); memcpy(r + s, rowBuf.data(), sizeof(u32) * (IW - s)); }
      else { memcpy(r + (IW - s), o, sizeof(u32) * s); memcpy(r, rowBuf.data() + s, sizeof(u32) * (IW - s)); } } }
  if (gs != GS_MENU) return;
  topBar(); tabsBar();
  if (msgT > 0) { int w = textW(msg, fs) + fs * 8; fillRectA(IW / 2 - w / 2, IH - tabH() - fs * 16, w, fs * 11, RGB(0, 0, 0), 190); drawText(IW / 2, IH - tabH() - fs * 14, msg, fs, RGB(255, 230, 160), 1); }
}

// =====================================================================
//  БИТВА БАШЕН: бой с другом по Wi-Fi (UDP). Хост ведёт бой, друг шлёт ходы и получает состояние.
// =====================================================================
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
  #endif
  typedef SOCKET sock_t;
  #define BADSOCK INVALID_SOCKET
  static void sockClose(sock_t s) { closesocket(s); }
  static void sockNB(sock_t s) { u_long m = 1; ioctlsocket(s, FIONBIO, &m); }
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <errno.h>
  typedef int sock_t;
  #define BADSOCK (-1)
  static void sockClose(sock_t s) { close(s); }
  static void sockNB(sock_t s) { int f = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, f | O_NONBLOCK); }
#endif

static const unsigned short NET_PORT = 27345;
static const unsigned NET_MAGIC = 0x42415348u;   // BASH
static const int NET_VER = 1;
enum { NET_OFF, NET_HOST, NET_CLIENT };
static int netMode = NET_OFF;
static bool netPeer = false;                     // есть ли связь с другом
static bool netStarted = false;                  // игра началась (после лобби)
static sock_t netSock = BADSOCK;
static sockaddr_in netPeerAddr, netFoundAddr;
static bool netFound = false, netBye = false;   // netBye - друг сам вышел из игры
static float netLastRecv = 0, netSendT = 0, netWorldT = 0, netWorld2T = 0, netDiscT = 0;
static char manualIP[24] = "";
static char netMyIP[32] = "", netFoundIP[32] = "", netStatus[96] = "";

// пакеты
enum { PK_DISCOVER = 1, PK_HERE, PK_JOIN, PK_WELCOME, PK_PING, PK_STATE, PK_BYE };
// события с подтверждением (доставляются по порядку)
enum { EV_DECK = 1, EV_START, EV_PLAY, EV_END, EV_DENY, EV_QUIT };
struct NetEv { unsigned seq; unsigned char type, pad[3]; int i0, i1; float f[4]; };
static std::vector<NetEv> netOut;                // ещё не подтверждённые
static unsigned netOutSeq = 0, netInSeq = 0;     // последний отправленный / последний принятый по порядку
static std::vector<NetEv> netInbox;              // принятые, ждут обработки игрой

static void netEvent(int type, int i0 = 0, int i1 = 0, float f0 = 0, float f1 = 0, float f2 = 0, float f3 = 0) {
  if (netMode == NET_OFF || !netPeer) return;
  NetEv e; memset(&e, 0, sizeof e); e.seq = ++netOutSeq; e.type = (unsigned char)type; e.i0 = i0; e.i1 = i1; e.f[0] = f0; e.f[1] = f1; e.f[2] = f2; e.f[3] = f3;
  netOut.push_back(e); if (netOut.size() > 400) netOut.erase(netOut.begin());
}

// запись и чтение пакета
struct PBuf { unsigned char d[1400]; int n, r; bool bad; };
static void pbInit(PBuf &b) { b.n = 0; b.r = 0; b.bad = false; }
static void wU8(PBuf &b, int v) { if (b.n < (int)sizeof b.d) b.d[b.n++] = (unsigned char)v; else b.bad = true; }
static void wU16(PBuf &b, int v) { wU8(b, v & 255); wU8(b, (v >> 8) & 255); }
static void wU32(PBuf &b, unsigned v) { wU16(b, (int)(v & 0xFFFF)); wU16(b, (int)(v >> 16)); }
static void wF(PBuf &b, float f) { unsigned u; memcpy(&u, &f, 4); wU32(b, u); }
static int rU8(PBuf &b) { if (b.r < b.n) return b.d[b.r++]; b.bad = true; return 0; }
static int rU16(PBuf &b) { int a = rU8(b); return a | (rU8(b) << 8); }
static int rI16(PBuf &b) { int v = rU16(b); return v >= 32768 ? v - 65536 : v; }
static unsigned rU32(PBuf &b) { unsigned a = (unsigned)rU16(b); return a | ((unsigned)rU16(b) << 16); }
static float rF(PBuf &b) { unsigned u = rU32(b); float f; memcpy(&f, &u, 4); return f; }
static void wHead(PBuf &b, int type, int maxEv = 12) {
  pbInit(b); wU32(b, NET_MAGIC); wU8(b, NET_VER); wU8(b, type); wU32(b, netInSeq);
  int n = 0; for (size_t i = 0; i < netOut.size() && n < maxEv; i++) n++;
  wU8(b, n);
  for (int i = 0; i < n; i++) { const NetEv &e = netOut[i]; wU32(b, e.seq); wU8(b, e.type); wU32(b, (unsigned)e.i0); wU32(b, (unsigned)e.i1); for (int k = 0; k < 4; k++) wF(b, e.f[k]); }
}

static bool netOpen(bool host) {
#ifdef _WIN32
  static bool wsa = false; if (!wsa) { WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false; wsa = true; }
#endif
  if (netSock != BADSOCK) { sockClose(netSock); netSock = BADSOCK; }
  netSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); if (netSock == BADSOCK) return false;
  int yes = 1; setsockopt(netSock, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof yes); setsockopt(netSock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
  sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(host ? NET_PORT : 0);
  if (bind(netSock, (sockaddr *)&a, sizeof a) != 0) { sockClose(netSock); netSock = BADSOCK; return false; }
  sockNB(netSock);
  return true;
}
// свой адрес в сети: через "подключение" UDP-сокета (пакеты не уходят)
static unsigned netLocalIP(const char *probe) {
  sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); if (s == BADSOCK) return 0;
  sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(9); a.sin_addr.s_addr = inet_addr(probe);
  unsigned ip = 0;
  if (connect(s, (sockaddr *)&a, sizeof a) == 0) { sockaddr_in m; socklen_t l = sizeof m; if (getsockname(s, (sockaddr *)&m, &l) == 0) ip = ntohl(m.sin_addr.s_addr); }
  sockClose(s); return ip;
}
static void ipStr(unsigned ip, char *o, int n) { snprintf(o, n, "%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); }
static void netFindMyIP() {
  const char *pr[3] = {"192.168.43.255", "10.255.255.255", "8.8.8.8"}; unsigned ip = 0;
  for (int i = 0; i < 3 && (!ip || (ip >> 24) == 127); i++) ip = netLocalIP(pr[i]);
  if (ip && (ip >> 24) != 127) ipStr(ip, netMyIP, sizeof netMyIP); else snprintf(netMyIP, sizeof netMyIP, "НЕИЗВЕСТЕН");
}
static void netSendTo(const PBuf &b, const sockaddr_in &to) { if (netSock != BADSOCK && !b.bad) sendto(netSock, (const char *)b.d, b.n, 0, (const sockaddr *)&to, sizeof to); }
static void netSend(const PBuf &b) { if (netPeer || (netStarted && !netBye)) netSendTo(b, netPeerAddr); }   // во время игры шлём и при обрыве: вдруг связь вернётся
static void netReset() {
  netOut.clear(); netInbox.clear(); netOutSeq = netInSeq = 0; netPeer = false; netStarted = false; netFound = false; netBye = false; netLastRecv = 0;
}
static void netClose() {
  if (netSock != BADSOCK) { if (netPeer) { PBuf b; wHead(b, PK_BYE); for (int i = 0; i < 3; i++) netSend(b); } sockClose(netSock); netSock = BADSOCK; }
  netMode = NET_OFF; netReset();
}
static bool netHost() { netReset(); if (!netOpen(true)) { snprintf(netStatus, sizeof netStatus, "НЕ УДАЛОСЬ ОТКРЫТЬ ПОРТ %d", NET_PORT); return false; } netMode = NET_HOST; netFindMyIP(); snprintf(netStatus, sizeof netStatus, "ЖДЁМ ДРУГА..."); return true; }
static bool netJoinMode() { netReset(); if (!netOpen(false)) { snprintf(netStatus, sizeof netStatus, "СЕТЬ НЕДОСТУПНА"); return false; } netMode = NET_CLIENT; netFindMyIP(); snprintf(netStatus, sizeof netStatus, "ИЩЕМ ИГРУ..."); return true; }
static void netDiscover(const char *manualIP) {
  PBuf b; pbInit(b); wU32(b, NET_MAGIC); wU8(b, NET_VER); wU8(b, PK_DISCOVER);
  sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(NET_PORT);
  if (manualIP && *manualIP) { a.sin_addr.s_addr = inet_addr(manualIP); netSendTo(b, a); return; }
  unsigned targets[6]; int nt = 0; targets[nt++] = 0xFFFFFFFFu; targets[nt++] = 0x7F000001u;
  unsigned me = 0; const char *pr[3] = {"192.168.43.255", "10.255.255.255", "8.8.8.8"};
  for (int i = 0; i < 3; i++) { me = netLocalIP(pr[i]); if (me && (me >> 24) != 127) { targets[nt++] = (me & 0xFFFFFF00u) | 255u; targets[nt++] = (me & 0xFFFFFF00u) | 1u; } if (nt >= 6) break; }
  for (int i = 0; i < nt; i++) { a.sin_addr.s_addr = htonl(targets[i]); netSendTo(b, a); }
}
static void netJoin() { if (!netFound) return; netPeerAddr = netFoundAddr; netPeer = true; netLastRecv = 0; PBuf b; wHead(b, PK_JOIN); netSend(b); snprintf(netStatus, sizeof netStatus, "ПОДКЛЮЧАЕМСЯ..."); }

// приём: события в netInbox, состояние - в обработчик игры
static void netHandlePacket(int type, PBuf &b);    // в g25_netsync.cpp
static void netPoll(float dt) {
  if (netSock == BADSOCK) return;
  netLastRecv += dt;
  for (int guard = 0; guard < 200; guard++) {
    PBuf b; pbInit(b); sockaddr_in from; socklen_t fl = sizeof from;
    int n = (int)recvfrom(netSock, (char *)b.d, sizeof b.d, 0, (sockaddr *)&from, &fl);
    if (n <= 0) break;
    b.n = n; if (rU32(b) != NET_MAGIC || rU8(b) != NET_VER) continue;
    int type = rU8(b);
    if (type == PK_DISCOVER) { if (netMode == NET_HOST && !netStarted) { PBuf r; pbInit(r); wU32(r, NET_MAGIC); wU8(r, NET_VER); wU8(r, PK_HERE); netSendTo(r, from); } continue; }
    if (type == PK_HERE) { if (netMode == NET_CLIENT && !netPeer) { netFound = true; netFoundAddr = from; ipStr(ntohl(from.sin_addr.s_addr), netFoundIP, sizeof netFoundIP); snprintf(netStatus, sizeof netStatus, "НАЙДЕНА ИГРА: %s", netFoundIP); } continue; }
    if (type == PK_JOIN && netMode == NET_HOST) {
      bool same = netPeer && netPeerAddr.sin_addr.s_addr == from.sin_addr.s_addr && netPeerAddr.sin_port == from.sin_port;
      if (!same && (netStarted || (netPeer && netLastRecv < 6))) continue;   // уже есть друг (в начатую игру чужих не пускаем)
      if (!same) { netReset(); netPeer = true; netPeerAddr = from; }
      snprintf(netStatus, sizeof netStatus, "ДРУГ ПОДКЛЮЧИЛСЯ");
    }
    if (!(netPeer || (netStarted && !netBye)) || from.sin_addr.s_addr != netPeerAddr.sin_addr.s_addr || from.sin_port != netPeerAddr.sin_port) continue;
    if (!netPeer && type != PK_BYE) { netPeer = true; snprintf(netStatus, sizeof netStatus, "СВЯЗЬ ВОССТАНОВЛЕНА"); }   // связь вернулась
    netLastRecv = 0;
    unsigned ack = rU32(b); int ne = rU8(b);
    while (!netOut.empty() && (int)(ack - netOut[0].seq) >= 0) netOut.erase(netOut.begin());   // подтверждённые
    for (int i = 0; i < ne && !b.bad; i++) { NetEv e; memset(&e, 0, sizeof e); e.seq = rU32(b); e.type = (unsigned char)rU8(b); e.i0 = (int)rU32(b); e.i1 = (int)rU32(b); for (int k = 0; k < 4; k++) e.f[k] = rF(b);
      if ((int)(e.seq - netInSeq) > 0) { netInSeq = e.seq; netInbox.push_back(e); } }   // по порядку; пропуск возможен, только если очередь переполнилась
    if (b.bad) continue;
    if (type == PK_BYE) { netPeer = false; netBye = true; snprintf(netStatus, sizeof netStatus, "ДРУГ ВЫШЕЛ ИЗ ИГРЫ"); if (!netStarted) netReset(); continue; }
    if (type == PK_WELCOME && netMode == NET_CLIENT) snprintf(netStatus, sizeof netStatus, "ПОДКЛЮЧЕНО. ЖДЁМ, КОГДА ДРУГ НАЧНЁТ");
    netHandlePacket(type, b);
  }
  if (netPeer && netLastRecv > 8) { netPeer = false; snprintf(netStatus, sizeof netStatus, "СВЯЗЬ С ДРУГОМ ПОТЕРЯНА"); }
}

// ---------------------------------------------------------------------
//  синхронизация боя
// ---------------------------------------------------------------------
struct NetDeck { bool got; int deck[8], lvl[8], king; };
static NetDeck peerDeck; static bool netReadyStart = false, netDeckSent = false, netDuo = false;
static int netClientSide() { return netDuo ? 2 : 1; } static unsigned netSeed = 0; static int netArena = 0;
static float netStateT = 0, netSnapAge = 0, netSnapDT = 1.0f / 15;
static bool netIsClient() { return netMode == NET_CLIENT && netStarted && gs == GS_BATTLE; }
static bool netIsHost() { return netMode == NET_HOST && netStarted && gs == GS_BATTLE; }
static void netSendDeck() {
  int c0 = 0, c1 = 0; float l0 = 0, l1 = 0;
  for (int i = 0; i < 4; i++) { c0 |= pf.deck[i] << (i * 8); c1 |= pf.deck[i + 4] << (i * 8); l0 += pf.lvl[pf.deck[i]] * powf(16.0f, (float)i); l1 += pf.lvl[pf.deck[i + 4]] * powf(16.0f, (float)i); }
  netEvent(EV_DECK, c0, c1, l0, l1, (float)pf.kingLvl, 0);
}
static void readDeck(const NetEv &e, NetDeck &d) {
  for (int i = 0; i < 4; i++) { d.deck[i] = clampi((e.i0 >> (i * 8)) & 255, 0, NCARD - 1); d.deck[i + 4] = clampi((e.i1 >> (i * 8)) & 255, 0, NCARD - 1);
    d.lvl[i] = clampi(((int)e.f[0] >> (i * 4)) & 15, 1, MAXLVL); d.lvl[i + 4] = clampi(((int)e.f[1] >> (i * 4)) & 15, 1, MAXLVL); }
  d.king = clampi((int)e.f[2], 1, MAXLVL); d.got = true;
}
static void netSendQuit() { netEvent(EV_QUIT, 0, 0); }
static int netSendPlay(int slot, float x, float z) { netEvent(EV_PLAY, slot, 0, x, z, 0, 0); sfx(SFX_PLAY); return 1; }
static void beginNetBattle() {
  memset(&BI, 0, sizeof BI); BI.net = true; BI.vsBot = false; snprintf(BI.enemy, sizeof BI.enemy, "ДРУГ");
  int my[8]; for (int i = 0; i < 8; i++) my[i] = pf.lvl[pf.deck[i]];
  if (netDuo) {                                                   // 2 на 2: хост и друг в одной команде против двух ботов
    setupDuo(netMode == NET_HOST ? 0 : 2, false, netSeed, netArena, peerDeck.deck, peerDeck.lvl, peerDeck.king);
    BI.net = true; if (netMode == NET_CLIENT) nBots = 0; }
  else if (netMode == NET_HOST) { BI.myTeam = 0; BI.mySide = 0; BI.mateSide = -1; startBattle(pf.deck, my, pf.kingLvl, peerDeck.deck, peerDeck.lvl, peerDeck.king, false, netArena, netSeed); nBots = 0; }
  else { BI.myTeam = 1; BI.mySide = 1; BI.mateSide = -1; startBattle(peerDeck.deck, peerDeck.lvl, peerDeck.king, pf.deck, my, pf.kingLvl, false, netArena, netSeed); nBots = 0; }
  viewTeam = BI.myTeam; selCard = -1; dragging = false; netStarted = true; gs = GS_BATTLE; netSnapAge = 0; previewDirty = true;
}
// хост -> друг: состояние боя
static void wI16f(PBuf &b, float v, float k) { int i = (int)lroundf(v * k); wU16(b, clampi(i, -32768, 32767) & 0xFFFF); }
static void netSendState() {
  PBuf b; wHead(b, PK_STATE, 3);
  wF(b, B.t); wU8(b, (B.overtime ? 1 : 0) | (B.side[0].kingActive ? 2 : 0) | (B.side[1].kingActive ? 4 : 0) | (B.over ? 8 : 0));
  Side &s = B.side[netClientSide()]; wU8(b, (int)(s.elixir * 20)); for (int i = 0; i < 4; i++) wU8(b, s.hand[i]); for (int i = 0; i < 4; i++) wU8(b, s.q[i]);
  wU8(b, B.side[0].crowns | (B.side[1].crowns << 4)); wU8(b, (int)(B.side[0].elixir * 20));      // + эликсир хоста (напарника в 2 на 2)
  int idx[90], n = 0; for (size_t i = 0; i < B.units.size() && n < 70; i++) if (UT(B.units[i]).kind == UT_TOWER) idx[n++] = (int)i;
  for (size_t i = 0; i < B.units.size() && n < 70; i++) if (UT(B.units[i]).kind != UT_TOWER && B.units[i].alive) idx[n++] = (int)i;
  wU8(b, n);
  for (int k = 0; k < n; k++) { const Unit &u = B.units[idx[k]];
    wU16(b, u.id & 0xFFFF); wU8(b, u.type); wU8(b, u.team | (u.alive ? 2 : 0) | (u.frozenT > 0 || u.slowT > 0 ? 4 : 0) | (u.charging ? 8 : 0) | (u.attacking ? 16 : 0) | (u.moving ? 32 : 0) | (u.flash > 0 ? 64 : 0) | (u.shield > 0 ? 128 : 0));
    unsigned xx = (unsigned)clampi((int)lroundf(u.x * 100), 0, 4095), zz = (unsigned)clampi((int)lroundf(u.z * 100), 0, 4095);   // 12 + 12 бит
    wU8(b, xx & 255); wU8(b, ((xx >> 8) & 15) | ((zz & 15) << 4)); wU8(b, zz >> 4);
    wU16(b, clampi((int)ceilf(u.hp), 0, 65535)); wU8(b, (u.lvl & 15) | ((int)(clampf(u.deployT, 0, 1) * 15) << 4));
    wU8(b, (int)(fmodf(u.face / 6.2831853f + 10.0f, 1.0f) * 255)); wU8(b, (int)(clampf(u.atkAnim, 0, 1) * 255)); }
  int np = imin(12, (int)B.projs.size()); wU8(b, np);
  for (int k = 0; k < np; k++) { const Proj &p = B.projs[k]; wU8(b, p.kind); wI16f(b, p.x, 100); wI16f(b, p.z, 100); wI16f(b, p.y, 100); wI16f(b, p.sx, 50); wI16f(b, p.sz, 50); wI16f(b, p.tx, 50); wI16f(b, p.tz, 50); }
  int ns = imin(5, (int)B.spells.size()); wU8(b, ns);
  for (int k = 0; k < ns; k++) { const Spell &sp = B.spells[k]; wU8(b, sp.kind | (sp.team << 4) | (sp.hit ? 0x80 : 0)); wI16f(b, sp.x, 100); wI16f(b, sp.z, 100); wI16f(b, sp.sx, 50); wI16f(b, sp.sz, 50); wU8(b, (int)(sp.r * 20)); wU16(b, (int)(sp.t * 1000)); wU16(b, (int)(sp.delay * 1000)); wU16(b, (int)(sp.time * 1000)); }
  int nf = 0, f0 = (int)B.fx.size() - 1; for (int k = f0; k >= 0 && nf < 6; k--) if (B.fx[k].t < 0.12f) nf++;
  wU8(b, nf); for (int k = f0; k >= 0 && nf > 0; k--) { const Fx &f = B.fx[k]; if (f.t >= 0.12f) continue; nf--; wU8(b, f.kind | (f.team << 4)); wI16f(b, f.x, 100); wI16f(b, f.z, 100); wI16f(b, f.y, 100); wU8(b, (int)(f.T * 50)); wU8(b, (int)(f.r * 20)); }
  netSend(b);
}
static float rI16f(PBuf &b, float k) { return rI16(b) / k; }
static void netApplyState(PBuf &b) {
  float t = rF(b); int fl = rU8(b); if (b.bad) return;
  B.t = t; B.overtime = fl & 1; B.side[0].kingActive = (fl & 2) != 0; B.side[1].kingActive = (fl & 4) != 0;
  Side &s = B.side[BI.mySide]; s.elixir = rU8(b) / 20.0f; for (int i = 0; i < 4; i++) s.hand[i] = clampi(rU8(b), 0, 7); for (int i = 0; i < 4; i++) s.q[i] = clampi(rU8(b), 0, 7);
  int cr = rU8(b); B.side[0].crowns = cr & 15; B.side[1].crowns = cr >> 4; float hostEl = rU8(b) / 20.0f; if (BI.mateSide >= 0) B.side[BI.mateSide].elixir = hostEl;
  std::vector<Unit> old = B.units; B.units.clear();
  int n = rU8(b);
  for (int k = 0; k < n && !b.bad; k++) { Unit u; memset(&u, 0, sizeof u);
    u.id = rU16(b); u.type = clampi(rU8(b), 0, NUNIT - 1); int f = rU8(b); u.team = f & 1; u.alive = (f & 2) != 0; u.frozenT = (f & 4) ? 1 : 0; u.charging = (f & 8) != 0; u.attacking = (f & 16) != 0; u.moving = (f & 32) != 0; u.shield = (f & 128) ? 1.0f : 0; u.flash = (f & 64) ? 0.1f : 0;
    int b0 = rU8(b), b1 = rU8(b), b2 = rU8(b); u.x = (b0 | ((b1 & 15) << 8)) / 100.0f; u.z = (((b1 >> 4) & 15) | (b2 << 4)) / 100.0f;
    u.hp = (float)rU16(b); int ld = rU8(b); u.lvl = clampi(ld & 15, 1, MAXLVL); u.deployT = (ld >> 4) / 15.0f;
    u.face = rU8(b) / 255.0f * 6.2831853f; u.atkAnim = rU8(b) / 255.0f;
    const UnitT &ut = UNITS[u.type]; float kk = ut.kind == UT_TOWER ? powf(1.08f, (float)(u.lvl - 1)) : lvlK(u.lvl); u.maxhp = ut.hp * kk; u.y = ut.air ? 2.2f : 0;
    u.px = u.x; u.pz = u.z; u.anim = 0;
    for (size_t j = 0; j < old.size(); j++) if (old[j].id == u.id) { u.px = old[j].x; u.pz = old[j].z; u.anim = old[j].anim; break; }
    B.units.push_back(u); }
  for (int tm = 0; tm < 2; tm++) for (size_t i = 0; i < B.units.size(); i++) { const Unit &u = B.units[i]; if (u.team != tm) continue; if (u.type == U_KING) B.side[tm].towerId[0] = u.id; if (u.type == U_PRINCESS) B.side[tm].towerId[u.x < 9 ? 1 : 2] = u.id; }
  B.projs.clear(); int np = rU8(b);
  for (int k = 0; k < np && !b.bad; k++) { Proj p; memset(&p, 0, sizeof p); p.kind = rU8(b); p.x = rI16f(b, 100); p.z = rI16f(b, 100); p.y = rI16f(b, 100); p.sx = rI16f(b, 50); p.sz = rI16f(b, 50); p.tx = rI16f(b, 50); p.tz = rI16f(b, 50); p.px = p.x; p.pz = p.z; p.py = p.y; B.projs.push_back(p); }
  B.spells.clear(); int ns = rU8(b);
  for (int k = 0; k < ns && !b.bad; k++) { Spell sp; memset(&sp, 0, sizeof sp); int kf = rU8(b); sp.kind = kf & 15; sp.team = (kf >> 4) & 1; sp.hit = (kf & 0x80) != 0; sp.x = rI16f(b, 100); sp.z = rI16f(b, 100); sp.sx = rI16f(b, 50); sp.sz = rI16f(b, 50); sp.r = rU8(b) / 20.0f; sp.t = rU16(b) / 1000.0f; sp.delay = rU16(b) / 1000.0f; sp.time = rU16(b) / 1000.0f; B.spells.push_back(sp); }
  int nf = rU8(b);
  for (int k = 0; k < nf && !b.bad; k++) { Fx f; memset(&f, 0, sizeof f); int kt = rU8(b); f.kind = kt & 15; f.team = (kt >> 4) & 1; f.x = rI16f(b, 100); f.z = rI16f(b, 100); f.y = rI16f(b, 100); f.T = rU8(b) / 50.0f; f.r = rU8(b) / 20.0f;
    bool dup = false; for (size_t j = 0; j < B.fx.size(); j++) if (B.fx[j].kind == f.kind && fabsf(B.fx[j].x - f.x) < 0.02f && fabsf(B.fx[j].z - f.z) < 0.02f && B.fx[j].t < 0.3f) dup = true;
    if (!dup && B.fx.size() < 160) { B.fx.push_back(f); if (f.kind == FX_BOOM) sfx(SFX_BOOM, 0.8f); else if (f.kind == FX_HIT) sfx(SFX_HIT, 0.5f); else if (f.kind == FX_TOWERDOWN) sfx(SFX_TOWER); else if (f.kind == FX_ZAP) sfx(SFX_ZAP); } }
  netSnapAge = 0;
}
static void netHandlePacket(int type, PBuf &b) {
  if (type == PK_JOIN && netMode == NET_HOST) { PBuf r; wHead(r, PK_WELCOME); netSend(r); if (!netDeckSent) { netSendDeck(); netDeckSent = true; } }
  if (type == PK_WELCOME && netMode == NET_CLIENT && !netDeckSent) { netSendDeck(); netDeckSent = true; }
  if (type == PK_STATE && netMode == NET_CLIENT && gs == GS_BATTLE) netApplyState(b);
}
static void netEvents() {
  for (size_t i = 0; i < netInbox.size(); i++) { const NetEv &e = netInbox[i];
    switch (e.type) {
      case EV_DECK: readDeck(e, peerDeck); if (netMode == NET_HOST) snprintf(netStatus, sizeof netStatus, "ДРУГ ГОТОВ К БОЮ"); else snprintf(netStatus, sizeof netStatus, "ПОДКЛЮЧЕНО. ЖДЁМ, КОГДА ДРУГ НАЧНЁТ"); break;
      case EV_START: if (netMode == NET_CLIENT && peerDeck.got) { netSeed = (unsigned)e.i0; netArena = clampi(e.i1 & 255, 0, NARENA - 1); netDuo = (e.i1 >> 8) & 1; beginNetBattle(); } break;
      case EV_PLAY: if (netMode == NET_HOST && gs == GS_BATTLE) { if (playCard(netClientSide(), clampi(e.i0, 0, 3), e.f[0], e.f[1])) {} else netEvent(EV_DENY, 0, 0); } break;
      case EV_DENY: say("ХОД НЕ ПРОШЁЛ"); break;
      case EV_END: if (netMode == NET_CLIENT && gs == GS_BATTLE) { B.over = true; B.winner = e.i0; B.side[0].crowns = e.i1 & 15; B.side[1].crowns = (e.i1 >> 4) & 15; } break;
      case EV_QUIT: if (gs == GS_BATTLE && !B.over) {
          if (netDuo && netMode == NET_HOST) { botInit(2, 2); if (nBots < 3) bots[nBots++] = bot; say("ДРУГ ВЫШЕЛ - ЗА НЕГО ИГРАЕТ БОТ", 4); }
          else { B.over = true; B.winner = netDuo ? -1 : BI.myTeam; say(netDuo ? "ДРУГ ВЫШЕЛ ИЗ БОЯ" : "ДРУГ СДАЛСЯ", 3); } } break;
    } }
  netInbox.clear();
}
static void netTick(float dt) {
  if (netMode == NET_OFF) return;
  netPoll(dt); netEvents();
  netSendT -= dt; netStateT -= dt; netDiscT -= dt; netSnapAge += dt;
  if (netMode == NET_CLIENT && !netPeer && !netStarted && netDiscT <= 0) { netDiscover(manualIP); netDiscT = 1.0f; }
  if (netIsHost() && netStateT <= 0) { netSendState(); netStateT = netSnapDT; netSendT = 0.2f; }
  if (netSendT <= 0 && (netPeer || netStarted)) { PBuf b; wHead(b, netMode == NET_CLIENT && !netStarted && !peerDeck.got ? PK_JOIN : PK_PING); netSend(b); netSendT = netOut.empty() ? 0.25f : 0.06f; }
}
// ---------------------------------------------------------------------
//  лобби
// ---------------------------------------------------------------------
static bool ipEdit = false;
static void startLobby() { netClose(); memset(&peerDeck, 0, sizeof peerDeck); netDeckSent = false; gs = GS_LOBBY; netStatus[0] = 0; ipEdit = false; sfx(SFX_CLICK); }
static void frameLobby(float dt) {
  (void)dt; layoutCol(); fillRect(0, 0, IW, IH, RGB(30, 38, 62)); char b[96];
  int y = safeTop() + fs * 14; drawTextSh(IW / 2, y, "БОЙ С ДРУГОМ", fs * 2, RGB(255, 230, 150), 1); y += fs * 22;
  drawText(IW / 2, y, "ОБА ТЕЛЕФОНА - В ОДНОЙ WI-FI СЕТИ", fs, RGB(200, 210, 230), 1); y += fs * 10;
  drawText(IW / 2, y, "ИЛИ ОДИН РАЗДАЁТ ТОЧКУ ДОСТУПА", fs, RGB(200, 210, 230), 1); y += fs * 16;
  int bw = colW * 3 / 4, bh = fs * 20;
  if (netMode == NET_OFF) {
    if (button(IW / 2 - bw / 2, y, bw, bh, "СОЗДАТЬ ИГРУ", true, RGB(60, 150, 70))) { netHost(); sfx(SFX_CLICK); }
    y += bh + fs * 6;
    if (button(IW / 2 - bw / 2, y, bw, bh, "ПОДКЛЮЧИТЬСЯ К ДРУГУ", true, RGB(70, 110, 190))) { netJoinMode(); netDiscT = 0; sfx(SFX_CLICK); }
    y += bh + fs * 6;
  } else {
    snprintf(b, sizeof b, "МОЙ АДРЕС: %s", netMyIP); drawTextSh(IW / 2, y, b, fs + 1, RGB(255, 255, 255), 1); y += fs * 14;
    drawTextSh(IW / 2, y, netStatus, fs, RGB(255, 220, 120), 1); y += fs * 14;
    if (netMode == NET_HOST) {
      if (button(IW / 2 - bw / 2, y, bw, fs * 14, netDuo ? "РЕЖИМ: 2 НА 2 ПРОТИВ БОТОВ" : "РЕЖИМ: 1 НА 1 ДРУГ ПРОТИВ ДРУГА", true, netDuo ? RGB(150, 80, 170) : RGB(70, 80, 110))) netDuo = !netDuo;
      y += fs * 18;
      if (peerDeck.got && netPeer) { if (button(IW / 2 - bw / 2, y, bw, bh, "НАЧАТЬ БОЙ", true, RGB(230, 170, 40))) {
          netSeed = (unsigned)rand() * 2654435761u + 7u; netArena = arenaOf(pf.trophies); netEvent(EV_START, (int)netSeed, netArena | (netDuo ? 256 : 0)); beginNetBattle(); return; } }
      else drawText(IW / 2, y + fs * 6, "ДРУГ НАЖИМАЕТ ПОДКЛЮЧИТЬСЯ К ДРУГУ", fs, RGB(200, 210, 230), 1);
      y += bh + fs * 8;
    } else {
      if (netFound && !netPeer) { snprintf(b, sizeof b, "ПОДКЛЮЧИТЬСЯ: %s", netFoundIP); if (button(IW / 2 - bw / 2, y, bw, bh, b, true, RGB(60, 150, 70))) { netJoin(); sfx(SFX_CLICK); } }
      y += bh + fs * 6;
      snprintf(b, sizeof b, "АДРЕС ДРУГА: %s_", manualIP); if (button(IW / 2 - bw / 2, y, bw, fs * 14, manualIP[0] || ipEdit ? b : "ВВЕСТИ АДРЕС ВРУЧНУЮ", true, RGB(70, 80, 110))) { ipEdit = !ipEdit; }
      y += fs * 18;
      if (ipEdit) { static const char *K[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "<"}; int kw = bw / 3 - fs * 2, kh = fs * 14;
        for (int k = 0; k < 12; k++) { int kx = IW / 2 - bw / 2 + (k % 3) * (kw + fs * 3), ky = y + (k / 3) * (kh + fs * 2);
          if (button(kx, ky, kw, kh, K[k], true, RGB(50, 60, 90))) { int l = (int)strlen(manualIP); if (K[k][0] == '<') { if (l) manualIP[l - 1] = 0; } else if (l < 15) { manualIP[l] = K[k][0]; manualIP[l + 1] = 0; } netFound = false; netDiscT = 0; } }
        y += 4 * (kh + fs * 2) + fs * 4; }
    }
  }
  if (button(IW / 2 - bw / 2, IH - fs * 30, bw, fs * 16, "НАЗАД", true, RGB(90, 90, 110))) { netClose(); gs = GS_MENU; sfx(SFX_CLICK); }
}

// =====================================================================
//  БИТВА БАШЕН: звук, кадр боя, ввод, главный цикл
// =====================================================================
// ---- звук: несколько голосов, синтез на лету ----
struct Voice { float f0, f1, t, dur, vol, ph; int noise; };
static Voice voices[12]; static volatile int vHead = 0; static bool audioOk = false;
static void audioCb(void *, Uint8 *st, int len) {
  short *o = (short *)st; int n = len / 2; static unsigned rs = 777u;
  for (int i = 0; i < n; i++) { float v = 0;
    for (int k = 0; k < 12; k++) { Voice &vc = voices[k]; if (vc.dur <= 0 || vc.t >= vc.dur) continue; float k01 = vc.t / vc.dur, env = (1 - k01) * (1 - k01) * vc.vol, f = vc.f0 + (vc.f1 - vc.f0) * k01;
      if (vc.noise) { rs = rs * 1664525u + 1013904223u; float nz = ((rs >> 9) & 0xFFFF) / 32768.0f - 1.0f; vc.ph += (nz - vc.ph) * (vc.noise == 2 ? 0.08f : 0.35f); v += vc.ph * env; }
      else { vc.ph += f / 22050.0f; if (vc.ph > 1) vc.ph -= 1; v += (vc.ph < 0.5f ? 1.0f : -1.0f) * 0.35f * env + sinf(vc.ph * 6.2832f) * 0.5f * env; }
      vc.t += 1.0f / 22050.0f; }
    if (v > 1) v = 1; if (v < -1) v = -1; o[i] = (short)(v * 14000); }
}
static void audioInit() {
  if (audioOk) return; SDL_InitSubSystem(SDL_INIT_AUDIO);
  SDL_AudioSpec want; memset(&want, 0, sizeof want); want.freq = 22050; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audioCb;
  if (SDL_OpenAudio(&want, NULL) == 0) { audioOk = true; SDL_PauseAudio(0); }
}
static void voice(float f0, float f1, float dur, float vol, int noise) { if (!audioOk || !pf.soundOn) return; int k = vHead; vHead = (vHead + 1) % 12; Voice v; v.f0 = f0; v.f1 = f1; v.t = 0; v.dur = dur; v.vol = vol; v.ph = 0; v.noise = noise; voices[k] = v; }
static float sfxLast[16];
static void sfx(int kind, float vol) {
  if (kind < 0 || kind >= 16 || gTime - sfxLast[kind] < 0.05f) return; sfxLast[kind] = gTime;
  switch (kind) {
    case SFX_CLICK: voice(900, 700, 0.05f, 0.35f * vol, 0); break;
    case SFX_CARD: voice(600, 900, 0.06f, 0.3f * vol, 0); break;
    case SFX_PLAY: voice(300, 520, 0.18f, 0.5f * vol, 0); voice(0, 0, 0.12f, 0.25f * vol, 1); break;
    case SFX_HIT: voice(0, 0, 0.07f, 0.35f * vol, 1); break;
    case SFX_SHOOT: voice(1400, 500, 0.06f, 0.18f * vol, 0); break;
    case SFX_BOOM: voice(0, 0, 0.45f, 0.8f * vol, 2); voice(120, 40, 0.35f, 0.5f * vol, 0); break;
    case SFX_TOWER: voice(0, 0, 0.9f, 0.9f * vol, 2); voice(90, 30, 0.8f, 0.6f * vol, 0); break;
    case SFX_ZAP: voice(0, 0, 0.2f, 0.6f * vol, 1); voice(2000, 300, 0.15f, 0.3f * vol, 0); break;
    case SFX_WIN: voice(520, 520, 0.15f, 0.5f, 0); voice(660, 660, 0.3f, 0.4f, 0); voice(780, 1040, 0.5f, 0.4f, 0); break;
    case SFX_LOSE: voice(400, 200, 0.6f, 0.5f, 0); break;
    case SFX_CHEST: voice(500, 1000, 0.25f, 0.45f, 0); voice(750, 1500, 0.3f, 0.3f, 0); break;
    case SFX_DENY: voice(220, 180, 0.15f, 0.4f, 0); break;
  }
}

// ---- кадр боя ----
static float endT = 0;
static void frameBattle(float dt) {
  battleLayout(); setupArenaCam(topH, arenaY1);
  if (netIsClient()) { simAlpha = clampf(netSnapAge / netSnapDT, 0, 1); for (size_t i = 0; i < B.fx.size(); i++) B.fx[i].t += dt;
    B.fx.erase(std::remove_if(B.fx.begin(), B.fx.end(), [](const Fx &f){ return f.t >= f.T; }), B.fx.end());
    if (!B.over) { float r = (doubleElixir() || B.overtime ? 2.0f : 1.0f) / ELIX_T; for (int s2 = 0; s2 < B.nSides; s2++) B.side[s2].elixir = fminv(10.0f, B.side[s2].elixir + r * dt); }
    for (size_t i = 0; i < B.units.size(); i++) if (B.units[i].moving || B.units[i].attacking) B.units[i].anim += dt * (UT(B.units[i]).speed * 6.0f + 2.0f); }
  else { if (BI.vsBot && !B.over) for (int i = 0; i < nBots; i++) { bot = bots[i]; botThink(dt); bots[i] = bot; } bool was = B.over; simAlpha = simRun(dt);
    if (netIsHost() && B.over && !was) netEvent(EV_END, B.winner, B.side[0].crowns | (B.side[1].crowns << 4)); }
  // звуки по событиям боя
  for (size_t i = 0; i < B.ev.size(); i++) { const BEv &e = B.ev[i];
    switch (e.type) { case BE_HIT: sfx(SFX_HIT, 0.6f); break; case BE_SHOOT: sfx(SFX_SHOOT, 0.5f); break; case BE_BOOM: sfx(e.a == 2 ? SFX_ZAP : SFX_BOOM); break; case BE_TOWER: sfx(SFX_TOWER); break; case BE_PLAY: if (e.team != BI.myTeam) sfx(SFX_PLAY, 0.5f); break; } }
  B.ev.clear();
  if (msgT > 0) msgT -= dt;
  battleInput();
  int gc = ghostCard(); bool ok = gc >= 0 && ghostOn && canPlace(BI.myTeam, gc, ghostX, ghostZ);
  setClipRect(0, 0, IW, IH);
  drawBattle3D(simAlpha, selCard >= 0, gc, ghostX, ghostZ, ok);
  drawBattleHud();
  if (B.over) { endT += dt; bool win = B.winner == BI.myTeam; const char *t = B.winner < 0 ? "НИЧЬЯ" : (win ? "ПОБЕДА!" : "ПОРАЖЕНИЕ");
    drawTextSh(IW / 2, IH / 3, t, fs * 3, B.winner < 0 ? RGB(230, 230, 230) : (win ? RGB(255, 220, 80) : RGB(255, 100, 90)), 1);
    if (endT > 2.5f && !BI.done) { finishBattle(); endT = 0; if (BI.net) { /* сеть оставляем: можно сыграть ещё раз */ netStarted = false; } } }
  else endT = 0;
  if (BI.net && !netPeer && netLastRecv > 8 && !B.over) { B.over = true; B.winner = -1; say("СВЯЗЬ С ДРУГОМ ПОТЕРЯНА", 4); }
}

// ---- ввод: один палец ----
static long long ptrId = -99;
static void fingerDown(long long id, int x, int y) { if (P.down) return; ptrId = id; P.down = true; P.pressed = true; P.x = P.sx = x; P.y = P.sy = y; }
static void fingerMove(long long id, int x, int y) { if (!P.down || id != ptrId) return; P.dx += x - P.x; P.dy += y - P.y; P.x = x; P.y = y; }
static void fingerUp(long long id, int x, int y) { if (!P.down || id != ptrId) return; P.down = false; P.x = x; P.y = y; P.cx = x; P.cy = y; P.clicked = true; }
static void goBack(bool &running) {
  if (gs == GS_START) running = false;
  else if (gs == GS_MENU) { if (infoCard >= 0) infoCard = -1; else if (showTop) showTop = false; else if (menuTab != 2) menuTab = 2; else { saveProfile(); if (!devMode) realPf = pf; gs = GS_START; } }
  else if (gs == GS_LOBBY) { netClose(); gs = GS_MENU; }
  else if (gs == GS_CHEST) gs = GS_MENU;
  else if (gs == GS_RESULT) gs = GS_MENU;
}
static float fpsShow = 0, fpsAcc = 0; static int fpsN = 0;

int main(int argc, char **argv) {
  (void)argc; (void)argv;
#if defined(__ANDROID__) && defined(SDL_MAJOR_VERSION) && SDL_MAJOR_VERSION >= 2
  { const char *ip = SDL_AndroidGetInternalStoragePath(); if (ip && *ip) snprintf(gBase, sizeof gBase, "%s/", ip); }
#endif
  { FILE *g = fopen(pth("bashni_gl.cfg"), "r"); if (g) { int v = 1; if (fscanf(g, "%d", &v) == 1) gWantGL = v != 0; fclose(g); } }
  SDL_SetHint("SDL_ANDROID_TRAP_BACK_BUTTON", "1");
  if (!gfxInit()) return 1;
  rotMode = false; LW = SW; LH = SH;
  computeSize(); if (!allocBuffers()) return 1;
  srand((unsigned)time(NULL)); balanceInit();
  if (!loadProfile()) { FILE *old = fopen(pth("bashni.sav"), "rb"); if (old) { fclose(old); rename(pth("bashni.sav"), pth("bashni.broken")); }   // нечитаемый файл не затираем
    newProfile(); saveProfile(); }
  dailyRefresh(); realPf = pf; gs = GS_START; tourLoad();
  genThumbs();
  audioInit();
  memset(&P, 0, sizeof P);
  bool running = true; Uint32 last = SDL_GetTicks();
  while (running) {
    Uint32 now = SDL_GetTicks();
    float dt = clampf((now - last) / 1000.0f, 0.001f, 0.05f); last = now; gTime += dt;
    if (gfxCheckResize()) { rotMode = false; LW = SW; LH = SH; computeSize(); if (!allocBuffers()) break; genThumbs(); }
    P.pressed = false; P.clicked = false; P.dx = P.dy = 0;
#if SDL_MODE == 2
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      int mx, my;
      switch (ev.type) {
        case SDL_QUIT: running = false; break;
        case SDL_APP_WILLENTERBACKGROUND: case SDL_APP_TERMINATING: case SDL_APP_LOWMEMORY: saveProfile(); break;
        case SDL_KEYDOWN: if (ev.key.keysym.sym == SDLK_ESCAPE
#if defined(__ANDROID__)
          || ev.key.keysym.sym == SDLK_AC_BACK
#endif
          ) goBack(running); break;
        case SDL_FINGERDOWN: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerDown((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_FINGERMOTION: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerMove((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_FINGERUP: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerUp((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_MOUSEBUTTONDOWN: if (ev.button.which != SDL_TOUCH_MOUSEID) { mapPt(ev.button.x, ev.button.y, mx, my); fingerDown(-1, mx, my); } break;
        case SDL_MOUSEMOTION: if (ev.motion.which != SDL_TOUCH_MOUSEID) { mapPt(ev.motion.x, ev.motion.y, mx, my); fingerMove(-1, mx, my); } break;
        case SDL_MOUSEBUTTONUP: if (ev.button.which != SDL_TOUCH_MOUSEID) { mapPt(ev.button.x, ev.button.y, mx, my); fingerUp(-1, mx, my); } break;
        default: break;
      }
    }
#else
    Ev e;
    while (pollEv(e)) {
      if (e.type == 0) running = false;
      else if (e.type == 4) goBack(running);
      else if (e.type == 5) saveProfile();
      else { int mx, my; mapPt(e.x, e.y, mx, my); if (e.type == 1) fingerDown(-1, mx, my); else if (e.type == 3) fingerMove(-1, mx, my); else fingerUp(-1, mx, my); }
    }
#endif
    netTick(dt);
    if (msgT > 0 && gs != GS_BATTLE) msgT -= dt;
    if (gs == GS_START) frameStart(); else if (gs == GS_MENU) frameMenu(); else if (gs == GS_BATTLE) frameBattle(dt); else if (gs == GS_RESULT) frameResult(); else if (gs == GS_CHEST) frameChest(dt); else frameLobby(dt);
    if (fpsOn) { char b[16]; snprintf(b, sizeof b, "%d", (int)(fpsShow + 0.5f)); drawText(IW - fs * 2, IH - fs * 9, b, imax(1, fs - 1), RGB(150, 160, 180), 2); }
    gfxPresent();
    Uint32 spent = SDL_GetTicks() - now; fpsAcc += dt; fpsN++; if (fpsAcc >= 1.0f) { fpsShow = fpsN / fpsAcc; fpsAcc = 0; fpsN = 0; }
    if (spent < 16) SDL_Delay(16 - spent);
  }
  saveProfile(); netClose();
  gfxQuit();
  return 0;
}
