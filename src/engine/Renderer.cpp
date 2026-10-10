#include "Renderer.h"
#include <filesystem>
#include <iostream>
#include <cstring>
#include <cmath>
#include <vector>
#include <fstream>

// STB TrueType implementation (Single-file compile flag)
#define STB_TRUETYPE_IMPLEMENTATION
#include "../stb_truetype.h"

// ═══════════════════════════════════════════════════════
// Shader Sources
// ═══════════════════════════════════════════════════════
static const char* vertSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vNormal;
void main() {
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
}
)GLSL";

static const char* fragSrc = R"GLSL(
#version 330 core
in vec3 vNormal;
uniform vec3 uColor;
uniform vec3 uLightDir;
uniform float uAlpha;
out vec4 FragColor;
void main() {
    vec3 n = normalize(vNormal);
    float diff = max(dot(n, normalize(uLightDir)), 0.0);
    vec3 col = uColor * 0.35 + uColor * diff * 0.65;
    FragColor = vec4(col, uAlpha);
}
)GLSL";

// 2D UI Shaders (Added in Phase 11b)
static const char* vert2DSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
uniform mat4 uProj;
uniform mat4 uModel;
out vec2 vTexCoord;
void main() {
    vTexCoord = aTexCoord;
    gl_Position = uProj * uModel * vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* frag2DSrc = R"GLSL(
#version 330 core
in vec2 vTexCoord;
uniform vec3 uColor;
uniform float uAlpha;
uniform int uUseTex;
uniform sampler2D uTex;
out vec4 FragColor;
void main() {
    if (uUseTex == 1) {
        float alpha = texture(uTex, vTexCoord).r; // Font atlas alpha
        FragColor = vec4(uColor, alpha * uAlpha);
    } else if (uUseTex == 2) {
        FragColor = texture(uTex, vTexCoord) * vec4(uColor, uAlpha); // Colored image
    } else {
        FragColor = vec4(uColor, uAlpha); // Flat colored rect
    }
}
)GLSL";

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "[Wavey] Shader error: " << log << std::endl;
    }
    return s;
}

// ═══════════════════════════════════════════════════════
// Initialization / Shutdown
// ═══════════════════════════════════════════════════════
void Renderer::Init() {
    CreateShaders();
    CreateMeshes();
    CreateFBO();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    std::cout << "[Wavey] Renderer initialized (OpenGL 3.3)" << std::endl;
}

void Renderer::Shutdown() {
    glDeleteProgram(shader);
    glDeleteProgram(shader2D);
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);
    glDeleteBuffers(1, &cubeEBO);
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);
    glDeleteVertexArrays(1, &cylinderVAO);
    glDeleteBuffers(1, &cylinderVBO);
    glDeleteBuffers(1, &cylinderEBO);
    glDeleteVertexArrays(1, &wedgeVAO);
    glDeleteBuffers(1, &wedgeVBO);
    glDeleteBuffers(1, &wedgeEBO);
    glDeleteVertexArrays(1, &planeVAO);
    glDeleteBuffers(1, &planeVBO);
    glDeleteVertexArrays(1, &quad2D_VAO);
    glDeleteBuffers(1, &quad2D_VBO);
    for (auto& [name, fd] : fonts) {
        glDeleteTextures(1, &fd.texture);
        if (fd.bakedChars) delete[] (stbtt_bakedchar*)fd.bakedChars;
    }
    fonts.clear();
}

