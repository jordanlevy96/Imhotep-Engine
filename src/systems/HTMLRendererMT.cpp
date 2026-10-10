/**
 * @file HTMLRendererMT.cpp
 * @brief Multi-threaded HTML/CSS renderer using litehtml + FreeType
 * @lines ~1380
 *
 * CRITICAL: This system uses TWO threads:
 * - **Main thread**: OpenGL operations, texture uploads, input handling
 * - **Render thread**: litehtml rendering, FreeType text rasterization
 *
 * Key functions (Main Thread):
 * - Initialize() - Setup GL, start render thread (line 858)
 * - LoadHTML() / UpdateHTML() - Queue HTML for render thread (line 938, 955)
 * - Render() - Upload texture to GPU (line 961)
 * - UpdateTextureFromPixelBuffer() - Main→GPU texture transfer (line 1004)
 * - HandleClickEvent() - Process UI clicks (line 1190)
 * - Shutdown() - Stop render thread gracefully (line 1067)
 *
 * Key functions (Render Thread):
 * - RenderThreadLoop() - Background rendering loop (line 1112)
 * - SoftwareRenderer class - litehtml document_container impl (line 16-857)
 *
 * Thread Safety:
 * - m_mutex protects: m_html, m_pendingHTML, m_renderRequested
 * - m_bufferMutex protects: m_backBuffer ↔ m_frontBuffer swap
 * - NEVER call OpenGL from render thread
 * - NEVER call FreeType from main thread
 *
 * Performance: Render thread runs async (~5-15ms), main thread only pays texture upload (~1-2ms)
 */

#include <glad/glad.h>
#include "systems/HTMLRendererMT.h"
#include "systems/ReactiveUI.h"
#include "controllers/Game.h"
#include "util/Logger.h"

#include <cstring>
#include <algorithm>
#include <chrono>

// stb_image for decoding PNGs from data URIs (implementation in Mesh.cpp)
#include "stb_image.h"

#include <yaml-cpp/binary.h> // For base64 decoding

// Software rendering document container for litehtml
class HTMLRendererMT::SoftwareRenderer : public litehtml::document_container
{
public:
    struct Glyph
    {
        int width, height;
        int bearingX, bearingY;
        int advance;
        std::vector<uint8_t> bitmap; // Grayscale bitmap
    };

    struct FontInfo
    {
        std::string name;
        int size;
        int weight;
        int ascent;
        std::map<char, Glyph> glyphs;
    };

