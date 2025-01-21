#include "backend.h"

#include <SFML/Graphics.hpp>
#include <mupdf/fitz.h>
#include <mupdf/fitz/color.h>
#include <mupdf/fitz/context.h>
#include <mupdf/fitz/document.h>
#include <mupdf/fitz/geometry.h>
#include <mupdf/fitz/outline.h>
#include <stdexcept>

#pragma once

class PDF : public Backend {
private:
    fz_context* ctx;
    fz_document* doc;

    int page_count;

    fz_pixmap* get_pixmap(int page_number, float zoom) {
        fz_pixmap* pix;

        // render page to an RGB pixmap
        fz_try(ctx) {
            fz_matrix ctm;
            ctm = fz_scale(zoom, zoom);
            ctm = fz_pre_rotate(ctm, 0);

            pix = fz_new_pixmap_from_page_number(ctx, doc, page_number, ctm, fz_device_rgb(ctx), 0);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            fz_drop_document(ctx, doc);
            fz_drop_context(ctx);
            throw std::runtime_error("cannot render page\n");
        }

        return pix;
    }

public:
    ~PDF() {
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }

    std::pair<int, int> size(int page_number) override {
        fz_page* page = fz_load_page(ctx, doc, page_number);
        fz_rect bbox = fz_bound_page(ctx, page);
        fz_drop_page(ctx, page);
        return { bbox.x1, bbox.y1 };
    }

	sf::Image render_page(int page_number, float zoom) override {
        fz_pixmap* pix;

        // render page to an RGB pixmap
        fz_try(ctx) {
            fz_matrix ctm;
            ctm = fz_scale(zoom, zoom);
            ctm = fz_pre_rotate(ctm, 0);

            pix = fz_new_pixmap_from_page_number(ctx, doc, page_number, ctm, fz_device_rgb(ctx), 0);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            fz_drop_document(ctx, doc);
            fz_drop_context(ctx);
            throw std::runtime_error("cannot render page\n");
        }

        unsigned char* pixels = new unsigned char[pix->w * pix->h * 4];
        int i = 0;
        for (int y = 0; y < pix->h; ++y) {
            unsigned char* p = &pix->samples[y * pix->stride];

            for (int x = 0; x < pix->w; ++x) {
                pixels[i * 4 + 0] = p[0];
                pixels[i * 4 + 1] = p[1];
                pixels[i * 4 + 2] = p[2];
                pixels[i * 4 + 3] = 255;

                p += pix->n;
                i += 1;
            }
        }
        unsigned int w = pix->w;
        unsigned int h = pix->h;
		auto ret = sf::Image({ w, h }, pixels);
		delete[] pixels;
        fz_drop_pixmap(ctx, pix);

        return ret;
    }

    PDF(const char* filename) {
        ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
        if (!ctx) {
            throw std::runtime_error("cannot create mupdf context\n");
        }

        /* Register the default file types to handle. */
        fz_try(ctx)
            fz_register_document_handlers(ctx);
        fz_catch(ctx) {
            fz_report_error(ctx);
            fz_drop_context(ctx);
            throw std::runtime_error("cannot register document handlers\n");
        }

        /* Open the document. */
        fz_try(ctx)
            doc
            = fz_open_document(ctx, filename);
        fz_catch(ctx) {
            fz_report_error(ctx);
            fz_drop_context(ctx);
            throw std::runtime_error("cannot open document\n");
        }

        /* Count the number of pages. */
        fz_try(ctx)
            page_count
            = fz_count_pages(ctx, doc);
        fz_catch(ctx) {
            fz_report_error(ctx);
            fz_drop_document(ctx, doc);
            fz_drop_context(ctx);
            throw std::runtime_error("cannot count number of pages\n");
        }
    }

    std::vector<TOCEntry> load_outline() override {
        std::vector<TOCEntry> toc;
        auto load_outline = [&toc](auto& me, fz_context* ctx, fz_outline* outline, int level) -> void {
            while (outline) {
                toc.push_back({ outline->title, outline->uri, level });
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
