#include "backend.h"

#include <libdjvu/ddjvuapi.h>
#include <libdjvu/miniexp.h>
#include <stdexcept>
#include <vector>

class DJVU : public Backend {
private:
    ddjvu_context_t* ctx;
    ddjvu_document_t* doc;
    int page_count;

    void handle_messages() {
        const ddjvu_message_t* msg;
        while ((msg = ddjvu_message_peek(ctx))) {
            ddjvu_message_pop(ctx);
            if (msg->m_any.tag == DDJVU_ERROR) {
                throw std::runtime_error(msg->m_error.message);
            }
        }
    }

public:
    DJVU(const char* filename) {
        ctx = ddjvu_context_create("djvulibre_backend");
        if (!ctx) {
            throw std::runtime_error("Failed to create DJVU context");
        }

        doc = ddjvu_document_create_by_filename(ctx, filename, 0);
        if (!doc) {
            ddjvu_context_release(ctx);
            throw std::runtime_error("Failed to open DJVU document");
        }

        // Wait for document to load
        while (!ddjvu_document_decoding_done(doc)) {
            handle_messages();
        }

        page_count = ddjvu_document_get_pagenum(doc);
        if (page_count <= 0) {
            ddjvu_document_release(doc);
            ddjvu_context_release(ctx);
            throw std::runtime_error("Invalid page count");
        }
    }

    ~DJVU() {
        ddjvu_document_release(doc);
        ddjvu_context_release(ctx);
    }

    sf::Image render_page(int page_number, float zoom, bool subpixel) override {
        ddjvu_page_t* page = ddjvu_page_create_by_pageno(doc, page_number);
        if (!page) {
            throw std::runtime_error("Failed to create page");
        }
        while (!ddjvu_page_decoding_done(page)) {
            handle_messages();
        }

        unsigned int width = ddjvu_page_get_width(page) * zoom;
        unsigned int height = ddjvu_page_get_height(page) * zoom;
        ddjvu_rect_t rect { 0, 0, width, height };

        // Create RGB format
        ddjvu_format_t* format = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, nullptr);
        ddjvu_format_set_row_order(format, 1); // Top to bottom

        // Allocate pixel buffer & render
        std::vector<unsigned char> pixels(width * height * 3);
        unsigned char* buffer = pixels.data();
        if (!ddjvu_page_render(page, DDJVU_RENDER_COLOR, &rect, &rect,
                format, width * 3, (char*)buffer)) {
            ddjvu_format_release(format);
            ddjvu_page_release(page);
            throw std::runtime_error("Page rendering failed");
        }

        ddjvu_format_release(format);

        std::vector<unsigned char> out_pixels(width * height * 4, 255);
        for (int i = 0; i < width * height; ++i) {
            out_pixels[i * 4 + 0] = pixels[i * 3 + 0];
            out_pixels[i * 4 + 1] = pixels[i * 3 + 1];
            out_pixels[i * 4 + 2] = pixels[i * 3 + 2];
        }
        ddjvu_page_release(page);
        return sf::Image({ width, height }, out_pixels.data());
    }

    std::vector<TOCEntry> load_outline() override {
        std::vector<TOCEntry> entries;
        miniexp_t outline = ddjvu_document_get_outline(doc);
        if (outline == miniexp_nil) {
            return entries;
        }
        parse_outline_recursive(outline, entries, 0);
        return entries;
    }

    int count_pages() override {
        return page_count;
    }

private:
    void parse_outline_recursive(miniexp_t outline, std::vector<TOCEntry>& entries, int level) {
        if (!miniexp_consp(outline)) {
            return;
        }

        // Process current item
        miniexp_t item = miniexp_car(outline);
        if (miniexp_consp(item) && miniexp_length(item) >= 2) {
            // Extract title
            miniexp_t title_exp = miniexp_nth(0, item);
            std::string title;
            if (miniexp_stringp(title_exp)) {
                title = miniexp_to_str(title_exp);
            }

            // Extract page number from URL
            miniexp_t url_exp = miniexp_nth(1, item);
            int page = -1;
            if (miniexp_stringp(url_exp)) {
                std::string url = miniexp_to_str(url_exp);
                // URL format is typically "#page_number" (0-based)
                if (url.length() > 1 && url[0] == '#') {
                    try {
                        page = std::stoi(url.substr(1)) + 1; // Convert to 1-based
                    } catch (const std::exception&) {
                        page = -1;
                    }
                }
            }

            // Add entry if we have a valid title
            if (!title.empty()) {
                TOCEntry entry;
                entry.title = title;
                entry.page = page;
                entry.level = level;
                entries.push_back(entry);
            }

            // Process children (if any)
            if (miniexp_length(item) > 2) {
                for (int i = 2; i < miniexp_length(item); i++) {
                    parse_outline_recursive(miniexp_nth(i, item), entries, level + 1);
                }
            }
        }

        // Process next sibling
        miniexp_t next = miniexp_cdr(outline);
        if (next != miniexp_nil) {
            parse_outline_recursive(next, entries, level);
        }
    }
};
