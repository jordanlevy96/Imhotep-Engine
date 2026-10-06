#pragma once

#include "util/Shader.h"
#include "util/Mesh.h"

#include <memory>

class ResourceManager
{
public:
    static ResourceManager &GetInstance()
    {
        static ResourceManager instance;
        return instance;
    }

    std::shared_ptr<Shader> GetShader(const std::string &shaderSrc);
    std::shared_ptr<Mesh> GetMesh(const std::string &meshSrc);
    void Shutdown();

private:
    ResourceManager() = default;
    std::unordered_map<std::string, std::shared_ptr<Shader>> shaders;
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;

    // Make constructor, copy constructor, and assignment operator private
    ResourceManager(const ResourceManager &) = delete;
    ResourceManager &operator=(const ResourceManager &) = delete;
};