// ═══════════════════════════════════════════════════════
// UI Initialization & TrueType Font Baking (stb_truetype)
// ═══════════════════════════════════════════════════════
void Renderer::InitUI(const std::string& fontsFolder) {
    namespace fs = std::filesystem;

    // Create 2D quad VAO once
    float quadVertices[] = {
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 1.0f,  0.0f, 1.0f
    };
    glGenVertexArrays(1, &quad2D_VAO);
    glGenBuffers(1, &quad2D_VBO);
    glBindVertexArray(quad2D_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, quad2D_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
    glBindVertexArray(0);

    // Load each font by name
    LoadFont("Roboto",         fontsFolder + "/Roboto-Regular.ttf");
    LoadFont("RobotoBold",     fontsFolder + "/Roboto-Bold.ttf");
    LoadFont("OpenSans",       fontsFolder + "/OpenSans-Regular.ttf");
    LoadFont("JetBrainsMono",  fontsFolder + "/JetBrainsMono-Regular.ttf");

    std::cout << "[Wavey UI] " << fonts.size() << " fonts loaded." << std::endl;
}

void Renderer::LoadFont(const std::string& name, const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[Wavey UI] Could not open font: " << path << std::endl;
        return;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> fontBuffer(size);
    if (!file.read((char*)fontBuffer.data(), size)) return;

    FontData fd;
    fd.bakeSize = 48; // High-res bake for sharper rendering at any size
    const int atlasSize = 1024;
    std::vector<unsigned char> bitmap(atlasSize * atlasSize);
    stbtt_bakedchar* chars = new stbtt_bakedchar[96];
    stbtt_BakeFontBitmap(fontBuffer.data(), 0, (float)fd.bakeSize, bitmap.data(), atlasSize, atlasSize, 32, 96, chars);
    fd.bakedChars = (void*)chars;

    glGenTextures(1, &fd.texture);
    glBindTexture(GL_TEXTURE_2D, fd.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasSize, atlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    fonts[name] = fd;
    std::cout << "[Wavey UI] Font loaded: " << name << std::endl;
}

float Renderer::MeasureTextWidth(const std::string& text, float size, const std::string& fontName) {
    auto it = fonts.find(fontName);
    if (it == fonts.end()) {
        it = fonts.find("Roboto");
        if (it == fonts.end()) return 0.0f;
    }
    stbtt_bakedchar* chars = (stbtt_bakedchar*)it->second.bakedChars;
    float scale = size / (float)it->second.bakeSize;
    float width = 0.0f;
    for (char c : text) {
        if (c < 32 || c >= 128) continue;
        width += chars[c - 32].xadvance * scale;
    }
    return width;
}

// ═══════════════════════════════════════════════════════
// Shader compilation & pipeline assembly
// ═══════════════════════════════════════════════════════
void Renderer::CreateShaders() {
    // 3D Shader
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragSrc);
    shader = glCreateProgram();
    glAttachShader(shader, vs);
    glAttachShader(shader, fs);
    glLinkProgram(shader);
    glDeleteShader(vs);
    glDeleteShader(fs);

    locModel    = glGetUniformLocation(shader, "uModel");
    locView     = glGetUniformLocation(shader, "uView");
    locProj     = glGetUniformLocation(shader, "uProj");
    locColor    = glGetUniformLocation(shader, "uColor");
    locLightDir = glGetUniformLocation(shader, "uLightDir");
    locAlpha    = glGetUniformLocation(shader, "uAlpha");

    // 2D Shader (Added in Phase 11b)
    GLuint vs2D = CompileShader(GL_VERTEX_SHADER, vert2DSrc);
    GLuint fs2D = CompileShader(GL_FRAGMENT_SHADER, frag2DSrc);
    shader2D = glCreateProgram();
    glAttachShader(shader2D, vs2D);
    glAttachShader(shader2D, fs2D);
    glLinkProgram(shader2D);
    glDeleteShader(vs2D);
    glDeleteShader(fs2D);

    locProj2D   = glGetUniformLocation(shader2D, "uProj");
    locModel2D  = glGetUniformLocation(shader2D, "uModel");
    locColor2D  = glGetUniformLocation(shader2D, "uColor");
    locAlpha2D  = glGetUniformLocation(shader2D, "uAlpha");
    locUseTex2D = glGetUniformLocation(shader2D, "uUseTex");
}

void Renderer::CreateMeshes() {
    float cube[] = {
        -0.5f, -0.5f,  0.5f,  0, 0, 1,
         0.5f, -0.5f,  0.5f,  0, 0, 1,
         0.5f,  0.5f,  0.5f,  0, 0, 1,
        -0.5f,  0.5f,  0.5f,  0, 0, 1,
         0.5f, -0.5f, -0.5f,  0, 0,-1,
        -0.5f, -0.5f, -0.5f,  0, 0,-1,
        -0.5f,  0.5f, -0.5f,  0, 0,-1,
         0.5f,  0.5f, -0.5f,  0, 0,-1,
        -0.5f,  0.5f,  0.5f,  0, 1, 0,
         0.5f,  0.5f,  0.5f,  0, 1, 0,
         0.5f,  0.5f, -0.5f,  0, 1, 0,
        -0.5f,  0.5f, -0.5f,  0, 1, 0,
        -0.5f, -0.5f, -0.5f,  0,-1, 0,
         0.5f, -0.5f, -0.5f,  0,-1, 0,
         0.5f, -0.5f,  0.5f,  0,-1, 0,
        -0.5f, -0.5f,  0.5f,  0,-1, 0,
         0.5f, -0.5f,  0.5f,  1, 0, 0,
         0.5f, -0.5f, -0.5f,  1, 0, 0,
         0.5f,  0.5f, -0.5f,  1, 0, 0,
         0.5f,  0.5f,  0.5f,  1, 0, 0,
        -0.5f, -0.5f, -0.5f, -1, 0, 0,
        -0.5f, -0.5f,  0.5f, -1, 0, 0,
        -0.5f,  0.5f,  0.5f, -1, 0, 0,
        -0.5f,  0.5f, -0.5f, -1, 0, 0,
    };
    unsigned int indices[] = {
         0, 1, 2,  0, 2, 3,
         4, 5, 6,  4, 6, 7,
         8, 9,10,  8,10,11,
        12,13,14, 12,14,15,
        16,17,18, 16,18,19,
        20,21,22, 20,22,23,
    };

    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    glGenBuffers(1, &cubeEBO);
    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
    glBindVertexArray(0);

    float S = 100.0f;
    float plane[] = {
        -S, 0, -S,  0, 1, 0,
         S, 0, -S,  0, 1, 0,
         S, 0,  S,  0, 1, 0,
        -S, 0,  S,  0, 1, 0,
    };
    glGenVertexArrays(1, &planeVAO);
    glGenBuffers(1, &planeVBO);
    glBindVertexArray(planeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, planeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(plane), plane, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
    glBindVertexArray(0);

    // ── Sphere ──
    {
        std::vector<float> verts;
        std::vector<unsigned int> idx;
        const int latSteps = 16, lonSteps = 16;
        for (int i = 0; i <= latSteps; i++) {
            float lat = 3.14159265f * (-0.5f + (float)i / latSteps);
            float y = sinf(lat);
            float r = cosf(lat);
            for (int j = 0; j <= lonSteps; j++) {
                float lon = 2.0f * 3.14159265f * (float)j / lonSteps;
                float x = r * cosf(lon);
                float z = r * sinf(lon);
                verts.push_back(x * 0.5f); verts.push_back(y * 0.5f); verts.push_back(z * 0.5f);
                verts.push_back(x); verts.push_back(y); verts.push_back(z);
            }
        }
        for (int i = 0; i < latSteps; i++) {
            for (int j = 0; j < lonSteps; j++) {
                int a = i * (lonSteps + 1) + j;
                int b = a + lonSteps + 1;
                idx.push_back(a); idx.push_back(b); idx.push_back(a + 1);
                idx.push_back(b); idx.push_back(b + 1); idx.push_back(a + 1);
            }
        }
        sphereIndexCount = (int)idx.size();

        glGenVertexArrays(1, &sphereVAO);
        glGenBuffers(1, &sphereVBO);
        glGenBuffers(1, &sphereEBO);
        glBindVertexArray(sphereVAO);
        glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
        glBindVertexArray(0);
    }

    // ── Cylinder ──
    {
        std::vector<float> verts;
        std::vector<unsigned int> idx;
        const int segs = 24;
        for (int i = 0; i <= segs; i++) {
            float ang = 2.0f * 3.14159265f * (float)i / segs;
            float y = cosf(ang) * 0.5f;
            float z = sinf(ang) * 0.5f;
            verts.push_back(0.5f); verts.push_back(y); verts.push_back(z);
            verts.push_back(0); verts.push_back(cosf(ang)); verts.push_back(sinf(ang));
            verts.push_back(-0.5f); verts.push_back(y); verts.push_back(z);
            verts.push_back(0); verts.push_back(cosf(ang)); verts.push_back(sinf(ang));
        }
        for (int i = 0; i < segs; i++) {
            int a = i * 2;
            int b = a + 1;
            int c = a + 2;
            int d = a + 3;
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
        int frontCenterIdx = (int)verts.size() / 6;
        verts.push_back(0.5f); verts.push_back(0); verts.push_back(0);
        verts.push_back(1); verts.push_back(0); verts.push_back(0);
        int backCenterIdx = (int)verts.size() / 6;
        verts.push_back(-0.5f); verts.push_back(0); verts.push_back(0);
        verts.push_back(-1); verts.push_back(0); verts.push_back(0);
        for (int i = 0; i < segs; i++) {
            int a = i * 2;
            int c = a + 2;
            idx.push_back(frontCenterIdx); idx.push_back(a); idx.push_back(c);
            idx.push_back(backCenterIdx); idx.push_back(c + 1); idx.push_back(a + 1);
        }
        cylinderIndexCount = (int)idx.size();

        glGenVertexArrays(1, &cylinderVAO);
        glGenBuffers(1, &cylinderVBO);
        glGenBuffers(1, &cylinderEBO);
        glBindVertexArray(cylinderVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cylinderVBO);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cylinderEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
        glBindVertexArray(0);
    }

    // ── Wedge ──
    {
        float wedge[] = {
            -0.5f, -0.5f, -0.5f,  0,-1,0,
             0.5f, -0.5f, -0.5f,  0,-1,0,
             0.5f, -0.5f,  0.5f,  0,-1,0,
            -0.5f, -0.5f,  0.5f,  0,-1,0,
            -0.5f, -0.5f,  0.5f,  0,0,1,
             0.5f, -0.5f,  0.5f,  0,0,1,
             0.5f,  0.5f,  0.5f,  0,0,1,
            -0.5f,  0.5f,  0.5f,  0,0,1,
            -0.5f, -0.5f, -0.5f,  0, 0.707f, -0.707f,
             0.5f, -0.5f, -0.5f,  0, 0.707f, -0.707f,
             0.5f,  0.5f,  0.5f,  0, 0.707f, -0.707f,
            -0.5f,  0.5f,  0.5f,  0, 0.707f, -0.707f,
            -0.5f, -0.5f, -0.5f, -1,0,0,
            -0.5f, -0.5f,  0.5f, -1,0,0,
            -0.5f,  0.5f,  0.5f, -1,0,0,
             0.5f, -0.5f, -0.5f,  1,0,0,
             0.5f, -0.5f,  0.5f,  1,0,0,
             0.5f,  0.5f,  0.5f,  1,0,0,
        };
        unsigned int wedgeIdx[] = {
            0, 1, 2,  0, 2, 3,
            4, 5, 6,  4, 6, 7,
            8, 10, 9,  8, 11, 10,
            12, 13, 14,
            15, 17, 16,
        };

        glGenVertexArrays(1, &wedgeVAO);
        glGenBuffers(1, &wedgeVBO);
        glGenBuffers(1, &wedgeEBO);
        glBindVertexArray(wedgeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, wedgeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(wedge), wedge, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, wedgeEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(wedgeIdx), wedgeIdx, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
        glBindVertexArray(0);
    }
}

void Renderer::CreateFBO() {
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &fboTexture);
    glGenRenderbuffers(1, &fboDepth);
}

void Renderer::ResizeFBO(int w, int h) {
    if (w <= 0 || h <= 0 || (w == fboW && h == fboH)) return;
    fboW = w; fboH = h;

    glBindTexture(GL_TEXTURE_2D, fboTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindRenderbuffer(GL_RENDERBUFFER, fboDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTexture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::BeginScene() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, fboW, fboH);
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(shader);
}

void Renderer::EndScene() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::BlitToScreen(int width, int height) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::SetCamera(const Camera& cam, float aspect) {
    cam.GetViewMatrix(viewMat);
    cam.GetProjectionMatrix(aspect, projMat);
    glUniformMatrix4fv(locView, 1, GL_FALSE, viewMat);
    glUniformMatrix4fv(locProj, 1, GL_FALSE, projMat);
    glUniform3f(locLightDir, 0.4f, 0.8f, 0.3f);
}

void Renderer::DrawPart(const Vector3& pos, const Vector3& size, const Vector3& orientation, const Color3& color, float transparency, const std::string& shape) {
    float rx = orientation.X * 3.14159265f / 180.0f;
    float ry = orientation.Y * 3.14159265f / 180.0f;
    float rz = orientation.Z * 3.14159265f / 180.0f;

    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);

    float r00 = cy * cz;
    float r01 = cy * sz;
    float r02 = -sy;
    float r10 = sx * sy * cz - cx * sz;
    float r11 = sx * sy * sz + cx * cz;
    float r12 = sx * cy;
    float r20 = cx * sy * cz + sx * sz;
    float r21 = cx * sy * sz - sx * cz;
    float r22 = cx * cy;

    float model[16] = {
        r00 * size.X, r01 * size.X, r02 * size.X, 0,
        r10 * size.Y, r11 * size.Y, r12 * size.Y, 0,
        r20 * size.Z, r21 * size.Z, r22 * size.Z, 0,
        pos.X,        pos.Y,        pos.Z,        1
    };

    glUniformMatrix4fv(locModel, 1, GL_FALSE, model);
    glUniform3f(locColor, color.R, color.G, color.B);
    glUniform1f(locAlpha, 1.0f - transparency);

    if (shape == "Ball") {
        glBindVertexArray(sphereVAO);
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);
    } else if (shape == "Cylinder") {
        glBindVertexArray(cylinderVAO);
        glDrawElements(GL_TRIANGLES, cylinderIndexCount, GL_UNSIGNED_INT, 0);
    } else if (shape == "Wedge") {
        glBindVertexArray(wedgeVAO);
        glDrawElements(GL_TRIANGLES, 18, GL_UNSIGNED_INT, 0);
    } else {
        glBindVertexArray(cubeVAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
}

void Renderer::DrawGroundPlane() {
    float model[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    glUniformMatrix4fv(locModel, 1, GL_FALSE, model);
    glUniform3f(locColor, 0.38f, 0.55f, 0.27f);
    glUniform1f(locAlpha, 1.0f);

    glBindVertexArray(planeVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void Renderer::DrawWireframe(const Vector3& pos, const Vector3& size, const Vector3& orientation, const Color3& color) {
    float rx = orientation.X * 3.14159265f / 180.0f;
    float ry = orientation.Y * 3.14159265f / 180.0f;
    float rz = orientation.Z * 3.14159265f / 180.0f;

    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);

    float r00 = cy * cz, r01 = cy * sz, r02 = -sy;
    float r10 = sx * sy * cz - cx * sz, r11 = sx * sy * sz + cx * cz, r12 = sx * cy;
    float r20 = cx * sy * cz + sx * sz, r21 = cx * sy * sz - sx * cz, r22 = cx * cy;

    float e = 1.02f;
    float modelExp[16] = {
        r00 * size.X * e, r01 * size.X * e, r02 * size.X * e, 0,
        r10 * size.Y * e, r11 * size.Y * e, r12 * size.Y * e, 0,
        r20 * size.Z * e, r21 * size.Z * e, r22 * size.Z * e, 0,
        pos.X, pos.Y, pos.Z, 1
    };

    glUniformMatrix4fv(locModel, 1, GL_FALSE, modelExp);
    glUniform3f(locColor, color.R, color.G, color.B);
    glUniform1f(locAlpha, 1.0f);

    glBindVertexArray(cubeVAO);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(0);
}

// ═══════════════════════════════════════════════════════
// 2D UI Drawing Operations (Implemented in Phase 11b)
// ═══════════════════════════════════════════════════════
void Renderer::Begin2D(int w, int h) {
    glDisable(GL_DEPTH_TEST); // Render GUI on top of 3D
    glUseProgram(shader2D);

    // Build orthographic 2D screen projection matrix
    // Coordinates match pixel positions: (0,0) is top-left, (w,h) is bottom-right
    float proj2D[16] = {
        2.0f / w, 0.0f,      0.0f, 0.0f,
        0.0f,    -2.0f / h,  0.0f, 0.0f,
        0.0f,     0.0f,     -1.0f, 0.0f,
       -1.0f,     1.0f,      0.0f, 1.0f
    };
    glUniformMatrix4fv(locProj2D, 1, GL_FALSE, proj2D);
}

void Renderer::End2D() {
    glEnable(GL_DEPTH_TEST); // Restore 3D capabilities
}

void Renderer::DrawRect2D(float x, float y, float w, float h, const Color3& color, float transparency) {
    float model[16] = {
        w,    0.0f, 0.0f, 0.0f,
        0.0f, h,    0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        x,    y,    0.0f, 1.0f
    };
    glUniformMatrix4fv(locModel2D, 1, GL_FALSE, model);
    glUniform3f(locColor2D, color.R, color.G, color.B);
    glUniform1f(locAlpha2D, 1.0f - transparency);
    glUniform1i(locUseTex2D, 0); // Disable texture binding

    glBindVertexArray(quad2D_VAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void Renderer::DrawImage2D(GLuint texture, float x, float y, float w, float h, const Color3& color, float transparency) {
    float model[16] = {
        w,    0.0f, 0.0f, 0.0f,
        0.0f, h,    0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        x,    y,    0.0f, 1.0f
    };
    glUniformMatrix4fv(locModel2D, 1, GL_FALSE, model);
    glUniform3f(locColor2D, color.R, color.G, color.B);
    glUniform1f(locAlpha2D, 1.0f - transparency);
    glUniform1i(locUseTex2D, 2); // Set textured image draw state

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBindVertexArray(quad2D_VAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void Renderer::DrawText2D(const std::string& text, float x, float y, float size, const Color3& color, float transparency, const std::string& fontName) {
    auto it = fonts.find(fontName);
    if (it == fonts.end()) {
        it = fonts.find("Roboto");
        if (it == fonts.end()) return;
    }
    FontData& fd = it->second;
    if (!fd.bakedChars || fd.texture == 0) return;

    glUniform3f(locColor2D, color.R, color.G, color.B);
    glUniform1f(locAlpha2D, 1.0f - transparency);
    glUniform1i(locUseTex2D, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fd.texture);
    glBindVertexArray(quad2D_VAO);

    float scale = size / (float)fd.bakeSize;
    float currentX = x;
    stbtt_bakedchar* chars = (stbtt_bakedchar*)fd.bakedChars;

    for (char c : text) {
        if (c < 32 || c >= 128) continue;
        stbtt_bakedchar* b = &chars[c - 32];

        float w = (b->x1 - b->x0) * scale;
        float h = (b->y1 - b->y0) * scale;
        float xOffset = b->xoff * scale;
        float yOffset = b->yoff * scale;

        float charX = currentX + xOffset;
        float charY = y + yOffset + (float)fd.bakeSize * scale * 0.8f;

        float u0 = b->x0 / 1024.0f;
        float v0 = b->y0 / 1024.0f;
        float u1 = b->x1 / 1024.0f;
        float v1 = b->y1 / 1024.0f;

        float model[16] = {
            w,     0.0f,  0.0f, 0.0f,
            0.0f,  h,     0.0f, 0.0f,
            0.0f,  0.0f,  1.0f, 0.0f,
            charX, charY, 0.0f, 1.0f
        };
        glUniformMatrix4fv(locModel2D, 1, GL_FALSE, model);

        float vertices[] = {
            0.0f, 0.0f,  u0, v0,
            1.0f, 0.0f,  u1, v0,
            1.0f, 1.0f,  u1, v1,
            0.0f, 1.0f,  u0, v1
        };
        glBindBuffer(GL_ARRAY_BUFFER, quad2D_VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        currentX += b->xadvance * scale;
    }
    glBindVertexArray(0);
}
