#pragma once
#include <GL/glew.h>
#include "../core/Types.h"
#include <unordered_map>
#include "Camera.h"
#include <string>

class Renderer {
public:
    void Init();
    void Shutdown();

    void ResizeFBO(int width, int height);
    void BeginScene();
    void EndScene();
    void BlitToScreen(int width, int height);
    GLuint GetSceneTexture() const { return fboTexture; }
    GLuint GetFBO() const { return fbo; }

    void SetCamera(const Camera& cam, float aspect);
    void DrawPart(const Vector3& pos, const Vector3& size, const Vector3& orientation, const Color3& color, float transparency = 0.0f, const std::string& shape = "Block");
    void DrawGroundPlane();
    void DrawWireframe(const Vector3& pos, const Vector3& size, const Vector3& orientation, const Color3& color);

    // ── 2D UI Rendering System (Added in Phase 11b) ──
    void InitUI(const std::string& fontsFolder);
    void LoadFont(const std::string& name, const std::string& path);
    float MeasureTextWidth(const std::string& text, float size, const std::string& fontName);
    void Begin2D(int screenWidth, int screenHeight);
    void End2D();
    void DrawRect2D(float x, float y, float w, float h, const Color3& color, float transparency = 0.0f);
    void DrawText2D(const std::string& text, float x, float y, float size, const Color3& color, float transparency = 0.0f, const std::string& fontName = "Roboto");
    void DrawImage2D(GLuint texture, float x, float y, float w, float h, const Color3& color, float transparency = 0.0f);

private:
    // 3D Resources
    GLuint shader = 0;
    GLint locModel, locView, locProj, locColor, locLightDir, locAlpha;
    GLuint cubeVAO = 0, cubeVBO = 0, cubeEBO = 0;
    GLuint sphereVAO = 0, sphereVBO = 0, sphereEBO = 0;
    int sphereIndexCount = 0;
    GLuint cylinderVAO = 0, cylinderVBO = 0, cylinderEBO = 0;
    int cylinderIndexCount = 0;
    GLuint wedgeVAO = 0, wedgeVBO = 0, wedgeEBO = 0;
    GLuint planeVAO = 0, planeVBO = 0;

    // FBO Resources
    GLuint fbo = 0, fboTexture = 0, fboDepth = 0;
    int fboW = 0, fboH = 0;
    float viewMat[16], projMat[16];

    // 2D UI Resources
    GLuint shader2D = 0;
    GLint locModel2D, locProj2D, locColor2D, locAlpha2D, locUseTex2D;
    GLuint quad2D_VAO = 0, quad2D_VBO = 0;

    // Multi-font system (Phase 11d)
    struct FontData {
        GLuint texture = 0;
        void* bakedChars = nullptr;
        int bakeSize = 48; // Baked at high res for sharpness
    };
    std::unordered_map<std::string, FontData> fonts;

    void CreateShaders();
    void CreateMeshes();
    void CreateFBO();
};