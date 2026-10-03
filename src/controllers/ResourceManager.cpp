#include "controllers/ResourceManager.h"

std::shared_ptr<Shader> ResourceManager::GetShader(const std::string &shaderSrc)
{
    auto it = shaders.find(shaderSrc);
    if (it != shaders.end())
    {
        return it->second;
    }

    std::shared_ptr<Shader> shader = std::make_shared<Shader>(shaderSrc);
    shaders[shaderSrc] = shader;
    return shader;
}

std::shared_ptr<Mesh> ResourceManager::GetMesh(const std::string &meshSrc)
{
    auto it = meshes.find(meshSrc);
    if (it != meshes.end())
    {
        return it->second;
    }

    std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>(meshSrc);
    meshes[meshSrc] = mesh;
    return mesh;
}

void ResourceManager::Shutdown()
{
    // Clear meshes before shaders while the OpenGL context is still current.
    meshes.clear();
    shaders.clear();
}
