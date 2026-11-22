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
#include "SceneEdit.hpp"
#include "ShaderClass.hpp"

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
