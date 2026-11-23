#pragma once

#include <SDL2/SDL.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string>
#include <vector>

#include "Camera.hpp"
#include "ChangeDetect.hpp"
#include "DataObject.hpp"
#include "Experiments.hpp"
#include "SceneEdit.hpp"
#include "ShaderClass.hpp"
#include "Stereo.hpp"
#include "VolumeView.hpp"

class App {
 public:
  App(const float screenWidth, const float screenHeight, const std::string& initialPlyPath = "");
  ~App();

  void OnEvent(SDL_Event& event);
  void OnRender();
  void OnImGuiRender();
  void OnSplatDiffImGui();  // SplatDiff panel: scans A/B and the scan-B editor

  void loadScanB(const std::string& path);
  void setShowB(bool show);
  bool isShowingB() const { return showB; }
  void runDetection();
  bool isSplitView() const { return splitView; }
  float getSplitFraction() const { return dividerFraction; }
  // view options, also settable from the command line
  void setSliceView(bool on, int axis, float position) {
    showSlice = on, sliceAxis = axis, slicePosition = position;
    if (detection && volumeView) volumeView->updateSlice(*detection, sliceAxis, slicePosition);
  }
  StereoSettings& stereoSettings() { return stereo; }
  void runExperimentCases();
  void showExperimentCase(size_t index);
  void showExperimentCase(const std::string& name) {
    for (size_t i = 0; i < expCases.size(); ++i)
      if (expCases[i].name == name) showExperimentCase(i);
  }
  bool isUiHidden() const { return uiHidden; }
  void toggleUi() { uiHidden = !uiHidden; }
  void setViewOptions(bool overlayOn, bool dim, bool split, bool volume, bool volumeOnlyOn) {
    overlay = overlayOn, dimUnchanged = dim, splitView = split, showVolume = volume, volumeOnly = volumeOnlyOn;
    sortVisible();
  }
  void setCamera(const glm::vec3& position, const glm::vec3& lookAt);
  void setSelectionBoxVisible(bool visible) { showSelectionBox = visible; }

 private:
  std::string modelPath;
  float width;
  float height;
  float tan_fovx;
  float tan_fovy;
  float focal_x;
  float focal_y;
  const float C0 = 0.28209479f;
  float scaleFactor = 1.0f;

  // Rotation Gizmo & Model Transform
  glm::mat4 splatModelMatrix = glm::mat4(1.0f);
  bool isRotating = false;
  glm::vec2 lastMousePos;
  const float gizmoSize = 150.0f;

  std::unique_ptr<Mesh> boxMesh;
  std::unique_ptr<Mesh> sphereMesh;
  std::unique_ptr<Mesh> coneMesh;

  // Gizmo Interaction State
  enum class GizmoMode {
    NONE,
    TRANSLATE_X,
    TRANSLATE_Y,
    TRANSLATE_Z,
    SCALE_PX,
    SCALE_NX,
    SCALE_PY,
    SCALE_NY,
    SCALE_PZ,
    SCALE_NZ
  };
  GizmoMode currentGizmoMode = GizmoMode::NONE;
  glm::vec3 dragStartWorldPos;
  glm::vec3 dragStartBoxPos;
  glm::vec3 dragStartBoxSize;

  glm::vec3 getMouseRay(float x, float y);
  bool intersectRaySphere(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& sphereCenter,
                          float radius, float& t);

  glm::vec3 getArcballVector(float x, float y);

  bool showSelectionBox = true;
  float boxSize[3] = {1.0f, 1.0f, 1.0f};
  float boxPosition[3] = {0.0f, 0.0f, 0.0f};
  // SplatDiff: scan A (reference) and scan B (later scan). `splat` points at the one being displayed.
  std::unique_ptr<GaussianSplat> sceneA;
  std::unique_ptr<GaussianSplat> sceneB;
  GaussianSplat* splat = nullptr;
  bool showB = false;
  std::string scanBPath = "./assets/scanB.ply";
  std::vector<splatdiff::EditRecord> editLog;

  // scan-B editor parameters (ImGui)
  float moveTranslation[3] = {1.0f, 0.0f, 0.0f};
  float moveYawDeg = 0.0f;
  float duplicateOffset[3] = {0.0f, 0.0f, 1.5f};
  float colourGain[3] = {1.2f, 1.1f, 0.9f};
  float colourOffset = 0.0f;
  bool colourSelectionOnly = false;
  float jitterPos = 0.01f;
  float jitterScale = 0.05f;
  float splitFraction = 0.2f;
  float pruneFraction = 0.3f;
  float pruneMaxOpacity = 0.3f;
  int editSeed = 1;

  // change detection (milestone 2)
  splatdiff::DetectParams detectParams;
  std::unique_ptr<splatdiff::DetectResult> detection;
  splatdiff::Evaluation evaluation;
  bool haveEvaluation = false;
  bool autoDetect = true;
  bool showRegions = true;
  bool showGroundTruth = false;
  std::string benchmarkReport;
  std::unique_ptr<Mesh> edgeMesh;  // 12 box edges, drawn with GL_LINES

  // views (milestone 3)
  bool overlay = true;
  bool dimUnchanged = false;
  bool splitView = false;
  float dividerFraction = 0.5f;  // before/after divider, fraction of the window width
  bool draggingSplit = false;
  bool showVolume = false;
  bool volumeOnly = false;
  bool showSlice = false;
  int sliceAxis = 1;
  float slicePosition = 0.3f;
  std::unique_ptr<VolumeView> volumeView;
  // One rendered viewpoint: the normal camera, or one eye of the stereo pair.
  struct RenderView {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::ivec2 size{1, 1};  // viewport size in pixels
  };
  RenderView currentView;
  RenderView mainView() const;
  void applyView(Shader* shader) const;   // viewMatrix + camMatrix uniforms from currentView
  void renderScene(const RenderView& v);  // splats, change volume, region boxes
  void drawSplats(GaussianSplat* scene);

  // experiments (milestone 5)
  std::vector<splatdiff::ExperimentCase> expCases;
  std::vector<splatdiff::ExperimentRow> expRows;
  std::vector<splatdiff::SweepPoint> expTau, expThreads;
  int expSeed = 7;
  std::string expStatus;
  void runTauSweep();
  void runThreadSweep();
  void exportExperiments();

  // stereo / VR (milestone 4)
  StereoSettings stereo;
  bool uiHidden = false;  // H: clean screen for the VR / stereo demo
  std::unique_ptr<StereoRenderer> stereoRenderer;
  void sortVisible();
  void updateChangeTags();
  void drawBoxEdges(const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& colour);
  void runBenchmark();

  splatdiff::Region selectionRegion() const;
  void resetScanB();
  void onScanBEdited(const splatdiff::EditRecord& record);
  std::unique_ptr<Shader> boxShader;
  std::unique_ptr<Shader> splatShader;
  std::unique_ptr<Camera> camera;
  std::unique_ptr<CameraEventListener> listener;
  glm::mat4 getViewModelMatrix();
  std::unique_ptr<GaussianSplat> loadPlyFile(const std::string path);
  void showPathSelector();
};
