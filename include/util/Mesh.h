/**
 * @file Mesh.h
 * @brief 3D mesh loading and OpenGL buffer management
 * @lines ~65
 *
 * Quick-stats (Public API):
 * - Mesh() - Load and upload an OBJ mesh
 * - AddTexture() - Load and attach a texture
 *
 * Implementation reference: See src/util/Mesh.cpp
 */

#pragma once

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL // allows vec3 in unordered_map
#include "glm/gtx/hash.hpp"

#include <tiny_obj_loader.h>

/**
 * @brief 3D mesh wrapper with OpenGL vertex/index buffers
 *
 * Loads mesh data from .obj files using tinyobjloader and uploads to GPU.
 * Vertex format: position (vec3) + normal (vec3) = 6 floats per vertex.
 *
 * @note Currently does not support texture coordinates (TODO in implementation)
 * @note Vertex data is stored interleaved: [pos.xyz, normal.xyz, pos.xyz, normal.xyz, ...]
 *
 * Usage example:
 * @code
 * Mesh* mesh = new Mesh("../res/models/cube.obj");
 * mesh->AddTexture("../res/textures/wall.png", false);
 *
 * glBindVertexArray(mesh->VAO);
 * glDrawElements(GL_TRIANGLES, mesh->indices.size(), GL_UNSIGNED_INT, 0);
 * @endcode
 */
class Mesh
{
public:
    /**
     * @brief Load mesh from .obj file and upload to GPU
     * @param filepath Path to .obj file (relative to executable)
     * @throws std::runtime_error if loading fails or faces lack valid positions/normals
     * @note Creates and binds VAO, VBO, EBO automatically
     */
    Mesh(const std::string &filepath);

    /**
     * @brief Cleanup OpenGL resources
     * @note Deletes VAO, VBO, EBO, and all loaded textures
     */
    ~Mesh();

    /**
     * @brief Load and attach a texture to this mesh
     * @param textureSrc Path to texture file (png, jpg, etc.)
     * @param alpha True if texture has alpha channel (RGBA), false for RGB
     * @note Uses stb_image for loading. Generates mipmaps automatically.
     */
    void AddTexture(const std::string &textureSrc, bool alpha);

    unsigned int VAO;                           ///< OpenGL Vertex Array Object
    unsigned int EBO;                           ///< OpenGL Element Buffer Object (indices)
    std::vector<unsigned int> textures;         ///< OpenGL texture IDs (can have multiple)
    std::vector<float> vertices;                ///< Interleaved vertex data (pos.xyz, normal.xyz)
    std::vector<unsigned int> indices;          ///< Triangle indices for glDrawElements

private:
    unsigned int VBO;                           ///< OpenGL Vertex Buffer Object
};