    struct ImageData
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<uint8_t> pixels; // RGBA pixels
    };

    SoftwareRenderer(FrameBuffer *buffer)
        : m_buffer(buffer)
    {
        // Initialize FreeType
        if (FT_Init_FreeType(&m_ft_library))
        {
            LOG_ERROR("[SoftwareRenderer] Failed to initialize FreeType");
        }
        else
        {
            LOG_INFO("[SoftwareRenderer] FreeType initialized");
        }
    }

    ~SoftwareRenderer()
    {
        // Clean up FreeType faces
        for (auto &facePair : m_ft_faces)
        {
            FT_Done_Face(facePair.second);
        }
        if (m_ft_library)
        {
            FT_Done_FreeType(m_ft_library);
        }
    }

    void RenderHTML(const std::string &html)
    {
        try
        {
            LOG_TRACE_L2("[SoftwareRenderer] Creating document from HTML ({} bytes)", html.size());

            // Create document
            m_document = litehtml::document::createFromString(html.c_str(), this);
            if (m_document)
            {
                LOG_TRACE_L2("[SoftwareRenderer] Rendering document");
                m_document->render(m_buffer->width);
                LOG_TRACE_L2("[SoftwareRenderer] Drawing to buffer");
                RenderToBuffer();
                LOG_TRACE_L2("[SoftwareRenderer] Render complete");
            }
            else
            {
                LOG_ERROR("[SoftwareRenderer] Failed to create document");
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("[SoftwareRenderer] Exception: {}", e.what());
        }
    }

    void RenderToBuffer()
    {
        // Clear buffer
        std::memset(m_buffer->pixels.data(), 0, m_buffer->pixels.size());

        // Draw document
        litehtml::position clip(0, 0, m_buffer->width, m_buffer->height);
        m_document->draw((litehtml::uint_ptr)0, 0, 0, &clip);
    }

    // Minimal litehtml::document_container implementation
    litehtml::uint_ptr create_font(const litehtml::font_description &descr,
                                   const litehtml::document *doc,
                                   litehtml::font_metrics *fm) override
    {
        // Check if we already have this font cached (by name + size)
        std::string fontKey = descr.family + "_" + std::to_string(descr.size);
        auto cachedFont = m_fontCache.find(fontKey);
        if (cachedFont != m_fontCache.end())
        {
            litehtml::uint_ptr font_id = m_next_font_id++;
            m_fonts[font_id] = cachedFont->second;

            // Set font metrics
            if (fm)
            {
                fm->height = descr.size;
                fm->ascent = cachedFont->second.ascent;
                fm->descent = -(descr.size - cachedFont->second.ascent);
                fm->x_height = descr.size / 2;
                fm->draw_spaces = true;
            }

            return font_id;
        }

        litehtml::uint_ptr font_id = m_next_font_id++;
        FontInfo fontInfo;
        fontInfo.name = descr.family;
        fontInfo.size = descr.size;
        fontInfo.weight = descr.weight;

        // Load font with FreeType - use Menlo for monospace
        std::string fontPath = "/System/Library/Fonts/Menlo.ttc"; // macOS monospace default

        FT_Face face = nullptr;
        auto it = m_ft_faces.find(fontPath);
        if (it != m_ft_faces.end())
        {
            face = it->second;
        }
        else if (!FT_New_Face(m_ft_library, fontPath.c_str(), 0, &face))
        {
            m_ft_faces[fontPath] = face;
            LOG_INFO("[SoftwareRenderer] Loaded font face: {}", fontPath);
        }

        if (face)
        {
            auto glyphLoadStart = std::chrono::high_resolution_clock::now();

            FT_Set_Pixel_Sizes(face, 0, descr.size);
            fontInfo.ascent = (face->size->metrics.ascender >> 6);

            // Load ASCII characters
            for (unsigned char c = 32; c < 127; c++)
            {
                // Load with NO_HINTING to avoid glyph corruption from aggressive hinting
                if (FT_Load_Char(face, c, FT_LOAD_RENDER | FT_LOAD_NO_HINTING))
                {
                    continue;
                }

                // Verify we got a grayscale bitmap
                if (face->glyph->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
                {
                    LOG_WARNING("[SoftwareRenderer] Unexpected pixel mode {} for char '{}', skipping",
                                face->glyph->bitmap.pixel_mode, (char)c);
                    continue;
                }

                Glyph glyph;
                glyph.width = face->glyph->bitmap.width;
                glyph.height = face->glyph->bitmap.rows;
                glyph.bearingX = face->glyph->bitmap_left;
                glyph.bearingY = face->glyph->bitmap_top;
                glyph.advance = face->glyph->advance.x;

                // Copy bitmap data (row-by-row to handle pitch/padding)
                size_t bitmapSize = glyph.width * glyph.height;
                glyph.bitmap.resize(bitmapSize);
                if (bitmapSize > 0)
                {
                    // FreeType bitmaps may have padding (pitch != width)
                    // pitch can be negative for bottom-up bitmaps, but we always render top-down
                    // Copy row-by-row to avoid corrupted glyph data
                    int pitch = face->glyph->bitmap.pitch;
                    bool flipVertically = (pitch < 0);
                    pitch = abs(pitch);

                    for (unsigned int row = 0; row < glyph.height; row++)
                    {
                        unsigned int srcRow = flipVertically ? (glyph.height - 1 - row) : row;
                        std::memcpy(
                            glyph.bitmap.data() + row * glyph.width,
                            face->glyph->bitmap.buffer + srcRow * pitch,
                            glyph.width);
                    }
                }

                fontInfo.glyphs[c] = glyph;
            }

            auto glyphLoadEnd = std::chrono::high_resolution_clock::now();
            auto glyphLoadDuration = std::chrono::duration_cast<std::chrono::milliseconds>(glyphLoadEnd - glyphLoadStart).count();
            LOG_TRACE_L3("[SoftwareRenderer] Loaded {} glyphs at size {} in {}ms", fontInfo.glyphs.size(), descr.size, glyphLoadDuration);

            // Set font metrics
            if (fm)
            {
                fm->height = descr.size;
                fm->ascent = fontInfo.ascent;
                fm->descent = -(face->size->metrics.descender >> 6);
                fm->x_height = descr.size / 2;
                fm->draw_spaces = true;
            }
        }
        else
        {
            LOG_ERROR("[SoftwareRenderer] Failed to load font");
            fontInfo.ascent = descr.size * 0.8f;
        }

        m_fonts[font_id] = fontInfo;
        m_fontCache[fontKey] = fontInfo; // Cache for reuse
        return font_id;
    }

    void delete_font(litehtml::uint_ptr hFont) override
    {
        m_fonts.erase(hFont);
    }

    litehtml::pixel_t text_width(const char *text, litehtml::uint_ptr hFont) override
    {
        auto it = m_fonts.find(hFont);
        if (it == m_fonts.end() || !text)
            return 0;

        const FontInfo &font = it->second;
        if (font.glyphs.empty())
            return 0;

        // Calculate actual text width using glyph advance values
        float width = 0;
        for (const char *c = text; *c; c++)
        {
            auto glyph_it = font.glyphs.find(*c);
            if (glyph_it != font.glyphs.end())
            {
                width += (glyph_it->second.advance >> 6);
            }
        }
        return width;
    }

    void draw_text(litehtml::uint_ptr hdc, const char *text, litehtml::uint_ptr hFont,
                   litehtml::web_color color, const litehtml::position &pos) override
    {
        if (!text || !*text)
            return;

        auto it = m_fonts.find(hFont);
        if (it == m_fonts.end())
            return;

        const FontInfo &font = it->second;
        if (font.glyphs.empty())
            return;

        // Calculate baseline: pos.y is top of text box (top-left origin)
        // Baseline = top of box + ascent distance (text sits below the top)
        float baseline_y = pos.y + font.ascent;
        float x = pos.x;

        for (const char *c = text; *c; c++)
        {
            auto glyph_it = font.glyphs.find(*c);
            if (glyph_it == font.glyphs.end())
                continue;

            const Glyph &glyph = glyph_it->second;

            // Calculate position
            int xpos = x + glyph.bearingX;
            int ypos = baseline_y - glyph.bearingY;

            // Draw glyph bitmap
            DrawGlyph(glyph, xpos, ypos, color);

            // Advance to next character
            x += (glyph.advance >> 6);
        }
    }

    void DrawGlyph(const Glyph &glyph, int x, int y, litehtml::web_color color)
    {
        if (glyph.bitmap.empty())
            return;

        // Render glyph bitmap to pixel buffer
        for (int py = 0; py < glyph.height; py++)
        {
            for (int px = 0; px < glyph.width; px++)
            {
                int screenX = x + px;
                int screenY = y + py;

                // Bounds check
                if (screenX < 0 || screenX >= (int)m_buffer->width ||
                    screenY < 0 || screenY >= (int)m_buffer->height)
                {
                    continue;
                }

                // Read bitmap directly - no flipping needed
                uint8_t alpha = glyph.bitmap[py * glyph.width + px];
                if (alpha == 0)
                    continue;

                // Get pixel in buffer
                uint8_t *pixel = &m_buffer->pixels[(screenY * m_buffer->width + screenX) * 4];

                // Alpha blend with text color
                float a = alpha / 255.0f;
                pixel[0] = (uint8_t)(color.red * a + pixel[0] * (1.0f - a));
                pixel[1] = (uint8_t)(color.green * a + pixel[1] * (1.0f - a));
                pixel[2] = (uint8_t)(color.blue * a + pixel[2] * (1.0f - a));
                pixel[3] = std::max(pixel[3], alpha);
            }
        }
    }

    litehtml::pixel_t pt_to_px(float pt) const override
    {
        return (pt * 96.0f) / 72.0f;
    }

    litehtml::pixel_t get_default_font_size() const override
    {
        return 16;
    }

    const char *get_default_font_name() const override
    {
        return "Arial";
    }

    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker &marker) override {}

    void load_image(const char *src, const char *baseurl, bool redraw_on_ready) override
    {
        if (!src)
            return;

        std::string url(src);

        // Check if already loaded
        if (m_imageCache.find(url) != m_imageCache.end())
        {
            return;
        }

        // Check if it's a data URI
        if (url.find("data:image/png;base64,") == 0)
        {
            // Extract base64 data
            std::string base64Data = url.substr(22); // Skip "data:image/png;base64,"

            // Decode base64
            std::vector<unsigned char> pngData = YAML::DecodeBase64(base64Data);

            if (pngData.empty())
            {
                LOG_ERROR("[SoftwareRenderer] Failed to decode base64 data URI");
                return;
            }

            // Decode PNG using stb_image
            // Temporarily disable vertical flip - PNG images are already top-down,
            // and the viewport PNG was pre-flipped in GetTextureAsDataURI()
            stbi_set_flip_vertically_on_load(false);

            int width, height, channels;
            unsigned char *pixels = stbi_load_from_memory(
                pngData.data(),
                pngData.size(),
                &width,
                &height,
                &channels,
                4 // Force RGBA
            );

            // Restore flip setting for other texture loads (e.g., 3D textures)
            stbi_set_flip_vertically_on_load(true);

            if (!pixels)
            {
                LOG_ERROR("[SoftwareRenderer] Failed to decode PNG from data URI");
                return;
            }

            // Store in cache
            ImageData &imgData = m_imageCache[url];
            imgData.width = width;
            imgData.height = height;
            imgData.channels = 4;
            imgData.pixels.assign(pixels, pixels + (width * height * 4));

            stbi_image_free(pixels);

            static bool logged = false;
            if (!logged)
            {
                LOG_INFO("[SoftwareRenderer] Loaded image from data URI: {}x{}", width, height);
                logged = true;
            }
        }
    }

    void get_image_size(const char *src, const char *baseurl, litehtml::size &sz) override
    {
        if (!src)
        {
            sz.width = 0;
            sz.height = 0;
            return;
        }

        std::string url(src);
        auto it = m_imageCache.find(url);
        if (it != m_imageCache.end())
        {
            sz.width = it->second.width;
            sz.height = it->second.height;
        }
        else
        {
            sz.width = 0;
            sz.height = 0;
        }
    }

    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
                    const std::string &url, const std::string &base_url) override
    {
        auto it = m_imageCache.find(url);
        if (it == m_imageCache.end())
        {
            return; // Image not loaded
        }

        const ImageData &img = it->second;
        if (img.pixels.empty())
        {
            return;
        }

        // Get destination rectangle
        int dst_x = layer.border_box.x;
        int dst_y = layer.border_box.y;
        int dst_w = layer.border_box.width;
        int dst_h = layer.border_box.height;

        static bool logged = false;
        if (!logged)
        {
            LOG_INFO("[SoftwareRenderer] draw_image: src={}x{}, dst=({},{}) {}x{}",
                     img.width, img.height, dst_x, dst_y, dst_w, dst_h);
            logged = true;
        }

        // Nearest-neighbor scaling
        float x_ratio = (float)img.width / (float)dst_w;
        float y_ratio = (float)img.height / (float)dst_h;

        for (int y = 0; y < dst_h; y++)
        {
            for (int x = 0; x < dst_w; x++)
            {
                // Map destination pixel to source pixel
                int src_x = (int)(x * x_ratio);
                int src_y = (int)(y * y_ratio);

                // Clamp to source bounds
                src_x = std::min(src_x, img.width - 1);
                src_y = std::min(src_y, img.height - 1);

                int src_idx = (src_y * img.width + src_x) * 4;
                uint8_t r = img.pixels[src_idx + 0];
                uint8_t g = img.pixels[src_idx + 1];
                uint8_t b = img.pixels[src_idx + 2];
                uint8_t a = img.pixels[src_idx + 3];

                // Draw pixel with alpha blending
                if (a > 0)
                {
                    SetPixel(dst_x + x, dst_y + y, r, g, b, a);
                }
            }
        }
    }

    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
                         const litehtml::web_color &color) override
    {
        DrawRect(layer.border_box.x, layer.border_box.y,
                 layer.border_box.width, layer.border_box.height, color);
    }

    void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
                              const litehtml::background_layer::linear_gradient &gradient) override
    {
        if (!gradient.color_points.empty())
        {
            DrawRect(layer.border_box.x, layer.border_box.y,
                     layer.border_box.width, layer.border_box.height,
                     gradient.color_points[0].color);
        }
    }

    void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
                              const litehtml::background_layer::radial_gradient &gradient) override
    {
        if (!gradient.color_points.empty())
        {
            DrawRect(layer.border_box.x, layer.border_box.y,
                     layer.border_box.width, layer.border_box.height,
                     gradient.color_points[0].color);
        }
    }

    void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
                             const litehtml::background_layer::conic_gradient &gradient) override
    {
        if (!gradient.color_points.empty())
        {
            DrawRect(layer.border_box.x, layer.border_box.y,
                     layer.border_box.width, layer.border_box.height,
                     gradient.color_points[0].color);
        }
    }

    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders &borders,
                      const litehtml::position &draw_pos, bool root) override
    {
        // Draw all four borders
        if (borders.top.width > 0)
            DrawBorder(draw_pos, borders.top, 0);
        if (borders.right.width > 0)
            DrawBorder(draw_pos, borders.right, 1);
        if (borders.bottom.width > 0)
            DrawBorder(draw_pos, borders.bottom, 2);
        if (borders.left.width > 0)
            DrawBorder(draw_pos, borders.left, 3);
    }

    void set_caption(const char *caption) override {}
    void set_base_url(const char *base_url) override {}
    void link(const std::shared_ptr<litehtml::document> &doc, const litehtml::element::ptr &el) override {}
    void on_anchor_click(const char *url, const litehtml::element::ptr &el) override {}
    void on_mouse_event(const litehtml::element::ptr &el, litehtml::mouse_event event) override {}
    void set_cursor(const char *cursor) override {}
    void transform_text(litehtml::string &text, litehtml::text_transform tt) override {}
    void import_css(litehtml::string &text, const litehtml::string &url, litehtml::string &baseurl) override {}
    void set_clip(const litehtml::position &pos, const litehtml::border_radiuses &bdr_radius) override {}
    void del_clip() override {}

    void get_viewport(litehtml::position &viewport) const override
    {
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = m_buffer->width;
        viewport.height = m_buffer->height;
    }

    std::shared_ptr<litehtml::element> create_element(const char *tag_name,
                                                      const litehtml::string_map &attributes,
                                                      const std::shared_ptr<litehtml::document> &doc) override
    {
        return nullptr;
    }

    void get_media_features(litehtml::media_features &media) const override
    {
        media.type = litehtml::media_type_screen;
        media.width = m_buffer->width;
        media.height = m_buffer->height;
        media.device_width = m_buffer->width;
        media.device_height = m_buffer->height;
        media.color = 8;
        media.monochrome = 0;
        media.color_index = 256;
        media.resolution = 96;
    }

    void get_language(litehtml::string &language, litehtml::string &culture) const override
    {
        language = "en";
        culture = "";
    }

    /**
     * @brief Extract interactive elements from rendered document
     * @param eventHandlers Map from TemplateParser (elementId → {eventType → handler})
     * @note Call after document->render() to capture element positions
     */
    void ExtractInteractiveElements(const std::map<std::string, std::map<std::string, std::string>> &eventHandlers)
    {
        m_interactiveElements.clear();

        if (!m_document)
        {
            LOG_TRACE_L2("[SoftwareRenderer] No document to extract elements from");
            return;
        }

        auto root = m_document->root();
        if (!root)
        {
            LOG_TRACE_L2("[SoftwareRenderer] No document root");
            return;
        }

        // Traverse DOM tree to find elements with data-event-id
        TraverseElementForEvents(root, eventHandlers, 0);

        // Sort by z-index (ascending) so we can iterate reverse for hit-testing
        std::sort(m_interactiveElements.begin(), m_interactiveElements.end(),
                  [](const HTMLRendererMT::InteractiveElement &a, const HTMLRendererMT::InteractiveElement &b)
                  {
                      return a.zIndex < b.zIndex;
                  });

        LOG_TRACE_L2("[SoftwareRenderer] Extracted {} interactive elements", m_interactiveElements.size());
    }

    /**
     * @brief Get extracted interactive elements
     * @return Vector of interactive elements with bounds and handlers
     */
    const std::vector<HTMLRendererMT::InteractiveElement> &GetInteractiveElements() const
    {
        return m_interactiveElements;
    }

