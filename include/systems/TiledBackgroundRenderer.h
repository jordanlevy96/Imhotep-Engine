/**
 * @file TiledBackgroundRenderer.h
 * @brief World-centric tiled background renderer (procedural tiles + GPU cache)
 * @lines ~220
 *
 * Quick-stats (Public API):
 * - Initialize() - Create GL resources (line ~70)
 * - Shutdown() - Destroy GL resources (line ~85)
 * - Configure() - Update renderer config (line ~95)
 * - SetEnabled() - Toggle rendering (line ~105)
 * - Render() - Select tiles, upload budget, draw instanced tile grids (line ~120)
 *
 * Purpose:
 * - Renders tiled backgrounds with a GPU cache,
 *   but with procedural CPU tile generation for a vaporwave-style background.
 *
 * Notes:
 * - Camera-centric clipmaps are explicitly avoided: tiles are keyed by stable (lod, x, y).
 * - Current implementation targets a single LOD (lod=0) as a milestone-0 foundation.
 */

#pragma once

#include "Camera.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Shader;

/**
 * @brief Configuration for tiled background renderer
 */
struct TiledBackgroundConfig
{
    bool enabled = false;         ///< Master toggle (renderer no-ops when disabled)
    float planeY = 0.0f;          ///< World Y coordinate for the ground plane
    float farDistance = 600.0f;   ///< Horizon distance for the ground grid (world units, along camera forward)
    float widthMultiplier = 1.2f; ///< Expands computed view width for safety

    int tileResolution = 256;   ///< Tile texture size (pixels, square)
    int heightResolution = 64;  ///< Height texture size (pixels, square; used for mountains/displacement)
    int cacheSlots = 96;        ///< Max tiles resident on GPU (texture array layers)
    int maxUploadsPerFrame = 2; ///< Budget for new tile uploads per frame

    float tileWorldSize = 512.0f; ///< Base world units covered by one tile edge (lod=0, coarsest)
    int lodCount = 4;            ///< Number of LOD levels (lod=0..lodCount-1)
    float lodScale = 2.0f;       ///< Tile size multiplier per LOD (typically 2.0)
    float lodSplitFactor = 6.0f; ///< Subdivide when dist < tileSize*factor

    int meshResolution = 32;     ///< Tile mesh resolution (segments per tile edge; higher = smoother mountains)

    // Vaporwave grid parameters (world-space)
    float gridSpacing = 4.0f;     ///< Minor grid spacing (world units between cyan lines)
    int majorEvery = 8;           ///< Every N minor lines becomes a major line (magenta)
    float minorLineWidth = 0.06f; ///< Minor line half-width (world units)
    float majorLineWidth = 0.12f; ///< Major line half-width (world units)

    glm::vec4 baseColorA = glm::vec4(0.08f, 0.02f, 0.12f, 1.0f);   ///< Near color
    glm::vec4 baseColorB = glm::vec4(0.20f, 0.02f, 0.25f, 1.0f);   ///< Far color
    glm::vec4 minorLineColor = glm::vec4(0.0f, 0.85f, 1.0f, 1.0f); ///< Cyan
    glm::vec4 majorLineColor = glm::vec4(1.0f, 0.0f, 0.80f, 1.0f); ///< Magenta

    // Horizon line
    //
    // The simplest synthwave look is: grid extends to the far edge and terminates on a crisp magenta line.
    // We compute the line in the shader at forward distance ~= farDistance and anti-alias it in screen-space.
    float horizonLinePixels = 2.0f; ///< Approximate horizon line thickness in pixels (AA included)

    // Mountains (terrain displacement beyond the horizon)
    //
    // Mountains are rendered by extending tile selection beyond farDistance and displacing vertices in the shader using
    // a height texture layer.
    float mountainExtraDistance = 0.0f; ///< Extra distance beyond farDistance to render displaced terrain (world units)
    float mountainHeight = 26.0f;       ///< Peak height above planeY (world units)
    float mountainNoiseScale = 0.012f;  ///< Procedural noise scale (world units -> noise space); prefer mountainFeatureSize for tuning
    float mountainFeatureSize = 0.0f;   ///< If >0, overrides mountainNoiseScale via (noiseScale = 1 / mountainFeatureSize)
    float mountainDetail = 0.55f;       ///< Ruggedness [0..1] (0=smooth hills, 1=sharp ridges; also increases ridge frequency)
    float mountainFadeDistance = 24.0f; ///< How quickly mountains rise after the horizon (world units)
    float mountainRiseExponent = 0.65f; ///< Exponent shaping the rise curve (>0). Lower = rises sooner near horizon
    float skirtDepth = 0.5f;            ///< Skirt drop depth (world units) to hide LOD cracks in displaced regions
    float mountainGridScale = 4.0f;     ///< Multiplies gridSpacing for the mountain region (reduces line moiré at distance)
    float mountainMajorStrength = 0.0f; ///< [0..1] Major-line strength on mountains (0 disables magenta major lines)

