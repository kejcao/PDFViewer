#include "backend.h"

#include <SFML/Graphics.hpp>
#include <mupdf/fitz.h>
#include <mupdf/fitz/context.h>
#include <mupdf/fitz/document.h>
#include <stdexcept>

#pragma once

class PDF : public Backend {
private:
    fz_context* ctx;
    fz_document* doc;

    int page_count;

public:
    ~PDF() {
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }

    PDF(const char* filename) {
        ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
        if (!ctx) {
            throw std::runtime_error("cannot create mupdf context");
        }

        fz_try(ctx) {
            fz_register_document_handlers(ctx);
            doc = fz_open_document(ctx, filename);
            page_count = fz_count_pages(ctx, doc);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            throw std::runtime_error("something wrong when opening PDF");
        }
    }

    sf::Image render_page(int page_number) override {
        fz_pixmap* pix;

        fz_try(ctx)
            pix
            = fz_new_pixmap_from_page_number(
                ctx, doc, page_number, fz_scale(2, 2), fz_device_rgb(ctx), 0);
        fz_catch(ctx) {
            fz_report_error(ctx);
            throw std::runtime_error("failed to render page");
        }

        unsigned int w = pix->w;
        unsigned int h = pix->h;
        unsigned char* pixels = new unsigned char[w * h * 4];
        int i = 0;
        for (int y = 0; y < h; ++y) {
            unsigned char* p = &pix->samples[y * pix->stride];

            for (int x = 0; x < w; ++x) {
                pixels[i * 4 + 0] = p[0];
                pixels[i * 4 + 1] = p[1];
                pixels[i * 4 + 2] = p[2];
                pixels[i * 4 + 3] = 255;

                p += pix->n;
                i += 1;
            }
        }
        auto ret = sf::Image({ w, h }, pixels);
        delete[] pixels;
        fz_drop_pixmap(ctx, pix);

        return ret;
    }

    std::vector<TOCEntry> load_outline() override {
        std::vector<TOCEntry> toc;
        auto load_outline = [&](auto& me, fz_context* ctx, fz_outline* outline, int level) -> void {
            while (outline) {
                fz_link_dest dest = fz_resolve_link_dest(ctx, doc, outline->uri);
                toc.push_back({ outline->title, dest.loc.page, level });
                if (outline->down) {
                    me(me, ctx, outline->down, level + 1);
                }
                outline = outline->next;
            }
        };

        fz_outline* outline;
        fz_try(ctx)
            outline
            = fz_load_outline(ctx, doc);
        fz_catch(ctx) {
            throw std::runtime_error("cannot load table of contents\n");
        }
        load_outline(load_outline, ctx, outline, 0);
        fz_drop_outline(ctx, outline);

        return toc;
    }

    int count_pages() override {
        return page_count;
    }
};
