#include "App.hpp"

#include <OPPCH.h>

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
  listener->onKeyUpCallback = [&](SDL_Keycode key) { splat->sort(getViewModelMatrix()); };
  listener->onMouseLeftReleaseCallback = [&]() { splat->sort(getViewModelMatrix()); };
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
    if (isRotating) splat->sort(getViewModelMatrix());
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

  // Use premultiplied alpha for Gaussian Splats
  glEnable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  splatShader->use();
  glUniformMatrix4fv(glGetUniformLocation(splatShader->ID, "modelMatrix"), 1, GL_FALSE,
                     glm::value_ptr(splatModelMatrix));
  glUniform1f(glGetUniformLocation(splatShader->ID, "scaleFactor"), scaleFactor);
  glUniform2f(glGetUniformLocation(splatShader->ID, "Resolution"), (float)width, (float)height);
  glUniform2f(glGetUniformLocation(splatShader->ID, "Focal"), focal_x, focal_y);
  glUniform2f(glGetUniformLocation(splatShader->ID, "TanFov"), tan_fovx, tan_fovy);
  // calc box bounds location
  glm::vec3 boxMin = glm::vec3(boxPosition[0] - boxSize[0], boxPosition[1] - boxSize[1], boxPosition[2] - boxSize[2]);
  glm::vec3 boxMax = glm::vec3(boxPosition[0] + boxSize[0], boxPosition[1] + boxSize[1], boxPosition[2] + boxSize[2]);
  glUniform3fv(glGetUniformLocation(splatShader->ID, "BoxMin"), 1, glm::value_ptr(boxMin));
  glUniform3fv(glGetUniformLocation(splatShader->ID, "BoxMax"), 1, glm::value_ptr(boxMax));
  glUniform1i(glGetUniformLocation(splatShader->ID, "showSelectionBox"), showSelectionBox ? 1 : 0);
  camera->update(splatShader.get());
  splat->draw(splatShader.get());

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
  if (camera) splat->sort(getViewModelMatrix());
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
    ImGui::TextDisabled("Tab switches A/B");
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

  // ---- Change detection ----
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (!ImGui::CollapsingHeader("Change detection")) return;
  ImGui::SliderInt("Resolution", &detectParams.resolution, 32, 256);
  ImGui::SliderFloat("Threshold tau", &detectParams.tau, 0.05f, 0.95f);
  ImGui::SliderInt("Tolerance (voxels)", &detectParams.tolerance, 0, 2);
  ImGui::SliderInt("Min region voxels", &detectParams.minVoxels, 1, 100);
  ImGui::Checkbox("Occupancy 1-exp(-Phi)", &detectParams.occupancy);
  ImGui::SameLine();
  ImGui::Checkbox("Anti-alias", &detectParams.antiAlias);
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
  camera->update(boxShader.get());
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
  splat->sort(getViewModelMatrix());  // depth order for the new viewpoint
}