    // Mountain "composition" (optional)
    //
    // For the synthwave background, it can be useful to bias the height field so there are two dominant mountain ranges
    // (left and right) with a flatter valley in the center. This is implemented as a world-X mask applied during height
    // tile generation, so it remains world-centric and deterministic.
    float mountainSideStrength = 0.0f; ///< [0..1] Blend strength for the side-range mask (0=disable, 1=full)
    float mountainSideOffsetX = 80.0f; ///< Valley half-width (world units around X=0). Larger pushes mountains outward
    float mountainSideWidthX = 80.0f;  ///< Transition softness at valley boundary (world units). Larger reduces "trench" look
    float mountainSideBase = 0.18f;    ///< Valley-center amplitude multiplier [0..1] (0=flat center, 0.1=some hills)
};

/**
 * @brief Renderer that draws a ground plane using cached procedural tiles
 */
class TiledBackgroundRenderer
{
public:
    /**
     * @brief Stats snapshot for debugging/perf verification
     */
    struct Stats
    {
        std::uint64_t frameIndex = 0;
        int visibleCandidates = 0; ///< Pre-cap candidate tiles (post-quadtree)
        int selectedTiles = 0;     ///< Tiles actually submitted (<= cacheSlots)
        int residentTiles = 0;     ///< Occupied cache slots
        int pendingUploads = 0;    ///< Tiles queued to upload
        int uploadsThisFrame = 0;  ///< Tiles uploaded this frame
        int cacheHits = 0;         ///< Resolve hits this frame
        int cacheMisses = 0;       ///< Resolve misses this frame
        int evictions = 0;         ///< LRU evictions this frame
    };

    /**
     * @brief Get singleton instance
     */
    static TiledBackgroundRenderer &GetInstance()
    {
        static TiledBackgroundRenderer instance;
        return instance;
    }

    TiledBackgroundRenderer(TiledBackgroundRenderer const &) = delete;
    void operator=(TiledBackgroundRenderer const &) = delete;

    ~TiledBackgroundRenderer();

    /**
     * @brief Initialize GL resources (safe to call multiple times)
     * @return true if initialized successfully
     */
    bool Initialize();

    /**
     * @brief Destroy GL resources
     */
    void Shutdown();

    /**
     * @brief Configure renderer (recreates GPU cache if needed)
     * @param config New configuration
     */
    void Configure(const TiledBackgroundConfig &config);

    /**
     * @brief Enable/disable rendering
     */
    void SetEnabled(bool enabled);

    /**
     * @brief Get whether rendering is enabled
     */
    bool IsEnabled() const { return m_config.enabled; }

    /**
     * @brief Render tiled background (no-op if disabled/uninitialized)
     * @param camera Active camera
     * @param deltaMs Frame delta in milliseconds (for future animation hooks)
     */
    void Render(Camera *camera, float deltaMs);

    /**
     * @brief Get latest stats snapshot (updated during Render)
     */
    Stats GetLastStats() const { return m_lastStats; }

private:
    TiledBackgroundRenderer() = default;

    struct TileKey
    {
        int lod = 0;
        int x = 0;
        int y = 0;

        bool operator==(const TileKey &other) const
        {
            return lod == other.lod && x == other.x && y == other.y;
        }
    };

    struct TileInstance
    {
        glm::vec2 originXZ = glm::vec2(0.0f); ///< world-space origin (x,z)
        float layer = 0.0f;                   ///< texture array layer index (float for attrib)
        float hasData = 0.0f;                 ///< 1.0 if this layer contains valid tile data, else 0.0
        float tileWorldSize = 0.0f;           ///< world size for this tile instance
        float skirtMask = 0.0f;               ///< bitmask of edges needing skirts (0=west,1=east,2=south,3=north)
    };

    bool EnsureInitialized();
    void RecreateCacheIfNeeded();

    void SelectVisibleTiles(Camera *camera, std::vector<TileKey> &outKeys);
    int ResolveTileLayer(const TileKey &key, bool &outIsNew);

    void UploadPendingTiles();
    void GenerateTileRGBA8(const TileKey &key, std::vector<std::uint8_t> &outPixels) const;
    void GenerateTileHeightR16(const TileKey &key, std::vector<std::uint16_t> &outPixels) const;

    float GetTileWorldSizeForLOD(int lod) const;

    void EnsureDrawResources();
    void DrawTiles(Camera *camera, const std::vector<TileInstance> &instances) const;

    struct TileSlot
    {
        bool occupied = false;
        TileKey key{};
        std::uint64_t lastUsedFrame = 0;
        bool uploaded = false;
    };

    TiledBackgroundConfig m_config{};

    bool m_initialized = false;
    bool m_cacheDirty = true;
    bool m_meshDirty = true;

    std::uint64_t m_frameIndex = 0;
    std::uint32_t m_tileTextureArray = 0;
    std::uint32_t m_heightTextureArray = 0;

    Stats m_lastStats{};
    std::uint64_t m_lastStatsLogFrame = 0;

    std::vector<TileSlot> m_slots;
    std::unordered_map<std::uint64_t, int> m_keyToSlot; ///< packed key -> slot index
    std::vector<TileKey> m_pendingUploads;

    // Draw resources
    std::uint32_t m_vao = 0;
    std::uint32_t m_vbo = 0;
    std::uint32_t m_instanceVbo = 0;
    int m_tileVertexCount = 0;
    int m_instanceCapacity = 0;
    std::unique_ptr<Shader> m_shader;

    bool m_warnedTileOverflow = false;

    static std::uint64_t PackKey(const TileKey &key);
};
