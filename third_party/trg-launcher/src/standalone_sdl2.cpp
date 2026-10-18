#include "trg/standalone.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cstdio>

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

namespace trg {
namespace {

// The few GL calls made outside ImGui's backend, loaded through SDL so the
// game need not link OpenGL itself.
struct Gl {
  void(APIENTRY* Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
  void(APIENTRY* ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
  void(APIENTRY* Clear)(GLbitfield) = nullptr;
  void(APIENTRY* GenTextures)(GLsizei, GLuint*) = nullptr;
  void(APIENTRY* DeleteTextures)(GLsizei, const GLuint*) = nullptr;
  void(APIENTRY* BindTexture)(GLenum, GLuint) = nullptr;
  void(APIENTRY* TexParameteri)(GLenum, GLenum, GLint) = nullptr;
  void(APIENTRY* PixelStorei)(GLenum, GLint) = nullptr;
  void(APIENTRY* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;

  template <typename F>
  static void Load(F& fn, const char* name) {
    fn = reinterpret_cast<F>(SDL_GL_GetProcAddress(name));
  }
  bool Init() {
    Load(Viewport, "glViewport");
    Load(ClearColor, "glClearColor");
    Load(Clear, "glClear");
    Load(GenTextures, "glGenTextures");
    Load(DeleteTextures, "glDeleteTextures");
    Load(BindTexture, "glBindTexture");
    Load(TexParameteri, "glTexParameteri");
    Load(PixelStorei, "glPixelStorei");
    Load(TexImage2D, "glTexImage2D");
    return Viewport && ClearColor && Clear;
  }
};
Gl gl;
bool gl_ready = false;

}  // namespace

ImTextureID CreateTextureRGBA(int width, int height, const void* rgba) {
  if (!gl_ready || !gl.GenTextures || !rgba || width <= 0 || height <= 0) return ImTextureID{};
  GLuint tex = 0;
  gl.GenTextures(1, &tex);
  gl.BindTexture(GL_TEXTURE_2D, tex);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F /* GL_CLAMP_TO_EDGE */);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
  gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  return ImTextureID(tex);
}

void DestroyTexture(ImTextureID texture) {
  if (!gl_ready || !gl.DeleteTextures || texture == ImTextureID{}) return;
  const GLuint tex = GLuint(texture);
  gl.DeleteTextures(1, &tex);
}

Result RunStandalone(Launcher& launcher, const WindowOptions& options, const StandaloneHooks& hooks) {
#ifdef SDL_HINT_WINDOWS_DPI_SCALING
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "[trg-launcher] SDL_Init failed: %s; starting the game\n", SDL_GetError());
    return Result::kPlay;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_Rect usable = {0, 0, 1280, 800};
  SDL_GetDisplayUsableBounds(0, &usable);
  const int ww = std::min(options.width, int(float(usable.w) * 0.9f));
  const int wh = std::min(options.height, int(float(usable.h) * 0.9f));
  SDL_Window* win = SDL_CreateWindow(options.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : nullptr;
  if (!ctx || !gl.Init()) {
    std::fprintf(stderr, "[trg-launcher] no OpenGL 3.3 window: %s; starting the game\n", SDL_GetError());
    if (ctx) SDL_GL_DeleteContext(ctx);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    return Result::kPlay;
  }
  gl_ready = true;
  SDL_SetWindowMinimumSize(win, std::min(options.min_width, ww), std::min(options.min_height, wh));
  SDL_GL_MakeCurrent(win, ctx);
  SDL_GL_SetSwapInterval(options.vsync ? 1 : 0);

  IMGUI_CHECKVERSION();
  ImGuiContext* previous = ImGui::GetCurrentContext();
  ImGuiContext* imgui = ImGui::CreateContext();
  ImGui::SetCurrentContext(imgui);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  if (options.load_system_fonts && !launcher.config().fonts.regular)
    launcher.config().fonts = LoadSystemFonts(launcher.config().fonts.size > 0 ? launcher.config().fonts.size : 18.0f);
  ImGui_ImplSDL2_InitForOpenGL(win, ctx);
  ImGui_ImplOpenGL3_Init("#version 330 core");
  if (hooks.on_start) hooks.on_start(launcher);

  const ImVec4 clear = launcher.config().theme.bg;
  Result result = Result::kNone;
  double suppress_clicks_until = 0.0;
  while (result == Result::kNone) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      // A native file dialog's closing click must not land on the launcher.
      if ((ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) && ImGui::GetTime() < suppress_clicks_until)
        continue;
      ImGui_ImplSDL2_ProcessEvent(&ev);
      if (ev.type == SDL_DROPFILE) {
        launcher.DropFile(ev.drop.file);
        SDL_free(ev.drop.file);
      }
      if (ev.type == SDL_QUIT) result = Result::kQuit;
      if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
          ev.window.windowID == SDL_GetWindowID(win))
        result = Result::kQuit;
      if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
        suppress_clicks_until = ImGui::GetTime() + 0.25;
    }
    if (result != Result::kNone) break;
    if (SDL_GetWindowFlags(win) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(30);
      continue;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    result = launcher.Frame();
    ImGui::Render();
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    gl.Viewport(0, 0, dw, dh);
    gl.ClearColor(clear.x, clear.y, clear.z, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (hooks.after_render) hooks.after_render(launcher, dw, dh);
    SDL_GL_SwapWindow(win);
  }

  if (hooks.on_stop) hooks.on_stop(launcher);
  launcher.config().fonts = Fonts{nullptr, nullptr, nullptr, launcher.config().fonts.size};  // owned by the atlas
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext(imgui);
  ImGui::SetCurrentContext(previous);
  gl_ready = false;
  SDL_GL_DeleteContext(ctx);
  SDL_DestroyWindow(win);
  SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER);
  return result;
}

}  // namespace trg
