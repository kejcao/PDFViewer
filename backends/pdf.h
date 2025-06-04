#include "backend.h"

#include <SFML/Graphics.hpp>
#include <mupdf/fitz.h>
#include <mupdf/fitz/context.h>
#include <mupdf/fitz/display-list.h>
#include <mupdf/fitz/document.h>
#include <mupdf/fitz/pixmap.h>
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

    sf::Image render_page(int page_number, float zoom, bool subpixel) override {
        // https://www.mail-archive.com/zathura@lists.pwmt.org/msg00344.html
        // http://arkanis.de/weblog/2023-08-14-simple-good-quality-subpixel-text-rendering-in-opengl-with-stb-truetype-and-dual-source-blending

        fz_pixmap* pix;
        { // render to (fz_pixmap *)pix, 3x width if subpixel rendering is enabled.
            fz_display_list* list;
            fz_device* dev = NULL;

            fz_rect bbox;
            fz_page* page;
            fz_var(dev);
            fz_try(ctx) {
                page = fz_load_page(ctx, doc, page_number);
                bbox = fz_bound_page(ctx, page);
                bbox.x1 = (int)(bbox.x1 * zoom);
                bbox.y1 = (int)(bbox.y1 * zoom);
                bbox.x1 *= subpixel ? 3 : 1;
                list = fz_new_display_list(ctx, bbox);

                dev = fz_new_list_device(ctx, list);
                fz_run_page(ctx, page, dev, fz_scale((subpixel ? 3 : 1) * zoom, 1 * zoom), NULL);
            }
            fz_always(ctx) {
                fz_close_device(ctx, dev);
                fz_drop_device(ctx, dev);
                fz_drop_page(ctx, page);
            }
            fz_catch(ctx) {
                fz_report_error(ctx);
                throw std::runtime_error("failed to render page");
            }

            dev = NULL;
            fz_var(dev);
            fz_try(ctx) {
                pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), fz_irect_from_rect(bbox), NULL, 0);
                fz_clear_pixmap_with_value(ctx, pix, 0xff);

                dev = fz_new_draw_device(ctx, fz_identity, pix);
                fz_run_display_list(ctx, list, dev, fz_identity, bbox, NULL);
            }
            fz_always(ctx) {
                fz_close_device(ctx, dev);
                fz_drop_device(ctx, dev);
            }
            fz_catch(ctx) {
                fz_report_error(ctx);
                throw std::runtime_error("failed to render page");
            }
            fz_drop_display_list(ctx, list);
        }

        auto filter = [&](float x0, float x1, float x2, float x3, float x4) -> float {
            return x0 * (1.0 / 9) + x1 * (2.0 / 9) + x2 * (3.0 / 9) + x3 * (2.0 / 9) + x4 * (1.0 / 9);
            // float w1 = (float)0x08 / 256;
            // float w2 = (float)0x4d / 256;
            // float w3 = (float)0x56 / 256;
            // return x0 * w1 + x1 * w2 + x2 * w3 + x3 * w2 + x4 * w1;
        };
        auto filter1 = [&](float x0, float x1, float x2, float x3, float x4) -> float {
            float w1 = (float)0x08 / 256;
            float w2 = (float)0x4d / 256;
            float w3 = (float)0x56 / 256;
            return x0 * w1 + x1 * w2 + x2 * w3 + x3 * w2 + x4 * w1;
        };
        auto filter2 = [&](float x0, float x1, float x2, float x3, float x4) -> float {
            return x0 * (1.0 / 9) + x1 * (2.0 / 9) + x2 * (3.0 / 9) + x3 * (2.0 / 9) + x4 * (1.0 / 9);
        };

        // R: [0.15, 0.25, 0.3,  0.25, 0.15]
        // G: [0.1,  0.3,  0.6,  0.3,  0.1]   ← Stronger center (green)
        // B: [0.15, 0.25, 0.3,  0.25, 0.15]

        unsigned int w
            = subpixel ? pix->w / 3 : pix->w;
        unsigned int h = pix->h;
        unsigned char* output_pixels = new unsigned char[w * h * 4];

        int i = 0;
        for (int y = 0; y < h; ++y) {
            unsigned char* p = &pix->samples[y * pix->stride];

            if (subpixel) {
                output_pixels[i * 4 + 0] = p[0];
                output_pixels[i * 4 + 1] = p[1];
                output_pixels[i * 4 + 2] = p[2];
                output_pixels[i * 4 + 3] = 255;
                i += 1;
                p += pix->n * 3;

                for (int x = 1; x < w - 1; ++x) {
                    int n = pix->n;

                    // p += 1;
                    // float g = filter2(p[-n * 2], p[-n], p[0], p[n], p[n * 2]);
                    // p += 1;
                    // float b = filter1(p[-n * 2], p[-n], p[0], p[n], p[n * 2]);
                    // p += 1;
                    // float r = filter1(p[-n * 2], p[-n], p[0], p[n], p[n * 2]);

                    p += 1;
                    float r = filter(p[-n * 2], p[-n], p[0], p[n], p[n * 2]);
                    p += 1;
                    float g = filter(p[-n * 2], p[-n], p[0], p[n], p[n * 2]);
                    p += 1;
                    float b = filter(p[-n * 2], p[-n], p[0], p[n], p[n * 2]);

                    output_pixels[i * 4 + 0] = (int)r;
                    output_pixels[i * 4 + 1] = (int)g;
                    output_pixels[i * 4 + 2] = (int)b;
                    output_pixels[i * 4 + 3] = 255;

                    i += 1;
                    p += pix->n * 2;
                }

                output_pixels[i * 4 + 0] = p[0];
                output_pixels[i * 4 + 1] = p[1];
                output_pixels[i * 4 + 2] = p[2];
                output_pixels[i * 4 + 3] = 255;
                i += 1;
                p += pix->n * 3;
            } else {
                for (int x = 0; x < w; ++x) {
                    output_pixels[i * 4 + 0] = p[0];
                    output_pixels[i * 4 + 1] = p[1];
                    output_pixels[i * 4 + 2] = p[2];
                    output_pixels[i * 4 + 3] = 255;

                    p += pix->n;
                    i += 1;
                }
            }
        }
        auto ret = sf::Image({ w, h }, output_pixels);
        delete[] output_pixels;
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
