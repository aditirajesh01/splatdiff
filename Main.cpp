#include <OPPCH.h>

#include "App.hpp"
#include "GUI.hpp"
#include "Window.hpp"

// Global variables
const char* WINDOW_TITLE = "SplatDiff Viewer";

// Write the current back buffer to a binary PPM (used for headless verification and report figures).
static void saveScreenshot(const std::string& path, int w, int h) {
  std::vector<unsigned char> px(w * h * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = h - 1; y >= 0; --y) fwrite(&px[y * w * 3], 1, w * 3, f);  // flip vertically
  fclose(f);
  std::cout << "Saved screenshot to " << path << std::endl;
}

int main(int argc, char* args[]) {
  // usage: splatdiff [scanA.ply] [--scan-b scanB.ply] [--show-b] [--detect] [--no-box] [--camera ex ey ez lx ly lz]
  // [--screenshot out.ppm]
  std::string plyPath;
  std::string scanBPath;
  bool showB = false;
  bool detect = false;
  bool noBox = false;
  bool haveCamera = false;
  float cameraArgs[6] = {};
  std::string screenshotPath;
  for (int i = 1; i < argc; ++i) {
    std::string a = args[i];
    if (a == "--screenshot" && i + 1 < argc) {
      screenshotPath = args[++i];
    } else if (a == "--scan-b" && i + 1 < argc) {
      scanBPath = args[++i];
    } else if (a == "--show-b") {
      showB = true;
    } else if (a == "--detect") {
      detect = true;
    } else if (a == "--no-box") {
      noBox = true;
    } else if (a == "--camera" && i + 6 < argc) {  // eye x y z, look-at x y z
      for (int k = 0; k < 6; ++k) cameraArgs[k] = std::strtof(args[++i], nullptr);
      haveCamera = true;
    } else {
      plyPath = a;
    }
  }

  // init SDL
  SDL_Window* window = nullptr;
  SDL_GLContext context;
  // default screen size
  int* screen_width = new int(1024);
  int* screen_height = new int(768);

  if (!initWindow(&window, &context, WINDOW_TITLE, *screen_width, *screen_height)) {
    std::cerr << "Failed to initialize!" << std::endl;
    exit(1);
  }

  // init glew
  glewExperimental = GL_TRUE;
  GLenum glewError = glewInit();
  if (glewError != GLEW_OK) {
    std::cerr << "Error initializing GLEW! " << glewGetErrorString(glewError) << std::endl;
    exit(1);
  }
  // SDL_GL_SetSwapInterval(0);  // Disable VSync

  App* app = new App(*screen_width, *screen_height, plyPath);
  if (!scanBPath.empty()) app->loadScanB(scanBPath);
  if (showB) app->setShowB(true);
  if (detect) app->runDetection();
  if (noBox) app->setSelectionBoxVisible(false);
  if (haveCamera)
    app->setCamera(glm::vec3(cameraArgs[0], cameraArgs[1], cameraArgs[2]),
                   glm::vec3(cameraArgs[3], cameraArgs[4], cameraArgs[5]));
  GUI gui(window, context, app);

  // main func
  bool quit = false;
  SDL_Event e;
  int frame = 0;

  while (!quit) {
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q)) {
        quit = true;
      }
      if (!gui.isMouseOverGUI()) app->OnEvent(e);
      ImGui_ImplSDL2_ProcessEvent(&e);  // let imgui interact with SDL
    }

    app->OnRender();
    gui.draw();

    if (!screenshotPath.empty() && ++frame == 5) {
      int dw, dh;
      SDL_GL_GetDrawableSize(window, &dw, &dh);
      saveScreenshot(screenshotPath, dw, dh);
      quit = true;
    }

    SDL_GL_SwapWindow(window);
  }

  gui.shutdown();

  close(window, context);

  return 0;
}