private:
    /**
     * @brief Recursively traverse litehtml element tree
     * @param elem Current element
     * @param eventHandlers Event handler map from parser
     * @param parentZIndex Parent's z-index (inherited if element has no z-index)
     */
    void TraverseElementForEvents(litehtml::element::ptr elem,
                                  const std::map<std::string, std::map<std::string, std::string>> &eventHandlers,
                                  int parentZIndex)
    {
        if (!elem)
            return;

        // Check for data-event-id attribute
        const char *eventId = elem->get_attr("data-event-id");
        if (eventId && eventId[0] != '\0')
        {
            std::string elemIdStr(eventId);
            auto it = eventHandlers.find(elemIdStr);
            if (it != eventHandlers.end())
            {
                // Get element bounds (content box only)
                auto pos = elem->get_placement();

                // Get padding to calculate full clickable area
                auto padding = elem->css().get_padding();

                // Calculate full box including padding (makes buttons clickable in padding area)
                int paddingLeft = (int)padding.left.val();
                int paddingRight = (int)padding.right.val();
                int paddingTop = (int)padding.top.val();
                int paddingBottom = (int)padding.bottom.val();

                // Extract z-index from litehtml CSS
                // Note: get_z_index() returns 0 for "auto" (default), which means DOM order
                int elemZIndex = elem->css().get_z_index();

                // Create interactive element with full box (content + padding)
                HTMLRendererMT::InteractiveElement ie;
                ie.id = elemIdStr;
                ie.x = pos.x - paddingLeft;
                ie.y = pos.y - paddingTop;
                ie.width = pos.width + paddingLeft + paddingRight;
                ie.height = pos.height + paddingTop + paddingBottom;
                ie.handlers = it->second; // Copy event handler map
                ie.zIndex = elemZIndex;

                m_interactiveElements.push_back(ie);

                LOG_TRACE_L3("[SoftwareRenderer] Interactive element: id={}, bounds=({},{},{}x{}), z={}, handlers={}",
                             ie.id, ie.x, ie.y, ie.width, ie.height, ie.zIndex, ie.handlers.size());
            }
        }

        // Recurse into children (use DOM order for now)
        for (auto &child : elem->children())
        {
            TraverseElementForEvents(child, eventHandlers, 0);
        }
    }

