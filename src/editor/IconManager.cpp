#include "IconManager.h"
#include <iostream>
#include <filesystem>

// stb_image implementation (must be in exactly ONE .cpp file)
#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"

namespace fs = std::filesystem;
IconManager g_Icons;

void IconManager::LoadIcons(const std::string& folderPath) {
    if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
        std::cout << "[Icons] Folder not found: " << folderPath << std::endl;
        return;
    }

    for (const auto& entry : fs::directory_iterator(folderPath)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        // Convert to lowercase
        for (char& c : ext) c = (char)tolower(c);
        if (ext != ".png" && ext != ".jpg" && ext != ".bmp") continue;

        std::string className = entry.path().stem().string();
        std::string fullPath = entry.path().string();

        int width, height, channels;
        unsigned char* pixels = stbi_load(fullPath.c_str(), &width, &height, &channels, 4);

        if (!pixels) {
            std::cerr << "[Icons] Failed to load: " << fullPath << std::endl;
            continue;
        }

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        stbi_image_free(pixels);
        glBindTexture(GL_TEXTURE_2D, 0);

        icons[className] = texture;
        std::cout << "[Icons] Loaded: " << className << " (" << width << "x" << height << ")" << std::endl;
    }

    std::cout << "[Icons] " << icons.size() << " icons loaded from " << folderPath << std::endl;
}

GLuint IconManager::GetIcon(const std::string& className) const {
    auto it = icons.find(className);
    if (it != icons.end()) return it->second;
    return 0;
}

bool IconManager::HasIcon(const std::string& className) const {
    return icons.count(className) > 0;
}

void IconManager::Shutdown() {
    for (auto& [name, tex] : icons) {
        glDeleteTextures(1, &tex);
    }
    icons.clear();
}