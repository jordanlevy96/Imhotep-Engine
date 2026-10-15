/**
 * @file Mesh.cpp
 * @brief Parse OBJ geometry, validate vertex references, and upload mesh buffers.
 * @lines ~158
 *
 * Key functions:
 * - Mesh() - Load OBJ data and create OpenGL buffers (line ~13)
 * - AddTexture() - Load and attach a texture image
 */
#define TINYOBJLOADER_USE_CPP11
#define TINYOBJLOADER_IMPLEMENTATION

#include "util/Mesh.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <glad/glad.h>

#include <iostream>
#include <stdexcept>

Mesh::Mesh(const std::string &filepath)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filepath.c_str()))
    {
        if (!warn.empty())
        {
            std::cerr << "Warning: " << warn << std::endl;
        }
        if (!err.empty())
        {
            std::cerr << "Error: " << err << std::endl;
        }
        throw std::runtime_error("Failed to load mesh: " + filepath + ": " + err);
    }

    unsigned int i = 0;

    for (const auto &shape : shapes)
    {
        for (const auto &index : shape.mesh.indices)
        {
            if (index.vertex_index < 0 || index.normal_index < 0 ||
                static_cast<size_t>(index.vertex_index) >= attrib.vertices.size() / 3 ||
                static_cast<size_t>(index.normal_index) >= attrib.normals.size() / 3)
            {
                throw std::runtime_error("Mesh requires valid position and normal indices: " + filepath);
            }
            glm::vec4 pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2],
                1};

            glm::vec3 normal = {
                attrib.normals[3 * index.normal_index + 0],
                attrib.normals[3 * index.normal_index + 1],
                attrib.normals[3 * index.normal_index + 2],
            };

            // TODO: handle textures
            // glm::vec2 texCoord = {
            //     attrib.texcoords[2 * index.texcoord_index + 0],
            //     attrib.texcoords[2 * index.texcoord_index + 1],
            // };

            vertices.push_back(pos.x);
            vertices.push_back(pos.y);
            vertices.push_back(pos.z);
            vertices.push_back(normal.x);
            vertices.push_back(normal.y);
            vertices.push_back(normal.z);
            // vertices.push_back(texCoord.x);
            // vertices.push_back(texCoord.y);

            indices.push_back(i++);
        }
    }

    if (indices.empty())
    {
        throw std::runtime_error("Mesh contains no faces: " + filepath);
    }

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * indices.size(), indices.data(), GL_STATIC_DRAW);

    // position attribute (3 floats)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void *)0);
    glEnableVertexAttribArray(0);

    // normal attribute (3 floats)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // // texture coord attribute (2 floats)
    // glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, (void *)(6 * sizeof(float)));
    // glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);
}

Mesh::~Mesh()
{
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);

    vertices.clear();

    for (GLuint texture : textures)
    {
        glDeleteTextures(1, &texture);
    }
    textures.clear();
}

static GLuint loadTexture(const std::string &filepath, bool alpha)
{
    GLuint texture;
    int width, height, numChannels;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    unsigned char *data = stbi_load(filepath.c_str(), &width, &height, &numChannels, 0);
    GLenum format = alpha ? GL_RGBA : GL_RGB;
    if (data)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
    else
    {
        std::cerr << "Failed to load texture" << std::endl;
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    return texture;
}

void Mesh::AddTexture(const std::string &textureSrc, bool alpha)
{
    textures.push_back(loadTexture(textureSrc, alpha));
}
