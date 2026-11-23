#include "App.hpp"

#include <OPPCH.h>

#include <cfloat>
#include <filesystem>

#include "ImGuiFileDialog.h"

App::App(const float screenWidth, const float screenHeight, const std::string& initialPlyPath) {
  modelPath = initialPlyPath;
  width = screenWidth;
  height = screenHeight;
  tan_fovx = tan(glm::radians(45.0f) / 2.0f);
  tan_fovy = tan(glm::radians(45.0f) / 2.0f);
  focal_y = width / (2.0f * tan_fovy);
  focal_x = height / (2.0f * tan_fovx);
  glViewport(0, 0, screenWidth, screenHeight);

  splatShader =
      std::make_unique<Shader>("./shaders/geo_vert.glsl", "./shaders/geo_gert.glsl", "./shaders/geo_frag.glsl");

  sceneA = loadPlyFile(modelPath);
  resetScanB();
  splat = sceneA.get();

  glm::vec3 position = glm::vec3(5.0f, 3.0f, 0.0f);
  glm::vec3 orientation = glm::vec3(-0.7f, -0.6f, 0.0f);
  camera = std::make_unique<Camera>(screenWidth, screenHeight, position, orientation);
  listener = std::make_unique<GhostCameraListener>(camera.get());
  listener->onKeyUpCallback = [&](SDL_Keycode key) { sortVisible(); };
  listener->onMouseLeftReleaseCallback = [&]() { sortVisible(); };
  camera->setEventListener(listener.get());
  splat->sort(getViewModelMatrix());

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);

  // Box Mesh (24 vertices for flat shading)
  boxShader = std::make_unique<Shader>("./shaders/box_vert.glsl", "./shaders/box_frag.glsl");
  std::vector<Vertex> boxVertices = {// Front face
                                     {{-1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
                                     {{1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
                                     {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
                                     {{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
                                     // Back face
                                     {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}},
                                     {{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}},
                                     {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}},
                                     {{1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}},
                                     // Top face
                                     {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
                                     {{-1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
                                     {{1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
                                     {{1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
                                     // Bottom face
                                     {{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}},
                                     {{1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}},
                                     {{1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
                                     {{-1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
                                     // Right face
                                     {{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
                                     {{1.0f, 1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
                                     {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
                                     {{1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
                                     // Left face
                                     {{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}},
                                     {{-1.0f, -1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
                                     {{-1.0f, 1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
                                     {{-1.0f, 1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}}};
  std::vector<GLuint> boxIndices;
  for (int i = 0; i < 6; ++i) {
    boxIndices.push_back(i * 4 + 0);
    boxIndices.push_back(i * 4 + 1);
    boxIndices.push_back(i * 4 + 2);
    boxIndices.push_back(i * 4 + 2);
    boxIndices.push_back(i * 4 + 3);
    boxIndices.push_back(i * 4 + 0);
  }
  boxMesh = std::make_unique<Mesh>(boxVertices, boxIndices);

  // Sphere Mesh for Gizmo
  std::vector<Vertex> sphereVertices;
  std::vector<GLuint> sphereIndices;
  const int sectors = 20;
  const int stacks = 20;
  const float PI = 3.14159265f;
  for (int i = 0; i <= stacks; ++i) {
    float stackAngle = PI / 2 - i * PI / stacks;
    float xy = cosf(stackAngle);
    float z = sinf(stackAngle);
    for (int j = 0; j <= sectors; ++j) {
      float sectorAngle = j * 2 * PI / sectors;
      float x = xy * cosf(sectorAngle);
      float y = xy * sinf(sectorAngle);
      sphereVertices.push_back({{x, y, z}, {x, y, z}, {1.0f, 1.0f, 1.0f}});
    }
  }
  for (int i = 0; i < stacks; ++i) {
    int k1 = i * (sectors + 1);
    int k2 = k1 + sectors + 1;
    for (int j = 0; j < sectors; ++j, ++k1, ++k2) {
      if (i != 0) {
        sphereIndices.push_back(k1);
        sphereIndices.push_back(k2);
        sphereIndices.push_back(k1 + 1);
      }
      if (i != (stacks - 1)) {
        sphereIndices.push_back(k1 + 1);
        sphereIndices.push_back(k2);
        sphereIndices.push_back(k2 + 1);
      }
    }
  }
  sphereMesh = std::make_unique<Mesh>(sphereVertices, sphereIndices);

  // Cone Mesh for Arrows
  std::vector<Vertex> coneVertices;
  std::vector<GLuint> coneIndices;
  const int coneSectors = 20;
  coneVertices.push_back({{0, 0, 1.0f}, {0, 0, 1.0f}, {1.0f, 1.0f, 1.0f}});  // Tip
  for (int i = 0; i <= coneSectors; ++i) {
    float angle = i * 2 * PI / coneSectors;
    float x = cosf(angle);
    float y = sinf(angle);
    coneVertices.push_back({{x, y, 0}, {x, y, 0}, {1.0f, 1.0f, 1.0f}});
  }
  for (int i = 1; i <= coneSectors; ++i) {
    coneIndices.push_back(0);
    coneIndices.push_back(i);
    coneIndices.push_back(i + 1);
  }
  // Bottom cap
  int centerIdx = coneVertices.size();
  coneVertices.push_back({{0, 0, 0}, {0, 0, -1.0f}, {1.0f, 1.0f, 1.0f}});
  for (int i = 1; i <= coneSectors; ++i) {
    coneIndices.push_back(centerIdx);
    coneIndices.push_back(i + 1);
    coneIndices.push_back(i);
  }
  coneMesh = std::make_unique<Mesh>(coneVertices, coneIndices);

  // Box edges for change regions: 8 corners of [-1, 1]^3, 12 edges
  std::vector<Vertex> edgeVertices;
  for (int i = 0; i < 8; ++i) {
    glm::vec3 c((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
    edgeVertices.push_back({c, glm::vec3(0, 1, 0), glm::vec3(1)});  // normal faces the light: full brightness
  }
  std::vector<GLuint> edgeIndices = {0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 1, 3, 4, 6, 5, 7, 0, 4, 1, 5, 2, 6, 3, 7};
  edgeMesh = std::make_unique<Mesh>(edgeVertices, edgeIndices);
}
App::~App() {}

glm::vec3 App::getArcballVector(float x, float y) {
  // Map to [-1, 1]
  float mx = (x - (width - gizmoSize)) / gizmoSize * 2.0f - 1.0f;
  float my = (y - (height - gizmoSize)) / gizmoSize * 2.0f - 1.0f;
  my = -my;  // Invert Y for screen space

  glm::vec3 p = glm::vec3(mx, my, 0.0f);
  float mag2 = mx * mx + my * my;
  if (mag2 <= 1.0f) {
    p.z = sqrt(1.0f - mag2);
  } else {
    p = glm::normalize(p);
  }
  return p;
}

glm::vec3 App::getMouseRay(float x, float y) {
  float nx = (2.0f * x) / width - 1.0f;
  float ny = 1.0f - (2.0f * y) / height;
  glm::vec4 ray_clip = glm::vec4(nx, ny, -1.0f, 1.0f);
  glm::vec4 ray_eye = glm::inverse(camera->projMatrix) * ray_clip;
  ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0f, 0.0f);
  glm::vec3 ray_wor = glm::vec3(glm::inverse(camera->viewMatrix) * ray_eye);
  return glm::normalize(ray_wor);
}

bool App::intersectRaySphere(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& sphereCenter,
                             float radius, float& t) {
  glm::vec3 oc = rayOrigin - sphereCenter;
  float b = glm::dot(oc, rayDir);
  float c = glm::dot(oc, oc) - radius * radius;
  float h = b * b - c;
  if (h < 0.0f) return false;
  h = sqrt(h);
  t = -b - h;
  if (t < 0) t = -b + h;
  return t >= 0;
}

void App::OnEvent(SDL_Event& event) {
  // before/after divider: drag it with the left mouse button
  if (splitView) {
    float splitPx = dividerFraction * width;
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
        std::fabs(event.button.x - splitPx) < 10.0f) {
      draggingSplit = true;
      return;
    }
    if (draggingSplit && event.type == SDL_MOUSEMOTION) {
      dividerFraction = glm::clamp(event.motion.x / width, 0.02f, 0.98f);
      return;
    }
    if (draggingSplit && event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
      draggingSplit = false;
      return;
    }
  }
  if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_h && !event.key.repeat) {
    toggleUi();
    return;
  }
  if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_TAB && !event.key.repeat) {
    setShowB(!showB);  // flick between before and after
    return;
  }
  if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
    float mx = event.button.x;
    float my = event.button.y;

    // 1. Check Rotation Gizmo (Bottom Right)
    if (mx > (width - gizmoSize) && my > (height - gizmoSize)) {
      isRotating = true;
      lastMousePos = glm::vec2(mx, my);
      return;
    }

    // 2. Check Selection Box Gizmos (Translation Arrows & Scaling Handles)
    glm::vec3 rayOrigin = camera->position;
    glm::vec3 rayDir = getMouseRay(mx, my);
    glm::vec3 boxPos = glm::vec3(boxPosition[0], boxPosition[1], boxPosition[2]);
    float handleRadius = 0.3f;
    float minT = 1e10f;
    currentGizmoMode = GizmoMode::NONE;

    // Translation Handles (Arrows)
    float t;
    if (intersectRaySphere(rayOrigin, rayDir, boxPos + glm::vec3(2.0f, 0, 0), 0.5f, t) && t < minT) {
      minT = t;
      currentGizmoMode = GizmoMode::TRANSLATE_X;
    }
    if (intersectRaySphere(rayOrigin, rayDir, boxPos + glm::vec3(0, 2.0f, 0), 0.5f, t) && t < minT) {
      minT = t;
      currentGizmoMode = GizmoMode::TRANSLATE_Y;
    }
    if (intersectRaySphere(rayOrigin, rayDir, boxPos + glm::vec3(0, 0, 2.0f), 0.5f, t) && t < minT) {
      minT = t;
      currentGizmoMode = GizmoMode::TRANSLATE_Z;
    }

    // Scaling Handles (Faces)
    if (intersectRaySphere(rayOrigin, rayDir, boxPos + glm::vec3(boxSize[0], 0, 0), handleRadius, t) && t < minT) {
      minT = t;
      currentGizmoMode = GizmoMode::SCALE_PX;
    }
    if (intersectRaySphere(rayOrigin, rayDir, boxPos + glm::vec3(-boxSize[0], 0, 0), handleRadius, t) && t < minT) {
      minT = t;
      currentGizmoMode = GizmoMode::SCALE_NX;
    }
    if (intersectRaySphere(rayOrigin, rayDir, boxPos + glm::vec3(0, boxSize[1], 0), handleRadius, t) && t < minT) {
      minT = t;
      currentGizmoMode = GizmoMode::SCALE_PY;
    }
    if (intersectRaySphere(rayOrigin, rayDir, boxPos + glm::vec3(0, -boxSize[1], 0), handleRadius, t) && t < minT) {
      minT = t;
      currentGizmoMode = GizmoMode::SCALE_NY;
    }
    if (intersectRaySphere(rayOrigin, rayDir, boxPos + glm::vec3(0, 0, boxSize[2]), handleRadius, t) && t < minT) {
      minT = t;
      currentGizmoMode = GizmoMode::SCALE_PZ;
    }
    if (intersectRaySphere(rayOrigin, rayDir, boxPos + glm::vec3(0, 0, -boxSize[2]), handleRadius, t) && t < minT) {
      minT = t;
      currentGizmoMode = GizmoMode::SCALE_NZ;
    }

    if (currentGizmoMode != GizmoMode::NONE) {
      dragStartWorldPos = rayOrigin + rayDir * minT;
      dragStartBoxPos = boxPos;
      dragStartBoxSize = glm::vec3(boxSize[0], boxSize[1], boxSize[2]);
      return;
    }

  } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
    if (isRotating) sortVisible();
    isRotating = false;
    currentGizmoMode = GizmoMode::NONE;
  } else if (event.type == SDL_MOUSEMOTION) {
    float mx = event.motion.x;
    float my = event.motion.y;

    if (isRotating) {
      glm::vec3 va = getArcballVector(lastMousePos.x, lastMousePos.y);
      glm::vec3 vb = getArcballVector(mx, my);
      if (va != vb) {
        float angle = acos(glm::clamp(glm::dot(va, vb), -1.0f, 1.0f));
        glm::vec3 axis = glm::cross(va, vb);
        glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle * 2.0f, axis);
        splatModelMatrix = rotation * splatModelMatrix;
      }
      lastMousePos = glm::vec2(mx, my);
    } else if (currentGizmoMode != GizmoMode::NONE) {
      glm::vec3 rayOrigin = camera->position;
      glm::vec3 rayDir = getMouseRay(mx, my);

      // Axis Projection for Translation
      if (currentGizmoMode == GizmoMode::TRANSLATE_X || currentGizmoMode == GizmoMode::TRANSLATE_Y ||
          currentGizmoMode == GizmoMode::TRANSLATE_Z) {
        glm::vec3 axis(0);
        if (currentGizmoMode == GizmoMode::TRANSLATE_X) axis = glm::vec3(1, 0, 0);
        if (currentGizmoMode == GizmoMode::TRANSLATE_Y) axis = glm::vec3(0, 1, 0);
        if (currentGizmoMode == GizmoMode::TRANSLATE_Z) axis = glm::vec3(0, 0, 1);

        // Plane for projection: cross of axis and (axis cross cameraDir)
        glm::vec3 planeNormal = glm::normalize(glm::cross(axis, glm::cross(axis, rayOrigin - dragStartBoxPos)));
        float t = glm::dot(dragStartWorldPos - rayOrigin, planeNormal) / glm::dot(rayDir, planeNormal);
        glm::vec3 currentWorldPos = rayOrigin + rayDir * t;
        float offset = glm::dot(currentWorldPos - dragStartWorldPos, axis);

        boxPosition[0] = dragStartBoxPos.x + (currentGizmoMode == GizmoMode::TRANSLATE_X ? offset : 0);
        boxPosition[1] = dragStartBoxPos.y + (currentGizmoMode == GizmoMode::TRANSLATE_Y ? offset : 0);
        boxPosition[2] = dragStartBoxPos.z + (currentGizmoMode == GizmoMode::TRANSLATE_Z ? offset : 0);
      }
      // Axis Projection for Scaling
      else {
        glm::vec3 axis(0);
        int idx = 0;
        float sign = 1.0f;
        if (currentGizmoMode == GizmoMode::SCALE_PX) {
          axis = glm::vec3(1, 0, 0);
          idx = 0;
          sign = 1;
        }
        if (currentGizmoMode == GizmoMode::SCALE_NX) {
          axis = glm::vec3(1, 0, 0);
          idx = 0;
          sign = -1;
        }
        if (currentGizmoMode == GizmoMode::SCALE_PY) {
          axis = glm::vec3(0, 1, 0);
          idx = 1;
          sign = 1;
        }
        if (currentGizmoMode == GizmoMode::SCALE_NY) {
          axis = glm::vec3(0, 1, 0);
          idx = 1;
          sign = -1;
        }
        if (currentGizmoMode == GizmoMode::SCALE_PZ) {
          axis = glm::vec3(0, 0, 1);
          idx = 2;
          sign = 1;
        }
        if (currentGizmoMode == GizmoMode::SCALE_NZ) {
          axis = glm::vec3(0, 0, 1);
          idx = 2;
          sign = -1;
        }

        glm::vec3 planeNormal = glm::normalize(glm::cross(axis, glm::cross(axis, rayOrigin - dragStartWorldPos)));
        float t = glm::dot(dragStartWorldPos - rayOrigin, planeNormal) / glm::dot(rayDir, planeNormal);
        glm::vec3 currentWorldPos = rayOrigin + rayDir * t;
        float offset = glm::dot(currentWorldPos - dragStartWorldPos, axis);

        boxSize[idx] = std::max(0.1f, dragStartBoxSize[idx] + offset * sign * 0.5f);
        boxPosition[idx] = dragStartBoxPos[idx] + offset * 0.5f;
      }
    }
  }

  if (!isRotating && currentGizmoMode == GizmoMode::NONE) {
    camera->handle(event);
  }
}

void App::OnRender() {
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  camera->moveCamera();

  if (stereo.mode != StereoMode::Off) {
    // VR / stereo: render the scene once per eye into its own framebuffer, then combine.
    // Gizmos and the selection box are mono-only UI and are skipped here.
    if (!stereoRenderer) stereoRenderer = std::make_unique<StereoRenderer>();
    glm::ivec2 window{int(width), int(height)};
    glm::ivec2 eye = StereoRenderer::eyeSize(stereo.mode, window);
    for (int e = 0; e < 2; ++e) {
      RenderView v;
      v.size = eye;
      stereoEyeView(stereo, e, camera->position, camera->orientation, camera->upVector(), camera->fovDegrees(),
                    float(eye.x) / eye.y, camera->nearZ(), camera->farZ(), v.view, v.proj);
      stereoRenderer->beginEye(e, eye);
      renderScene(v);
    }
    stereoRenderer->composite(stereo, window);
    return;
  }

  renderScene(mainView());

  if (showSelectionBox) {
    boxShader->use();
    // Selection Box should be axis-aligned in world space, independent of model rotation
    glUniformMatrix4fv(glGetUniformLocation(boxShader->ID, "modelMatrix"), 1, GL_FALSE,
                       glm::value_ptr(glm::mat4(1.0f)));
    camera->update(boxShader.get());
    glUniform3f(glGetUniformLocation(boxShader->ID, "Color"), 0.8f, 0.8f, 0.8f);
    glUniform3fv(glGetUniformLocation(boxShader->ID, "Size"), 1, boxSize);
    glUniform3fv(glGetUniformLocation(boxShader->ID, "Position"), 1, boxPosition);
    boxMesh->draw(boxShader.get());

    // Wireframe overlay for sharp edges
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glUniform3f(glGetUniformLocation(boxShader->ID, "Color"), 1.0f, 1.0f, 1.0f);
    boxMesh->draw(boxShader.get());
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  // Render Rotation Gizmo in corner
  glClear(GL_DEPTH_BUFFER_BIT);
  glViewport(width - (int)gizmoSize, 0, (int)gizmoSize, (int)gizmoSize);
  boxShader->use();
  // Simple identity projection/view for gizmo
  glm::mat4 gizmoProj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 10.0f);
  glm::mat4 gizmoView = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f));
  glUniformMatrix4fv(glGetUniformLocation(boxShader->ID, "camMatrix"), 1, GL_FALSE,
                     glm::value_ptr(gizmoProj * gizmoView));
  glUniformMatrix4fv(glGetUniformLocation(boxShader->ID, "modelMatrix"), 1, GL_FALSE, glm::value_ptr(splatModelMatrix));

  // Draw X axis (Red)
  glUniform3f(glGetUniformLocation(boxShader->ID, "Color"), 1.0f, 0.0f, 0.0f);
  glUniform3f(glGetUniformLocation(boxShader->ID, "Size"), 1.0f, 0.05f, 0.05f);
  glUniform3f(glGetUniformLocation(boxShader->ID, "Position"), 0.5f, 0.0f, 0.0f);
  boxMesh->draw(boxShader.get());
  // Draw Y axis (Green)
  glUniform3f(glGetUniformLocation(boxShader->ID, "Color"), 0.0f, 1.0f, 0.0f);
  glUniform3f(glGetUniformLocation(boxShader->ID, "Size"), 0.05f, 1.0f, 0.05f);
  glUniform3f(glGetUniformLocation(boxShader->ID, "Position"), 0.0f, 0.5f, 0.0f);
  boxMesh->draw(boxShader.get());
  // Draw Z axis (Blue)
  glUniform3f(glGetUniformLocation(boxShader->ID, "Color"), 0.0f, 0.0f, 1.0f);
  glUniform3f(glGetUniformLocation(boxShader->ID, "Size"), 0.05f, 0.05f, 1.0f);
  glUniform3f(glGetUniformLocation(boxShader->ID, "Position"), 0.0f, 0.0f, 0.5f);
  boxMesh->draw(boxShader.get());

  // Draw Trackball Sphere
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  glUniform3f(glGetUniformLocation(boxShader->ID, "Color"), 0.6f, 0.6f, 0.6f);
  glUniform3f(glGetUniformLocation(boxShader->ID, "Size"), 1.0f, 1.0f, 1.0f);
  glUniform3f(glGetUniformLocation(boxShader->ID, "Position"), 0.0f, 0.0f, 0.0f);
  glUniformMatrix4fv(glGetUniformLocation(boxShader->ID, "modelMatrix"), 1, GL_FALSE, glm::value_ptr(splatModelMatrix));
  sphereMesh->draw(boxShader.get());
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  glViewport(0, 0, (int)width, (int)height);

  // --- Render 3D Gizmo Handles ---
  if (showSelectionBox) {
    boxShader->use();
    camera->update(boxShader.get());
    glUniformMatrix4fv(glGetUniformLocation(boxShader->ID, "modelMatrix"), 1, GL_FALSE,
                       glm::value_ptr(glm::mat4(1.0f)));
    glm::vec3 boxPos = glm::vec3(boxPosition[0], boxPosition[1], boxPosition[2]);

    // Translation Arrows (Using Cones)
    // X Arrow (Red)
    glUniform3f(glGetUniformLocation(boxShader->ID, "Color"), 1.0f, 0.2f, 0.2f);
    glUniform3f(glGetUniformLocation(boxShader->ID, "Size"), 0.2f, 0.2f, 0.5f);
    glm::mat4 modelX = glm::translate(glm::mat4(1.0f), boxPos + glm::vec3(2.0f, 0, 0));
    modelX = glm::rotate(modelX, glm::radians(90.0f), glm::vec3(0, 1, 0));
    glUniformMatrix4fv(glGetUniformLocation(boxShader->ID, "modelMatrix"), 1, GL_FALSE, glm::value_ptr(modelX));
    coneMesh->draw(boxShader.get());

    // Y Arrow (Green)
    glUniform3f(glGetUniformLocation(boxShader->ID, "Color"), 0.2f, 1.0f, 0.2f);
    glm::mat4 modelY = glm::translate(glm::mat4(1.0f), boxPos + glm::vec3(0, 2.0f, 0));
    modelY = glm::rotate(modelY, glm::radians(-90.0f), glm::vec3(1, 0, 0));
    glUniformMatrix4fv(glGetUniformLocation(boxShader->ID, "modelMatrix"), 1, GL_FALSE, glm::value_ptr(modelY));
    coneMesh->draw(boxShader.get());

    // Z Arrow (Blue)
    glUniform3f(glGetUniformLocation(boxShader->ID, "Color"), 0.2f, 0.2f, 1.0f);
    glm::mat4 modelZ = glm::translate(glm::mat4(1.0f), boxPos + glm::vec3(0, 0, 2.0f));
    glUniformMatrix4fv(glGetUniformLocation(boxShader->ID, "modelMatrix"), 1, GL_FALSE, glm::value_ptr(modelZ));
    coneMesh->draw(boxShader.get());

    // Scaling Handles (Small cyan spheres on faces) - Size decreased from 0.2 to 0.1
    glUniform3f(glGetUniformLocation(boxShader->ID, "Color"), 0.0f, 1.0f, 1.0f);  // Cyan
    glUniform3f(glGetUniformLocation(boxShader->ID, "Size"), 0.1f, 0.1f, 0.1f);
    glUniformMatrix4fv(glGetUniformLocation(boxShader->ID, "modelMatrix"), 1, GL_FALSE,
                       glm::value_ptr(glm::mat4(1.0f)));
    glUniform3fv(glGetUniformLocation(boxShader->ID, "Position"), 1,
                 glm::value_ptr(boxPos + glm::vec3(boxSize[0], 0, 0)));
    sphereMesh->draw(boxShader.get());
    glUniform3fv(glGetUniformLocation(boxShader->ID, "Position"), 1,
                 glm::value_ptr(boxPos + glm::vec3(-boxSize[0], 0, 0)));
    sphereMesh->draw(boxShader.get());
    glUniform3fv(glGetUniformLocation(boxShader->ID, "Position"), 1,
                 glm::value_ptr(boxPos + glm::vec3(0, boxSize[1], 0)));
    sphereMesh->draw(boxShader.get());
    glUniform3fv(glGetUniformLocation(boxShader->ID, "Position"), 1,
                 glm::value_ptr(boxPos + glm::vec3(0, -boxSize[1], 0)));
    sphereMesh->draw(boxShader.get());
    glUniform3fv(glGetUniformLocation(boxShader->ID, "Position"), 1,
                 glm::value_ptr(boxPos + glm::vec3(0, 0, boxSize[2])));
    sphereMesh->draw(boxShader.get());
    glUniform3fv(glGetUniformLocation(boxShader->ID, "Position"), 1,
                 glm::value_ptr(boxPos + glm::vec3(0, 0, -boxSize[2])));
    sphereMesh->draw(boxShader.get());
  }

  glEnable(GL_DEPTH_TEST);
}

App::RenderView App::mainView() const {
  RenderView v;
  v.size = glm::ivec2(int(width), int(height));
  v.view = glm::lookAt(camera->position, camera->position + camera->orientation, camera->upVector());
  v.proj = glm::perspective(glm::radians(camera->fovDegrees()), width / height, camera->nearZ(), camera->farZ());
  return v;
}

void App::applyView(Shader* shader) const {
  glUniformMatrix4fv(glGetUniformLocation(shader->ID, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(currentView.view));
  glUniformMatrix4fv(glGetUniformLocation(shader->ID, "camMatrix"), 1, GL_FALSE,
                     glm::value_ptr(currentView.proj * currentView.view));
}

void App::renderScene(const RenderView& v) {
  currentView = v;
  glViewport(0, 0, v.size.x, v.size.y);
  // SplatDiff view: splats (one scan, or A | B split at the divider), then the change volume on top
  if (!volumeOnly) {
    if (splitView) {
      int splitPx = int(dividerFraction * v.size.x);
      glEnable(GL_SCISSOR_TEST);
      glScissor(0, 0, splitPx, v.size.y);
      drawSplats(sceneA.get());
      glScissor(splitPx, 0, v.size.x - splitPx, v.size.y);
      drawSplats(sceneB.get());
      glDisable(GL_SCISSOR_TEST);
    } else {
      drawSplats(splat);
    }
  }
  if ((showVolume || volumeOnly) && volumeView && detection) {
    volumeView->draw(v.proj, v.view, splatModelMatrix, glm::vec2(v.size));
  }

  // Use standard alpha blending for GUI/Gizmos
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // SplatDiff: detected change regions and ground-truth boxes (file space, drawn with the model matrix)
  if (showGroundTruth && !editLog.empty()) {
    for (const auto& b : splatdiff::groundTruthBoxes(editLog)) drawBoxEdges(b.min, b.max, glm::vec3(0.9f));
  }
  if (showRegions && detection) {
    for (const auto& r : detection->regions) {
      glm::vec3 c(1.0f, 0.3f, 1.0f);  // changed (mixed)
      if (r.type == "added") c = glm::vec3(0.3f, 1.0f, 0.4f);
      if (r.type == "removed") c = glm::vec3(1.0f, 0.25f, 0.25f);
      if (r.type == "moved (from)") c = glm::vec3(1.0f, 0.55f, 0.1f);
      if (r.type == "moved (to)") c = glm::vec3(1.0f, 0.85f, 0.2f);
      drawBoxEdges(r.min, r.max, c);
    }
  }
}

void App::drawSplats(GaussianSplat* scene) {
  // Use premultiplied alpha for Gaussian Splats
  glEnable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  splatShader->use();
  glUniformMatrix4fv(glGetUniformLocation(splatShader->ID, "modelMatrix"), 1, GL_FALSE,
                     glm::value_ptr(splatModelMatrix));
  glUniform1f(glGetUniformLocation(splatShader->ID, "scaleFactor"), scaleFactor);
  // Pinhole focal length in pixels for this viewport. The same focal applies to x and y (square pixels);
  // the original code used the window *width* for focal_y, which stretched every splat vertically by the
  // aspect ratio (1.78x on a 16:9 window).
  const glm::vec2 res(currentView.size);
  const float tanHalfFovY = std::tan(glm::radians(camera->fovDegrees()) * 0.5f);
  const float focal = res.y / (2.0f * tanHalfFovY);
  glUniform2f(glGetUniformLocation(splatShader->ID, "Resolution"), res.x, res.y);
  glUniform2f(glGetUniformLocation(splatShader->ID, "Focal"), focal, focal);
  glUniform2f(glGetUniformLocation(splatShader->ID, "TanFov"), tanHalfFovY * res.x / res.y, tanHalfFovY);
  // calc box bounds location
  glm::vec3 boxMin = glm::vec3(boxPosition[0] - boxSize[0], boxPosition[1] - boxSize[1], boxPosition[2] - boxSize[2]);
  glm::vec3 boxMax = glm::vec3(boxPosition[0] + boxSize[0], boxPosition[1] + boxSize[1], boxPosition[2] + boxSize[2]);
  glUniform3fv(glGetUniformLocation(splatShader->ID, "BoxMin"), 1, glm::value_ptr(boxMin));
  glUniform3fv(glGetUniformLocation(splatShader->ID, "BoxMax"), 1, glm::value_ptr(boxMax));
  glUniform1i(glGetUniformLocation(splatShader->ID, "showSelectionBox"), showSelectionBox ? 1 : 0);
  glUniform1i(glGetUniformLocation(splatShader->ID, "overlayMode"), (overlay && detection) ? 1 : 0);
  glUniform1i(glGetUniformLocation(splatShader->ID, "dimUnchanged"), (dimUnchanged && detection) ? 1 : 0);
  applyView(splatShader.get());
  scene->draw(splatShader.get());
}

void App::sortVisible() {
  if (!camera) return;
  glm::mat4 vm = getViewModelMatrix();
  if (splitView) {
    sceneA->sort(vm);
    sceneB->sort(vm);
  } else {
    splat->sort(vm);
  }
}

void App::OnImGuiRender() {
  ImGui::Text("Camera Position:");
  ImGui::Text("X:%.2f Y:%.2f Z:%.2f", camera->position.x, camera->position.y, camera->position.z);
  ImGui::Text("Camera Orientation:");
  ImGui::Text("X:%.2f Y:%.2f Z:%.2f", camera->orientation.x, camera->orientation.y, camera->orientation.z);
  showPathSelector();

  ImGui::SliderFloat("ScaleF", &scaleFactor, 0.1f, 3.0f);

  ImGui::Checkbox("Show Selection Box", &showSelectionBox);
}

glm::mat4 App::getViewModelMatrix() { return camera->viewMatrix * splatModelMatrix; }

std::unique_ptr<GaussianSplat> App::loadPlyFile(std::string path) {
  if (path.empty()) {
    return std::make_unique<GaussianSplat>(std::vector<GaussianSphere>());
  }
  splatdiff::Gaussians spheres = splatdiff::loadPly(path);
  std::cout << "Loaded " << spheres.size() << " gaussians from " << path << std::endl;
  return std::make_unique<GaussianSplat>(spheres);
}

void App::showPathSelector() {
  if (ImGui::Button("Select scan A (.ply)")) {
    IGFD::FileDialogConfig config;
    config.path = "./assets/";
    ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose scan A (.ply)", ".ply", config);
  }

  if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      modelPath = ImGuiFileDialog::Instance()->GetFilePathName();
      sceneA = loadPlyFile(modelPath);
      resetScanB();  // a new reference scan invalidates the old scan B
      setShowB(false);
    }
    ImGuiFileDialog::Instance()->Close();
  }
}

// ---------------- SplatDiff: scans A/B and the scan-B editor ----------------

splatdiff::Region App::selectionRegion() const {
  splatdiff::Region r;
  r.center = glm::vec3(boxPosition[0], boxPosition[1], boxPosition[2]);
  r.halfSize = glm::vec3(boxSize[0], boxSize[1], boxSize[2]);
  r.modelMatrix = splatModelMatrix;
  return r;
}

void App::resetScanB() {
  sceneB = std::make_unique<GaussianSplat>(sceneA->spheres);
  editLog.clear();
  detection.reset();
  if (showB) splat = sceneB.get();
}

void App::setShowB(bool show) {
  showB = show;
  splat = showB ? sceneB.get() : sceneA.get();
  if (camera) sortVisible();
}

void App::loadScanB(const std::string& path) {
  sceneB = loadPlyFile(path);
  scanBPath = path;
  // ground truth saved next to the scan (by "Save B" or splatdiff_edit), if any
  editLog = splatdiff::loadEditLog(path.substr(0, path.rfind('.')) + ".edits.json");
  detection.reset();
  if (autoDetect) runDetection();
  setShowB(showB);
}

void App::onScanBEdited(const splatdiff::EditRecord& record) {
  editLog.push_back(record);
  sceneB->upload();
  setShowB(true);  // always show the result of an edit
  if (autoDetect) runDetection();
  std::cout << "[scan B] " << record.type << ": " << record.affected << " gaussians " << record.params << std::endl;
}

void App::OnSplatDiffImGui() {
  using namespace splatdiff;

  // ---- Scans ----
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::CollapsingHeader("Scans")) {
    if (ImGui::RadioButton("Scan A (before)", !showB)) setShowB(false);
    ImGui::SameLine();
    if (ImGui::RadioButton("Scan B (after)", showB)) setShowB(true);
    ImGui::TextDisabled("Tab switches A/B, H hides the UI");
    ImGui::Text("A: %zu gaussians", sceneA->spheres.size());
    ImGui::Text("B: %zu gaussians, %zu edits", sceneB->spheres.size(), editLog.size());

    if (ImGui::Button("Reset B = A")) {
      resetScanB();
      setShowB(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load B...")) {
      IGFD::FileDialogConfig config;
      config.path = "./assets/";
      ImGuiFileDialog::Instance()->OpenDialog("ChooseScanBDlgKey", "Choose scan B (.ply)", ".ply", config);
    }
    if (ImGuiFileDialog::Instance()->Display("ChooseScanBDlgKey")) {
      if (ImGuiFileDialog::Instance()->IsOk()) loadScanB(ImGuiFileDialog::Instance()->GetFilePathName());
      ImGuiFileDialog::Instance()->Close();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save B")) {
      try {
        savePly(scanBPath, sceneB->spheres);
        std::string logPath = scanBPath.substr(0, scanBPath.rfind('.')) + ".edits.json";
        saveEditLog(logPath, modelPath, editLog);
        std::cout << "Saved scan B to " << scanBPath << " and ground truth to " << logPath << std::endl;
      } catch (const std::exception& e) {
        std::cerr << "Save failed: " << e.what() << std::endl;
      }
    }
    static char pathBuf[512];
    std::snprintf(pathBuf, sizeof(pathBuf), "%s", scanBPath.c_str());
    if (ImGui::InputText("B path", pathBuf, sizeof(pathBuf))) scanBPath = pathBuf;
  }

  // ---- Real changes (inside the selection box) ----
  Region region = selectionRegion();
  if (ImGui::CollapsingHeader("Edit scan B")) {
    ImGui::SeparatorText("Real changes (inside selection box)");
    int selected = 0;
    for (const auto& g : sceneB->spheres) selected += region.contains(g.position);
    ImGui::Text("Selection box holds %d gaussians of B", selected);
    if (!showSelectionBox) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Turn on 'Show Selection Box' to place it");

    ImGui::DragFloat3("Move by", moveTranslation, 0.01f);
    ImGui::SliderFloat("Rotate (yaw)", &moveYawDeg, -180.0f, 180.0f, "%.0f deg");
    if (ImGui::Button("Move selection"))
      onScanBEdited(moveRegion(sceneB->spheres, region, glm::make_vec3(moveTranslation), moveYawDeg));
    ImGui::SameLine();
    if (ImGui::Button("Delete selection")) onScanBEdited(deleteRegion(sceneB->spheres, region));

    ImGui::DragFloat3("Copy offset", duplicateOffset, 0.01f);
    if (ImGui::Button("Duplicate selection"))
      onScanBEdited(duplicateRegion(sceneB->spheres, region, glm::make_vec3(duplicateOffset)));

    // ---- Changes that must NOT be detected ----
    ImGui::SeparatorText("Re-scan effects (should be ignored)");
    ImGui::ColorEdit3("Colour gain", colourGain, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::SliderFloat("Brightness", &colourOffset, -0.3f, 0.3f);
    ImGui::Checkbox("Only inside selection", &colourSelectionOnly);
    if (ImGui::Button("Apply lighting change"))
      onScanBEdited(colourShift(sceneB->spheres, glm::make_vec3(colourGain), glm::vec3(colourOffset),
                                colourSelectionOnly ? &region : nullptr));

    ImGui::SliderFloat("Position noise", &jitterPos, 0.0f, 0.05f, "%.4f");
    ImGui::SliderFloat("Scale noise", &jitterScale, 0.0f, 0.3f, "%.3f");
    if (ImGui::Button("Jitter all")) onScanBEdited(jitterAll(sceneB->spheres, jitterPos, jitterScale, editSeed++));

    ImGui::SliderFloat("Split fraction", &splitFraction, 0.0f, 1.0f);
    if (ImGui::Button("Split gaussians")) onScanBEdited(splitRandom(sceneB->spheres, splitFraction, editSeed++));

    ImGui::SliderFloat("Prune fraction", &pruneFraction, 0.0f, 1.0f);
    ImGui::SliderFloat("Max opacity", &pruneMaxOpacity, 0.0f, 1.0f);
    if (ImGui::Button("Prune faint gaussians"))
      onScanBEdited(pruneFaint(sceneB->spheres, pruneFraction, pruneMaxOpacity, editSeed++));
  }

  // ---- Ground truth ----
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::CollapsingHeader("Edit log (ground truth)")) {
    if (editLog.empty()) ImGui::TextDisabled("B is identical to A");
    for (size_t i = 0; i < editLog.size(); ++i) {
      const auto& r = editLog[i];
      ImVec4 col = r.geometric ? ImVec4(1.0f, 0.45f, 0.4f, 1) : ImVec4(0.55f, 0.8f, 1.0f, 1);
      ImGui::TextColored(col, "%zu. %-9s %7d  %s", i + 1, r.type.c_str(), r.affected, r.params.c_str());
    }
    ImGui::TextDisabled("red = real change, blue = must be ignored");
  }

  // ---- View ----
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::CollapsingHeader("View")) {
    ImGui::Checkbox("Change overlay", &overlay);
    ImGui::SameLine();
    ImGui::Checkbox("Dim unchanged", &dimUnchanged);
    if (ImGui::Checkbox("Before | after split", &splitView)) sortVisible();
    if (splitView) ImGui::SliderFloat("Divider", &dividerFraction, 0.02f, 0.98f);
    ImGui::Checkbox("Change volume", &showVolume);
    ImGui::SameLine();
    ImGui::Checkbox("Volume only", &volumeOnly);
    if (volumeView && (showVolume || volumeOnly)) {
      ImGui::SliderFloat("Volume opacity", &volumeView->opacity, 0.1f, 5.0f);
      ImGui::SliderFloat("Volume threshold", &volumeView->threshold, 0.0f, 0.9f);
      ImGui::Checkbox("Show occupied space", &volumeView->showContext);
    }
    if (!detection) ImGui::TextDisabled("Run change detection to enable overlay and volume");
    ImGui::Checkbox("Slice view", &showSlice);
    if (showSlice && detection && volumeView) {
      const char* axes[] = {"X (side)", "Y (top-down)", "Z (front)"};
      bool changed = ImGui::Combo("Slice axis", &sliceAxis, axes, 3);
      changed |= ImGui::SliderFloat("Slice position", &slicePosition, 0.0f, 1.0f);
      if (changed) volumeView->updateSlice(*detection, sliceAxis, slicePosition);
      glm::ivec2 sz = volumeView->sliceSize();
      float w = ImGui::GetContentRegionAvail().x;
      float h = std::min(w * sz.y / std::max(sz.x, 1), 360.0f);
      ImGui::Image((ImTextureID)(intptr_t)volumeView->sliceTexture(), ImVec2(h * sz.x / std::max(sz.y, 1), h));
      ImGui::TextDisabled("grey = occupied, red = removed, green = added");
    }
  }

  // ---- Stereo / VR ----
  if (ImGui::CollapsingHeader("Stereo / VR")) {
    int m = int(stereo.mode);
    const char* stereoModes[] = {"Off (mono)", "Anaglyph red/cyan (colour)", "Anaglyph red/cyan (grey)", "Side-by-side",
                                 "Cardboard (side-by-side + lens)"};
    ImGui::Combo("Stereo mode", &m, stereoModes, 5);
    stereo.mode = StereoMode(m);
    ImGui::SliderFloat("Eye separation", &stereo.eyeSeparation, 0.0f, 0.6f, "%.3f");
    ImGui::SliderFloat("Convergence", &stereo.convergence, 0.5f, 30.0f, "%.2f");
    if (ImGui::Button("Converge on scene centre") && detection) {
      glm::vec3 centre = detection->grid.origin + 0.5f * glm::vec3(detection->grid.dims) * detection->grid.voxel;
      glm::vec3 world = glm::vec3(splatModelMatrix * glm::vec4(centre, 1.0f));
      stereo.convergence = std::max(0.5f, glm::dot(world - camera->position, glm::normalize(camera->orientation)));
    }
    ImGui::Checkbox("Toe-in (wrong, compare)", &stereo.toeIn);
    ImGui::SameLine();
    ImGui::Checkbox("Swap eyes", &stereo.swapEyes);
    if (stereo.mode == StereoMode::Cardboard) {
      ImGui::SliderFloat("Lens k1", &stereo.k1, 0.0f, 0.6f);
      ImGui::SliderFloat("Lens k2", &stereo.k2, 0.0f, 0.6f);
    }
    ImGui::TextDisabled("Objects at the convergence distance sit at screen depth;");
    ImGui::TextDisabled("nearer ones pop out, farther ones sink in.");
    if (stereo.mode == StereoMode::SideBySide) ImGui::TextDisabled("Cross-eye viewing: tick Swap eyes.");
  }

  // ---- Experiments ----
  if (!expRows.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::CollapsingHeader("Experiments")) {
    ImGui::TextDisabled("Runs on scan A with the detection settings below.");
    ImGui::TextDisabled("The window pauses while a run is in progress.");
    ImGui::InputInt("Seed", &expSeed);
    if (ImGui::Button("Run all cases")) runExperimentCases();
    ImGui::SameLine();
    if (ImGui::Button("Tau sweep")) runTauSweep();
    ImGui::SameLine();
    if (ImGui::Button("Thread scaling")) runThreadSweep();
    if (ImGui::Button("Export CSV")) exportExperiments();
    if (!expStatus.empty()) ImGui::TextWrapped("%s", expStatus.c_str());

    if (!expRows.empty() &&
        ImGui::BeginTable("cases", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn("case", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("found");
      ImGui::TableSetupColumn("IoU");
      ImGui::TableSetupColumn("result");
      ImGui::TableSetupColumn("");
      ImGui::TableHeadersRow();
      for (size_t i = 0; i < expRows.size(); ++i) {
        const auto& r = expRows[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", r.name.c_str());
        if (ImGui::IsItemHovered() && i < expCases.size())
          ImGui::SetTooltip("%s: %s", r.group.c_str(), expCases[i].description.c_str());
        ImGui::TableNextColumn();
        if (r.expectChange)
          ImGui::Text("%d/%d", r.gtDetected, r.gtBoxes);
        else
          ImGui::Text("%d reg", r.regions);
        ImGui::TableNextColumn();
        if (r.expectChange)
          ImGui::Text("%.2f", r.meanIoU);
        else
          ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        ImGui::TextColored(r.pass ? ImVec4(0.4f, 0.9f, 0.4f, 1) : ImVec4(1, 0.35f, 0.35f, 1), r.pass ? "PASS" : "FAIL");
        ImGui::TableNextColumn();
        ImGui::PushID(int(i));
        if (ImGui::SmallButton("Show")) showExperimentCase(i);
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (!expTau.empty()) {
      std::vector<float> iou, fcr;
      for (const auto& p : expTau)
        if (p.mode == expTau.front().mode) iou.push_back(p.meanIoU), fcr.push_back(100.0f * p.falseChangeRate);
      char overlay[64];
      std::snprintf(overlay, sizeof(overlay), "IoU vs tau %.2f..%.2f", expTau.front().x, expTau[iou.size() - 1].x);
      ImGui::PlotLines("##iou", iou.data(), int(iou.size()), 0, overlay, 0.0f, 1.0f, ImVec2(-1, 60));
      ImGui::PlotLines("##fcr", fcr.data(), int(fcr.size()), 0, "false-change % vs tau", 0.0f, FLT_MAX, ImVec2(-1, 60));
    }
    if (!expThreads.empty()) {
      double serial = expThreads.front().totalMs;
      for (const auto& p : expThreads)
        ImGui::Text("%-24s %d thr  %7.1f ms  x%.2f", p.mode.c_str(), p.threads, p.totalMs, serial / p.totalMs);
    }
  }

  // ---- Change detection ----
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (!ImGui::CollapsingHeader("Change detection")) return;
  ImGui::SliderInt("Resolution", &detectParams.resolution, 32, 256);
  ImGui::SliderFloat("Threshold tau", &detectParams.tau, 0.05f, 0.95f);
  ImGui::SliderInt("Tolerance (voxels)", &detectParams.tolerance, 0, 2);
  ImGui::SliderInt("Min region voxels", &detectParams.minVoxels, 1, 100);
  ImGui::Checkbox("Occupancy 1-exp(-Phi)", &detectParams.occupancy);
  ImGui::SameLine();
  int filter = int(detectParams.filter);
  const char* filters[] = {"none", "clamp thin axes", "dilate all axes", "mass-preserving"};
  ImGui::SetNextItemWidth(150);
  if (ImGui::Combo("Filter", &filter, filters, 4)) detectParams.filter = FilterMode(filter);
  int mode = int(detectParams.mode);
  const char* modes[] = {"Serial", "OpenMP (atomic)", "OpenMP (private grids)"};
  if (ImGui::Combo("Execution", &mode, modes, 3)) detectParams.mode = ExecMode(mode);
  if (ImGui::Button("Detect changes")) runDetection();
  ImGui::SameLine();
  ImGui::Checkbox("Auto", &autoDetect);
  ImGui::SameLine();
  if (ImGui::Button("Benchmark")) runBenchmark();
  ImGui::Checkbox("Show regions", &showRegions);
  ImGui::SameLine();
  ImGui::Checkbox("Show ground truth", &showGroundTruth);

  if (!benchmarkReport.empty()) ImGui::TextUnformatted(benchmarkReport.c_str());
  if (detection) {
    const auto& d = *detection;
    const auto& t = d.timings;
    ImGui::Text("Grid %dx%dx%d, voxel %.3f", d.grid.dims.x, d.grid.dims.y, d.grid.dims.z, d.grid.voxel);
    ImGui::Text("%.1f ms on %d thread%s (splat %.1f + %.1f, compare %.1f, regions %.1f)", t.total, t.threads,
                t.threads > 1 ? "s" : "", t.splatA, t.splatB, t.compare, t.regions);
    ImGui::Text("%zu changed of %zu occupied voxels, %zu regions", d.changedVoxels, d.occupiedVoxels, d.regions.size());
    for (size_t i = 0; i < d.regions.size(); ++i) {
      const auto& r = d.regions[i];
      ImVec4 c(1.0f, 0.3f, 1.0f, 1);
      if (r.type == "added") c = ImVec4(0.3f, 1.0f, 0.4f, 1);
      if (r.type == "removed") c = ImVec4(1.0f, 0.25f, 0.25f, 1);
      if (r.type == "moved (from)") c = ImVec4(1.0f, 0.55f, 0.1f, 1);
      if (r.type == "moved (to)") c = ImVec4(1.0f, 0.85f, 0.2f, 1);
      ImGui::TextColored(c, "[%zu] %-13s %6d vox  at (%.2f, %.2f, %.2f)", i, r.type.c_str(), r.voxels, r.centroid.x,
                         r.centroid.y, r.centroid.z);
    }
    if (haveEvaluation) {
      ImGui::Text("vs ground truth: found %d/%d changes, %d false regions", evaluation.gtDetected, evaluation.gtBoxes,
                  evaluation.falseRegions);
      ImGui::Text("false-change rate %.3f%%, mean box IoU %.2f", 100.0f * evaluation.falseChangeRate,
                  evaluation.meanIoU);
    }
  }
}

// ---------------- SplatDiff: change detection ----------------

void App::drawBoxEdges(const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& colour) {
  boxShader->use();
  applyView(boxShader.get());
  glUniformMatrix4fv(glGetUniformLocation(boxShader->ID, "modelMatrix"), 1, GL_FALSE, glm::value_ptr(splatModelMatrix));
  glUniform3fv(glGetUniformLocation(boxShader->ID, "Color"), 1, glm::value_ptr(colour));
  glm::vec3 centre = 0.5f * (mn + mx), half = 0.5f * (mx - mn);
  glUniform3fv(glGetUniformLocation(boxShader->ID, "Position"), 1, glm::value_ptr(centre));
  glUniform3fv(glGetUniformLocation(boxShader->ID, "Size"), 1, glm::value_ptr(half));
  glUniform1f(glGetUniformLocation(boxShader->ID, "Alpha"), 1.0f);
  // core-profile macOS only draws 1-pixel lines: draw three nested copies for a bolder outline
  edgeMesh->vao.bind();
  float grow = 0.004f * glm::length(mx - mn) + 0.002f;
  for (int k = 0; k < 3; ++k) {
    glm::vec3 h = half + glm::vec3(k * grow);
    glUniform3fv(glGetUniformLocation(boxShader->ID, "Size"), 1, glm::value_ptr(h));
    glDrawElements(GL_LINES, edgeMesh->indices.size(), GL_UNSIGNED_INT, 0);
  }
  edgeMesh->vao.unbind();
  glUniform1f(glGetUniformLocation(boxShader->ID, "Alpha"), 0.5f);
}

void App::runDetection() {
  if (!sceneA || !sceneB || sceneA->spheres.empty()) return;
  detection = std::make_unique<splatdiff::DetectResult>(
      splatdiff::detectChanges(sceneA->spheres, sceneB->spheres, detectParams));
  haveEvaluation = !editLog.empty();
  if (haveEvaluation) evaluation = splatdiff::evaluate(*detection, splatdiff::groundTruthBoxes(editLog));
  updateChangeTags();
  if (!volumeView) volumeView = std::make_unique<VolumeView>();
  volumeView->upload(*detection);
  volumeView->updateSlice(*detection, sliceAxis, slicePosition);
  const auto& t = detection->timings;
  std::cout << "[detect] " << detection->regions.size() << " regions, " << detection->changedVoxels
            << " changed voxels, " << t.total << " ms (" << splatdiff::execModeName(detectParams.mode) << ")"
            << std::endl;
}

void App::runBenchmark() {
  using namespace splatdiff;
  char line[256];
  benchmarkReport.clear();
  double serial = 0;
  for (ExecMode m : {ExecMode::Serial, ExecMode::OpenMPAtomic, ExecMode::OpenMPPrivate}) {
    DetectParams q = detectParams;
    q.mode = m;
    DetectResult r = detectChanges(sceneA->spheres, sceneB->spheres, q);
    if (m == ExecMode::Serial) serial = r.timings.total;
    std::snprintf(line, sizeof(line), "%-22s %7.1f ms  x%.2f\n", execModeName(m), r.timings.total,
                  serial / r.timings.total);
    benchmarkReport += line;
  }
}

void App::setCamera(const glm::vec3& position, const glm::vec3& lookAt) {
  camera->position = position;
  camera->orientation = glm::normalize(lookAt - position);
  camera->viewMatrix =
      glm::lookAt(camera->position, camera->position + camera->orientation, glm::vec3(0.0f, 1.0f, 0.0f));
  sortVisible();  // depth order for the new viewpoint
}

// Tag every Gaussian with the type of the change region it sits in, so the splat shader can tint it.
// Scan A only shows "disappearing" changes (removed, moved from), scan B only "appearing" ones.
void App::updateChangeTags() {
  if (!detection) return;
  const auto& d = *detection;
  const auto& g = d.grid;
  auto regionAt = [&](const glm::vec3& p) {
    glm::ivec3 c = glm::ivec3(glm::floor((p - g.origin) / g.voxel));
    int best = -1;
    for (int dz = -1; dz <= 1 && best < 0; ++dz)  // own voxel first, then the 26 neighbours
      for (int dy = -1; dy <= 1 && best < 0; ++dy)
        for (int dx = -1; dx <= 1 && best < 0; ++dx) {
          glm::ivec3 q = c + glm::ivec3(dx, dy, dz);
          if (glm::any(glm::lessThan(q, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(q, g.dims))) continue;
          best = d.regionId[g.index(q.x, q.y, q.z)];
        }
    if (best < 0 && glm::all(glm::greaterThanEqual(c, glm::ivec3(0))) && glm::all(glm::lessThan(c, g.dims)))
      best = d.regionId[g.index(c.x, c.y, c.z)];
    return best;
  };
  auto tagFor = [&](const std::string& type, bool inA) -> float {
    if (type == "changed") return 5;
    if (inA) return type == "removed" ? 1 : (type == "moved (from)" ? 3 : 0);
    return type == "added" ? 2 : (type == "moved (to)" ? 4 : 0);
  };
  for (int s = 0; s < 2; ++s) {
    GaussianSplat* scene = s == 0 ? sceneA.get() : sceneB.get();
    std::vector<float> tags(scene->spheres.size(), 0.0f);
#pragma omp parallel for schedule(static)
    for (long i = 0; i < long(tags.size()); ++i) {
      int r = regionAt(scene->spheres[i].position);
      if (r >= 0) tags[i] = tagFor(d.regions[r].type, s == 0);
    }
    scene->setTags(tags);
  }
}

// ---------------- SplatDiff: experiments (milestone 5) ----------------

void App::runExperimentCases() {
  using namespace splatdiff;
  if (sceneA->spheres.empty()) return;
  expCases = standardCases(sceneA->spheres, uint32_t(expSeed));
  expRows.clear();
  int passed = 0;
  for (const auto& c : expCases) {
    expRows.push_back(runCase(sceneA->spheres, c, detectParams));
    passed += expRows.back().pass;
  }
  expStatus = std::to_string(passed) + " / " + std::to_string(expRows.size()) +
              " cases passed. Hover a case for "
              "details, Show loads it as scan B.";
  std::cout << "[experiments] " << expStatus << std::endl;
}

void App::showExperimentCase(size_t index) {
  using namespace splatdiff;
  if (index >= expCases.size()) return;
  Gaussians b;
  std::vector<EditRecord> log;
  runCase(sceneA->spheres, expCases[index], detectParams, &b, &log);
  sceneB = std::make_unique<GaussianSplat>(b);
  editLog = log;
  scanBPath = "./assets/case_" + expCases[index].name + ".ply";
  setShowB(true);
  runDetection();
}

// the sweeps run on the "combined" case (real edits + lighting + re-scan noise)
static bool combinedCase(const splatdiff::Gaussians& a, uint32_t seed, splatdiff::Gaussians& b,
                         std::vector<splatdiff::EditRecord>& log) {
  for (const auto& c : splatdiff::standardCases(a, seed))
    if (c.name == "combined") {
      b = a;
      c.make(b, log);
      return true;
    }
  return false;
}

void App::runTauSweep() {
  using namespace splatdiff;
  Gaussians b;
  std::vector<EditRecord> log;
  if (sceneA->spheres.empty() || !combinedCase(sceneA->spheres, uint32_t(expSeed), b, log)) return;
  expTau = sweepTau(sceneA->spheres, b, log, detectParams,
                    {0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f});
  expStatus = "Tau sweep done (combined case, current settings).";
}

void App::runThreadSweep() {
  using namespace splatdiff;
  Gaussians b;
  std::vector<EditRecord> log;
  if (sceneA->spheres.empty() || !combinedCase(sceneA->spheres, uint32_t(expSeed), b, log)) return;
  std::vector<int> threads;
  for (int t = 1; t <= maxThreads(); t *= 2) threads.push_back(t);
  expThreads = sweepThreads(sceneA->spheres, b, detectParams, {detectParams.resolution}, threads, 1);
  expStatus = "Thread scaling done at resolution " + std::to_string(detectParams.resolution) + ".";
}

void App::exportExperiments() {
  using namespace splatdiff;
  std::filesystem::create_directories("./assets/experiments_viewer");
  if (!expRows.empty()) writeCasesCsv("./assets/experiments_viewer/cases.csv", expRows);
  if (!expTau.empty()) writeSweepCsv("./assets/experiments_viewer/tau_sweep.csv", expTau);
  if (!expThreads.empty()) writeSweepCsv("./assets/experiments_viewer/threads.csv", expThreads);
  expStatus = "Exported to assets/experiments_viewer/ (plot with tools/plot_results.py).";
}