private:
    void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        // Bounds check
        if (x < 0 || y < 0 || x >= (int)m_buffer->width || y >= (int)m_buffer->height)
            return;

        uint8_t *pixel = &m_buffer->pixels[(y * m_buffer->width + x) * 4];

        if (a == 255)
        {
            pixel[0] = r;
            pixel[1] = g;
            pixel[2] = b;
            pixel[3] = a;
        }
        else
        {
            // Alpha blend
            float alpha = a / 255.0f;
            pixel[0] = (uint8_t)(r * alpha + pixel[0] * (1.0f - alpha));
            pixel[1] = (uint8_t)(g * alpha + pixel[1] * (1.0f - alpha));
            pixel[2] = (uint8_t)(b * alpha + pixel[2] * (1.0f - alpha));
            pixel[3] = std::max(pixel[3], a);
        }
    }

    void DrawRect(int x, int y, int width, int height, litehtml::web_color color)
    {
        // Clamp to buffer bounds
        if (x < 0)
        {
            width += x;
            x = 0;
        }
        if (y < 0)
        {
            height += y;
            y = 0;
        }
        if (x >= (int)m_buffer->width || y >= (int)m_buffer->height)
            return;
        if (x + width > (int)m_buffer->width)
            width = m_buffer->width - x;
        if (y + height > (int)m_buffer->height)
            height = m_buffer->height - y;
        if (width <= 0 || height <= 0)
            return;

        uint8_t r = color.red;
        uint8_t g = color.green;
        uint8_t b = color.blue;
        uint8_t a = color.alpha;

        // Simple alpha blending
        for (int py = y; py < y + height; py++)
        {
            for (int px = x; px < x + width; px++)
            {
                uint8_t *pixel = &m_buffer->pixels[(py * m_buffer->width + px) * 4];

                if (a == 255)
                {
                    pixel[0] = r;
                    pixel[1] = g;
                    pixel[2] = b;
                    pixel[3] = a;
                }
                else
                {
                    // Alpha blend
                    float alpha = a / 255.0f;
                    pixel[0] = (uint8_t)(r * alpha + pixel[0] * (1.0f - alpha));
                    pixel[1] = (uint8_t)(g * alpha + pixel[1] * (1.0f - alpha));
                    pixel[2] = (uint8_t)(b * alpha + pixel[2] * (1.0f - alpha));
                    pixel[3] = std::max(pixel[3], a);
                }
            }
        }
    }

    void DrawBorder(const litehtml::position &pos, const litehtml::border &border, int side)
    {
        if (border.width <= 0)
            return;

        int x, y, w, h;
        switch (side)
        {
        case 0: // top
            x = pos.x;
            y = pos.y;
            w = pos.width;
            h = border.width;
            break;
        case 1: // right
            x = pos.x + pos.width - border.width;
            y = pos.y;
            w = border.width;
            h = pos.height;
            break;
        case 2: // bottom
            x = pos.x;
            y = pos.y + pos.height - border.width;
            w = pos.width;
            h = border.width;
            break;
        case 3: // left
            x = pos.x;
            y = pos.y;
            w = border.width;
            h = pos.height;
            break;
        default:
            return;
        }

        DrawRect(x, y, w, h, border.color);
    }

    FrameBuffer *m_buffer;
    FT_Library m_ft_library = nullptr;
    std::map<std::string, FT_Face> m_ft_faces;
    std::map<litehtml::uint_ptr, FontInfo> m_fonts;
    std::map<std::string, FontInfo> m_fontCache; // Cache fonts by "family_size"
    litehtml::uint_ptr m_next_font_id = 1;

    // Image cache for data URIs and loaded images
    std::map<std::string, ImageData> m_imageCache;

    // Interactive elements extracted from DOM
    std::vector<HTMLRendererMT::InteractiveElement> m_interactiveElements;

    // IMPORTANT: m_document must be declared AFTER m_fonts/m_fontCache
    // because its destructor calls delete_font() which needs those maps
    litehtml::document::ptr m_document;
};

