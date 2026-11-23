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
  // usage: splatdiff [scanA.ply] [--scan-b scanB.ply] [--show-b] [--detect] [--split] [--volume] [--volume-only]
  // [--dim] [--slice axis pos] [--stereo 1-4] [--ipd d] [--convergence z] [--toe-in] [--swap-eyes] [--hide-ui]
  // [--experiments] [--show-case name]
  // [--no-overlay] [--no-box] [--camera ex ey ez lx ly lz]
  // [--screenshot out.ppm]
  std::string plyPath;
  std::string scanBPath;
  bool showB = false;
  bool detect = false;
  bool noBox = false;
  bool slice = false;
  bool hideUi = false;
  bool experiments = false;
  std::string showCase;
  int stereoMode = 0;
  float ipd = -1.0f, convergence = -1.0f;
  bool toeIn = false, swapEyes = false;
  int sliceAxis = 1;
  float slicePos = 0.3f;
  bool split = false, volume = false, volumeOnly = false, dim = false, overlay = true;
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
    } else if (a == "--split") {
      split = true;
    } else if (a == "--volume") {
      volume = true;
    } else if (a == "--volume-only") {
      volumeOnly = true;
    } else if (a == "--slice" && i + 2 < argc) {  // axis (0/1/2), position 0..1
      sliceAxis = std::atoi(args[++i]);
      slicePos = std::strtof(args[++i], nullptr);
      slice = true;
    } else if (a == "--stereo" && i + 1 < argc) {  // 1-4, see StereoMode
      stereoMode = std::atoi(args[++i]);
    } else if (a == "--ipd" && i + 1 < argc) {
      ipd = std::strtof(args[++i], nullptr);
    } else if (a == "--convergence" && i + 1 < argc) {
      convergence = std::strtof(args[++i], nullptr);
    } else if (a == "--toe-in") {
      toeIn = true;
    } else if (a == "--swap-eyes") {
      swapEyes = true;
    } else if (a == "--experiments") {
      experiments = true;
    } else if (a == "--show-case" && i + 1 < argc) {
      experiments = true;
      showCase = args[++i];
    } else if (a == "--hide-ui") {
      hideUi = true;
    } else if (a == "--dim") {
      dim = true;
    } else if (a == "--no-overlay") {
      overlay = false;
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
  app->setViewOptions(overlay, dim, split, volume, volumeOnly);
  if (slice) app->setSliceView(true, sliceAxis, slicePos);
  if (hideUi) app->toggleUi();
  if (experiments) app->runExperimentCases();
  if (!showCase.empty()) app->showExperimentCase(showCase);
  {
    StereoSettings& st = app->stereoSettings();
    st.mode = StereoMode(stereoMode);
    if (ipd >= 0) st.eyeSeparation = ipd;
    if (convergence > 0) st.convergence = convergence;
    st.toeIn = toeIn;
    st.swapEyes = swapEyes;
  }
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
      if (e.type == SDL_QUIT ||
          (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q && !ImGui::GetIO().WantTextInput)) {
        quit = true;
      }
      // keys typed into an ImGui text field must not also drive the camera / shortcuts
      bool isKey = e.type == SDL_KEYDOWN || e.type == SDL_KEYUP;
      bool typing = isKey && ImGui::GetIO().WantTextInput;
      if (!gui.isMouseOverGUI() && !typing) app->OnEvent(e);
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
