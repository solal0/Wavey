#pragma once
#include <GL/glew.h>
#include <string>
#include <unordered_map>

class IconManager {
public:
    void LoadIcons(const std::string& folderPath);
    GLuint GetIcon(const std::string& className) const;
    bool HasIcon(const std::string& className) const;
    void Shutdown();

private:
    std::unordered_map<std::string, GLuint> icons;
};

extern IconManager g_Icons;