// ============================================================================
// HTMLRendererMT Implementation
// ============================================================================

void HTMLRendererMT::Initialize(GLFWwindow *window, int width, int height)
{
    m_window = window;
    m_width = width;
    m_height = height;

    LOG_INFO("[HTMLRendererMT] Initializing multi-threaded HTML renderer");

    try
    {
        // Initialize buffers
        m_frontBuffer.width = width;
        m_frontBuffer.height = height;
        m_frontBuffer.frameNumber = 0;
        m_frontBuffer.pixels.resize(width * height * 4, 0);

        m_backBuffer.width = width;
        m_backBuffer.height = height;
        m_backBuffer.frameNumber = 0;
        m_backBuffer.pixels.resize(width * height * 4, 0);

        // Set up OpenGL resources
        SetupGL();

        m_projection = glm::ortho(0.0f, (float)m_width, (float)m_height, 0.0f, -1.0f, 1.0f);

        // Start render thread
        m_running = true;
        m_renderThread = std::make_unique<std::thread>(&HTMLRendererMT::RenderThreadLoop, this);

        LOG_INFO("[HTMLRendererMT] Initialization complete");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("[HTMLRendererMT] Initialization failed: {}", e.what());
        Shutdown();
        throw;
    }
}

void HTMLRendererMT::SetupGL()
{
    // Create composite shader
    m_compositeShader = new Shader(Game::GetInstance().conf.ResourcePath + "shaders/Composite.shader");

    // Create texture for uploading pixels
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create quad for rendering texture (screen space coordinates)
    float quadVertices[] = {
        // positions (screen space)        // texCoords
        0.0f, 0.0f, 0.0f, 0.0f,                      // top-left
        0.0f, (float)m_height, 0.0f, 1.0f,           // bottom-left
        (float)m_width, (float)m_height, 1.0f, 1.0f, // bottom-right

        0.0f, 0.0f, 0.0f, 0.0f,                      // top-left
        (float)m_width, (float)m_height, 1.0f, 1.0f, // bottom-right
        (float)m_width, 0.0f, 1.0f, 0.0f             // top-right
    };

    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void HTMLRendererMT::LoadHTML(const std::string &html)
{
    LoadHTMLAndGetRevision(html);
}

std::uint64_t HTMLRendererMT::LoadHTMLAndGetRevision(const std::string &html)
{
    auto start = std::chrono::high_resolution_clock::now();
    LOG_TRACE_L2("[HTMLRendererMT] LoadHTML called ({} bytes)", html.size());

    std::uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingHTML = html;
        m_hasNewHTML = true;
        revision = ++m_nextHTMLRevision;
        m_pendingHTMLRevision = revision;
    }
    m_cv.notify_one();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    LOG_TRACE_L2("[HTMLRendererMT] LoadHTML revision {} signaled render thread ({}ms)", revision, duration);
    return revision;
}

bool HTMLRendererMT::WaitForHTMLRevision(std::uint64_t revision, std::chrono::milliseconds timeout) const
{
    if (revision == 0)
    {
        return false;
    }
    std::unique_lock<std::mutex> lock(m_bufferMutex);
    const bool completed = m_frameCv.wait_for(lock, timeout, [this, revision]
    {
        return m_frontHTMLRevision >= revision || !m_running.load();
    });
    return completed && m_frontHTMLRevision == revision;
}

void HTMLRendererMT::UpdateHTML(const std::string &html)
{
    // Same as LoadHTML
    LoadHTML(html);
}

void HTMLRendererMT::Render()
{
    if (!m_compositeShader)
        return;

    // Check if new frame is ready
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        if (m_frontBuffer.frameNumber != m_lastFrameNumber)
        {
            LOG_TRACE_L2("[HTMLRendererMT] Uploading new frame {} (was {})", m_frontBuffer.frameNumber, m_lastFrameNumber);
            UpdateTextureFromPixelBuffer();
            m_lastFrameNumber = m_frontBuffer.frameNumber;
        }
    }

    // Set up OpenGL state
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Use composite shader
    m_compositeShader->Use();
    m_compositeShader->SetMat4("projection", m_projection);
    m_compositeShader->SetInt("screenTexture", 0);

    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    // Draw quad with texture
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Cleanup
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Restore OpenGL state
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void HTMLRendererMT::UpdateTextureFromPixelBuffer()
{
    // Coordinate system (top-left origin throughout):
    // 1. litehtml renders with pos.y=0 at top
    // 2. Pixel buffer stores row 0 = screen top
    // 3. Texture upload: direct copy (no flipping)
    // 4. Quad maps screen (0,0) → texture UV (0,0)
    // 5. Projection transforms to NDC with Y-down
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_frontBuffer.width, m_frontBuffer.height,
                    GL_RGBA, GL_UNSIGNED_BYTE, m_frontBuffer.pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void HTMLRendererMT::Resize(int width, int height)
{
    LOG_INFO("[HTMLRendererMT] Resize to {}x{}", width, height);

    m_width = width;
    m_height = height;

    // Update projection matrix
    m_projection = glm::ortho(0.0f, (float)m_width, (float)m_height, 0.0f, -1.0f, 1.0f);

    // Notify render thread
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_newWidth = width;
        m_newHeight = height;
        m_needsResize = true;
    }
    m_cv.notify_one();

    // Recreate texture
    if (m_texture)
    {
        glDeleteTextures(1, &m_texture);
    }

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Update quad vertices for new size
    float quadVertices[] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, (float)m_height, 0.0f, 1.0f,
        (float)m_width, (float)m_height, 1.0f, 1.0f,

        0.0f, 0.0f, 0.0f, 0.0f,
        (float)m_width, (float)m_height, 1.0f, 1.0f,
        (float)m_width, 0.0f, 1.0f, 0.0f};

    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quadVertices), quadVertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void HTMLRendererMT::Shutdown()
{
    if (m_isShutdown)
        return; // Already shut down
    m_isShutdown = true;

    LOG_INFO("[HTMLRendererMT] Shutting down");

    // Stop render thread
    if (m_renderThread)
    {
        m_running = false;
        m_cv.notify_one();
        m_frameCv.notify_all();
        if (m_renderThread->joinable())
        {
            m_renderThread->join();
        }
        m_renderThread.reset();
    }

    // Clean up OpenGL resources
    if (m_texture)
    {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if (m_quadVAO)
    {
        glDeleteVertexArrays(1, &m_quadVAO);
        m_quadVAO = 0;
    }
    if (m_quadVBO)
    {
        glDeleteBuffers(1, &m_quadVBO);
        m_quadVBO = 0;
    }
    if (m_compositeShader)
    {
        delete m_compositeShader;
        m_compositeShader = nullptr;
    }

    LOG_INFO("[HTMLRendererMT] Shutdown complete");
}

void HTMLRendererMT::RenderThreadLoop()
{
    LOG_INFO("[RenderThread] Starting");

    // Create renderer
    SoftwareRenderer renderer(&m_backBuffer);

    std::string currentHTML;
    std::uint64_t currentHTMLRevision = 0;
    bool needsRender = false;

    while (m_running)
    {
        // Wait for work
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]
                      { return !m_running || m_hasNewHTML || m_needsResize; });

            if (!m_running)
                break;

            // Handle resize
            if (m_needsResize)
            {
                m_backBuffer.width = m_newWidth;
                m_backBuffer.height = m_newHeight;
                m_backBuffer.pixels.resize(m_newWidth * m_newHeight * 4, 0);
                m_needsResize = false;
                needsRender = !currentHTML.empty();
            }

            // Handle new HTML
            if (m_hasNewHTML)
            {
                currentHTML = m_pendingHTML;
                currentHTMLRevision = m_pendingHTMLRevision;
                m_hasNewHTML = false;
                needsRender = true;
            }
        }

        // Render if needed
        if (needsRender && !currentHTML.empty())
        {
            auto renderStart = std::chrono::high_resolution_clock::now();
            LOG_TRACE_L2("[RenderThread] Rendering HTML");

            renderer.RenderHTML(currentHTML);

            // Extract interactive elements after rendering (thread-safe copy of handlers)
            auto handlers = GetEventHandlers();
            renderer.ExtractInteractiveElements(handlers);
            m_backInteractiveElements = renderer.GetInteractiveElements();
            // LOG_INFO("[RenderThread] Extracted {} interactive elements", m_backInteractiveElements.size());

            auto renderEnd = std::chrono::high_resolution_clock::now();
            auto renderDuration = std::chrono::duration_cast<std::chrono::milliseconds>(renderEnd - renderStart).count();

            // Swap buffers (pixel buffer + interactive elements)
            uint64_t newFrameNumber;
            {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                std::swap(m_frontBuffer, m_backBuffer);
                // Use monotonic counter to ensure frame numbers always increase
                m_nextFrameNumber++;
                m_frontBuffer.frameNumber = m_nextFrameNumber;
                m_frontHTMLRevision = currentHTMLRevision;
                newFrameNumber = m_frontBuffer.frameNumber;
            }
            m_frameCv.notify_all();
            {
                std::lock_guard<std::mutex> lock(m_interactiveElementsMutex);
                std::swap(m_frontInteractiveElements, m_backInteractiveElements);
            }

            needsRender = false;
            auto totalEnd = std::chrono::high_resolution_clock::now();
            auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - renderStart).count();
            LOG_TRACE_L2("[RenderThread] Render complete, frame {} (render: {}ms, total: {}ms)", newFrameNumber, renderDuration, totalDuration);
        }
    }

    LOG_INFO("[RenderThread] Exiting");
}

glm::vec2 HTMLRendererMT::WindowToFramebuffer(float windowX, float windowY) const
{
    int windowWidth, windowHeight;
    glfwGetWindowSize(m_window, &windowWidth, &windowHeight);

    float scaleX = (float)m_width / (float)windowWidth;
    float scaleY = (float)m_height / (float)windowHeight;

    return glm::vec2(windowX * scaleX, windowY * scaleY);
}

bool HTMLRendererMT::HandleClickEvent(float x, float y, int button)
{
    // Convert window coordinates to framebuffer coordinates
    glm::vec2 fb = WindowToFramebuffer(x, y);
    x = fb.x;
    y = fb.y;

    LOG_DEBUG("[HTMLRendererMT] HandleClickEvent: fb coords ({}, {})", x, y);

    // Copy interactive elements (thread-safe)
    std::vector<InteractiveElement> elements;
    {
        std::lock_guard<std::mutex> lock(m_interactiveElementsMutex);
        elements = m_frontInteractiveElements;
    }

    LOG_DEBUG("[HTMLRendererMT] HandleClickEvent: {} interactive elements", elements.size());

    // Screen coordinates now match litehtml coordinates (no flip needed)

    // Hit-test in reverse order (highest z-index first)
    for (auto it = elements.rbegin(); it != elements.rend(); ++it)
    {
        const auto &elem = *it;

        // Point-in-rectangle test
        if (x >= elem.x && x < elem.x + elem.width &&
            y >= elem.y && y < elem.y + elem.height)
        {
            LOG_DEBUG("[HTMLRendererMT] Hit element '{}' at ({},{}) size {}x{}",
                      elem.id, elem.x, elem.y, elem.width, elem.height);

            // Check if element has a click handler
            auto clickIt = elem.handlers.find("click");
            if (clickIt != elem.handlers.end())
            {
                LOG_DEBUG("[HTMLRendererMT] Dispatching click to handler: {}", clickIt->second);

                // Dispatch to ReactiveUI
                // Note: Pass framebuffer coordinates directly - they match the hit-test coordinates
                ReactiveUI &ui = ReactiveUI::GetInstance();
                ReactiveUI::EventData eventData;
                eventData.x = x;
                eventData.y = y;
                eventData.button = button;
                eventData.elemId = elem.id;
                eventData.eventType = "click";

                ui.DispatchEvent("click", clickIt->second, eventData);
                return true; // Event handled
            }
            else
            {
                LOG_DEBUG("[HTMLRendererMT] Element '{}' has no click handler, continuing search", elem.id);
            }
        }
    }

    LOG_DEBUG("[HTMLRendererMT] HandleClickEvent: no element found, returning false");
    return false; // Event not handled
}

bool HTMLRendererMT::HandleMouseButtonEvent(float x, float y, int button, int action)
{
    // Convert window coordinates to framebuffer coordinates
    glm::vec2 fb = WindowToFramebuffer(x, y);
    x = fb.x;
    y = fb.y;

    // Copy interactive elements (thread-safe)
    std::vector<InteractiveElement> elements;
    {
        std::lock_guard<std::mutex> lock(m_interactiveElementsMutex);
        elements = m_frontInteractiveElements;
    }

    // Determine event type based on action
    std::string eventType;
    if (action == GLFW_PRESS)
    {
        eventType = "mousedown";
    }
    else if (action == GLFW_RELEASE)
    {
        eventType = "mouseup";
    }
    else
    {
        return false; // Unknown action
    }

    auto dispatchToElement = [&](const InteractiveElement &elem) -> bool
    {
        auto handlerIt = elem.handlers.find(eventType);
        if (handlerIt == elem.handlers.end())
        {
            return false;
        }

        ReactiveUI &ui = ReactiveUI::GetInstance();
        ReactiveUI::EventData eventData;
        eventData.x = x;
        eventData.y = y;
        eventData.button = button;
        eventData.elemId = elem.id;
        eventData.eventType = eventType;

        ui.DispatchEvent(eventType, handlerIt->second, eventData);
        return true;
    };

    bool handled = false;

    // Primary dispatch target: element under cursor at time of event.
    for (auto it = elements.rbegin(); it != elements.rend(); ++it)
    {
        const auto &elem = *it;
        if (x >= elem.x && x < elem.x + elem.width &&
            y >= elem.y && y < elem.y + elem.height)
        {
            handled = dispatchToElement(elem);
            if (handled && action == GLFW_PRESS)
            {
                // Capture this element for a matching mouseup even if the cursor leaves the element.
                m_mouseCaptureElement = elem.id;
                m_mouseCaptureButton = button;
            }
            break;
        }
    }

    // Fallback for mouseup: if release happens outside the element, dispatch to the captured element.
    if (!handled && action == GLFW_RELEASE && !m_mouseCaptureElement.empty() && m_mouseCaptureButton == button)
    {
        for (const auto &elem : elements)
        {
            if (elem.id == m_mouseCaptureElement)
            {
                handled = dispatchToElement(elem);
                break;
            }
        }
    }

    // Always clear capture on release.
    if (action == GLFW_RELEASE)
    {
        m_mouseCaptureElement.clear();
        m_mouseCaptureButton = -1;
    }

    return handled;
}

bool HTMLRendererMT::TryGetInteractiveElementBounds(const std::string &elemId, int &x, int &y, int &width, int &height) const
{
    std::lock_guard<std::mutex> lock(m_interactiveElementsMutex);
    for (const auto &elem : m_frontInteractiveElements)
    {
        if (elem.id == elemId)
        {
            x = elem.x;
            y = elem.y;
            width = elem.width;
            height = elem.height;
            return true;
        }
    }

    return false;
}

bool HTMLRendererMT::TryFindInteractiveElementBoundsByHandler(const std::string &eventType,
                                                              const std::string &handlerExpr,
                                                              int &x,
                                                              int &y,
                                                              int &width,
                                                              int &height) const
{
    std::lock_guard<std::mutex> lock(m_interactiveElementsMutex);
    for (const auto &elem : m_frontInteractiveElements)
    {
        auto it = elem.handlers.find(eventType);
        if (it != elem.handlers.end() && it->second == handlerExpr)
        {
            x = elem.x;
            y = elem.y;
            width = elem.width;
            height = elem.height;
            return true;
        }
    }

    return false;
}

bool HTMLRendererMT::HasCompletedFirstRender() const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    return m_frontBuffer.frameNumber > 0;
}

void HTMLRendererMT::UpdateHoverState(float x, float y)
{
    // Convert window coordinates to framebuffer coordinates
    glm::vec2 fb = WindowToFramebuffer(x, y);
    x = fb.x;
    y = fb.y;

    // Copy interactive elements (thread-safe)
    std::vector<InteractiveElement> elements;
    {
        std::lock_guard<std::mutex> lock(m_interactiveElementsMutex);
        elements = m_frontInteractiveElements;
    }

    // Screen coordinates now match litehtml coordinates (no flip needed)

    // Hit-test in reverse order (highest z-index first)
    std::string newHoveredElement;
    for (auto it = elements.rbegin(); it != elements.rend(); ++it)
    {
        const auto &elem = *it;

        // Point-in-rectangle test
        if (x >= elem.x && x < elem.x + elem.width &&
            y >= elem.y && y < elem.y + elem.height)
        {
            newHoveredElement = elem.id;
            break; // Found topmost element
        }
    }

    // Check if hover state changed
    if (newHoveredElement != m_lastHoveredElement)
    {
        ReactiveUI &ui = ReactiveUI::GetInstance();

        // Dispatch mouseout/mouseleave to old element
        if (!m_lastHoveredElement.empty())
        {
            // Find old element
            for (const auto &elem : elements)
            {
                if (elem.id == m_lastHoveredElement)
                {
                    ReactiveUI::EventData eventData;
                    eventData.x = x;
                    eventData.y = y;
                    eventData.button = -1; // No button for hover events
                    eventData.elemId = elem.id;

                    // Dispatch mouseout event
                    auto mouseoutIt = elem.handlers.find("mouseout");
                    if (mouseoutIt != elem.handlers.end())
                    {
                        eventData.eventType = "mouseout";
                        ui.DispatchEvent("mouseout", mouseoutIt->second, eventData);
                    }

                    // Dispatch mouseleave event (alias for mouseout, for web compat)
                    auto mouseleaveIt = elem.handlers.find("mouseleave");
                    if (mouseleaveIt != elem.handlers.end())
                    {
                        eventData.eventType = "mouseleave";
                        ui.DispatchEvent("mouseleave", mouseleaveIt->second, eventData);
                    }
                    break;
                }
            }
        }

        // Dispatch mouseover/mouseenter to new element
        if (!newHoveredElement.empty())
        {
            // Find new element
            for (const auto &elem : elements)
            {
                if (elem.id == newHoveredElement)
                {
                    ReactiveUI::EventData eventData;
                    eventData.x = x;
                    eventData.y = y;
                    eventData.button = -1; // No button for hover events
                    eventData.elemId = elem.id;

                    // Dispatch mouseover event
                    auto mouseoverIt = elem.handlers.find("mouseover");
                    if (mouseoverIt != elem.handlers.end())
                    {
                        eventData.eventType = "mouseover";
                        ui.DispatchEvent("mouseover", mouseoverIt->second, eventData);
                    }

                    // Dispatch mouseenter event (alias for mouseover, for web compat)
                    auto mouseenterIt = elem.handlers.find("mouseenter");
                    if (mouseenterIt != elem.handlers.end())
                    {
                        eventData.eventType = "mouseenter";
                        ui.DispatchEvent("mouseenter", mouseenterIt->second, eventData);
                    }
                    break;
                }
            }
        }

        // TODO: CSS :hover pseudo-class support requires litehtml API integration
        // Need to call something like elem->set_pseudo_class(":hover", true) on hovered element
        // For now, hover events are dispatched to Lua handlers via mouseover/mouseout/mouseenter/mouseleave

        m_lastHoveredElement = newHoveredElement;
    }

    // Dispatch mousemove to currently hovered element (if handler exists).
    if (!newHoveredElement.empty())
    {
        for (const auto &elem : elements)
        {
            if (elem.id == newHoveredElement)
            {
                auto mousemoveIt = elem.handlers.find("mousemove");
                if (mousemoveIt != elem.handlers.end())
                {
                    ReactiveUI &ui = ReactiveUI::GetInstance();
                    ReactiveUI::EventData eventData;
                    eventData.x = x;
                    eventData.y = y;
                    eventData.button = -1;
                    eventData.elemId = elem.id;
                    eventData.eventType = "mousemove";
                    ui.DispatchEvent("mousemove", mousemoveIt->second, eventData);
                }
                break;
            }
        }
    }
}
