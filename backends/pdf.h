#include "backend.h"

#include <SFML/Graphics.hpp>
#include <iostream>
#include <mupdf/fitz.h>
#include <mupdf/fitz/color.h>
#include <mupdf/fitz/context.h>
#include <mupdf/fitz/document.h>
#include <mupdf/fitz/geometry.h>
#include <mupdf/fitz/outline.h>
#include <mupdf/fitz/pixmap.h>
#include <optional>
#include <pthread.h>
#include <stdexcept>
#include <thread>

#pragma once

void fail(std::string msg) {
    std::cerr << msg << std::endl;
    abort();
}

class PDF : public Backend {
private:
    fz_context* ctx;
    fz_document* doc;
    const char* filename;

    std::thread render;

    int page_count;
    std::unordered_map<int, sf::Image> pages;

public:
    ~PDF() {
        render.join();
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }

    std::pair<int, int> size(int page_number) override {
        fz_page* page = fz_load_page(ctx, doc, page_number);
        fz_rect bbox = fz_bound_page(ctx, page);
        fz_drop_page(ctx, page);
        return { bbox.x1, bbox.y1 };
    }

    std::optional<sf::Image> render_page(int page_number, float zoom) override {
        return pages.contains(page_number) ? std::optional(pages[page_number]) : std::nullopt;
    }

    // We have to abide by mupdf's C threading API. Hence no std::thread.
    void populate_pages() {
        struct thread_data {
            fz_context* ctx;
            int page_number;

            fz_display_list* list;
            fz_rect bbox;

            sf::Image result_image;
        };

        auto renderer = [](void* data_) -> void* {
            struct thread_data* data = (struct thread_data*)data_;
            int pagenumber = data->page_number;
            fz_context* ctx = data->ctx;
            fz_display_list* list = data->list;
            fz_rect bbox = data->bbox;
            fz_device* dev = NULL;

            // The context pointer is pointing to the main thread's
            // context, so here we create a new context based on it for
            // use in this thread.
            ctx = fz_clone_context(ctx);

            // Next we run the display list through the draw device which
            // will render the request area of the page to the pixmap.
            fz_var(dev);

            fz_pixmap* pix;
            fz_try(ctx) {
                // Create a white pixmap using the correct dimensions.
                pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), fz_round_rect(bbox), NULL, 0);
                fz_clear_pixmap_with_value(ctx, pix, 0xff);

                // Do the actual rendering.
                dev = fz_new_draw_device(ctx, fz_identity, pix);
                fz_run_display_list(ctx, list, dev, fz_identity, bbox, NULL);
                fz_close_device(ctx, dev);
            }
            fz_always(ctx)
                fz_drop_device(ctx, dev);
            fz_catch(ctx)
                fail("thread failed to render.");

            // Free this thread's context.
            fz_drop_context(ctx);

            auto pixmap_to_image = [](fz_pixmap* pix) -> sf::Image {
                unsigned char* pixels = new unsigned char[pix->w * pix->h * 4];
                int j = 0;
                for (int y = 0; y < pix->h; ++y) {
                    unsigned char* p = &pix->samples[y * pix->stride];

                    for (int x = 0; x < pix->w; ++x) {
                        pixels[j * 4 + 0] = p[0];
                        pixels[j * 4 + 1] = p[1];
                        pixels[j * 4 + 2] = p[2];
                        pixels[j * 4 + 3] = 255;

                        p += pix->n;
                        j += 1;
                    }
                }
                unsigned int w = pix->w;
                unsigned int h = pix->h;

                // Surprisingly, copying to pixels is not slow at all. The slow part is
                // this sf::Image initialization -- even without passing in pixels!
                auto ret = sf::Image({ w, h }, pixels);

                delete[] pixels;
                return ret;
            };

            data->result_image = pixmap_to_image(pix);
            fz_drop_pixmap(ctx, pix);

            return data;
        };

        pthread_mutex_t mutexes[FZ_LOCK_MAX];
        for (int i = 0; i < FZ_LOCK_MAX; i++) {
            if (pthread_mutex_init(&mutexes[i], NULL) != 0)
                fail("pthread_mutex_init()");
        }

        fz_locks_context locks = {
            mutexes,
            [](void* user, int lock) { // lock mutex
                pthread_mutex_t* mutex = (pthread_mutex_t*)user;
                if (pthread_mutex_lock(&mutex[lock]) != 0)
                    fail("pthread_mutex_lock()");
            },
            [](void* user, int lock) { // unlock mutex
                pthread_mutex_t* mutex = (pthread_mutex_t*)user;
                if (pthread_mutex_unlock(&mutex[lock]) != 0)
                    fail("pthread_mutex_unlock()");
            }
        };

        // Reuse these in constructor?
        fz_context* render_ctx = fz_new_context(NULL, &locks, FZ_STORE_UNLIMITED);
        fz_document* render_doc = NULL;

        pthread_t* threads = NULL;

        fz_var(threads);
        fz_var(render_doc);

        fz_try(render_ctx) {
            fz_register_document_handlers(render_ctx);
            render_doc = fz_open_document(render_ctx, filename);

            threads = (pthread_t*)malloc(page_count * sizeof(*threads));

            for (int i = 0; i < page_count; i++) {
                fz_page* page;
                fz_rect bbox;
                fz_display_list* list;
                fz_device* dev = NULL;
                fz_pixmap* pix;
                struct thread_data* data;

                fz_var(dev);
                fz_try(render_ctx) {
                    const float dpi_scale_factor = 144.0 / 72; // 144 dpi

                    // we create a display list that will hold drawing commands
                    // for page. only one thread at a time can ever be
                    // accessing the document.
                    page = fz_load_page(render_ctx, render_doc, i);

                    bbox = fz_bound_page(render_ctx, page);
                    bbox = fz_transform_rect(bbox, fz_scale(dpi_scale_factor, dpi_scale_factor));

                    list = fz_new_display_list(render_ctx, bbox);

                    dev = fz_new_list_device(render_ctx, list);
                    fz_run_page(render_ctx, page, dev, fz_scale(dpi_scale_factor, dpi_scale_factor), NULL);
                    fz_close_device(render_ctx, dev);
                }
                fz_always(render_ctx) {
                    fz_drop_device(render_ctx, dev);
                    fz_drop_page(render_ctx, page);
                }
                fz_catch(render_ctx)
                    fz_rethrow(render_ctx);

                data = new (struct thread_data) {
                    .ctx = render_ctx,
                    .page_number = i + 1,
                    .list = list,
                    .bbox = bbox,
                };

                if (pthread_create(&threads[i], NULL, renderer, data) != 0)
                    throw std::runtime_error("pthread_create()");
            }

            for (int i = 0; i < page_count; i++) {
                struct thread_data* data;

                if (pthread_join(threads[i], (void**)&data) != 0)
                    throw std::runtime_error("pthread_join");
                pages[i] = data->result_image;

                fz_drop_display_list(render_ctx, data->list);
                delete data;
            }
        }
        fz_always(render_ctx) {
            free(threads);
            fz_drop_document(render_ctx, render_doc);
        }
        fz_catch(render_ctx) {
            fz_report_error(render_ctx);
            fail("error");
        }

        fz_drop_context(render_ctx);
    }

    PDF(const char* filename)
        : filename { filename } {
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

        render = std::thread(&Backend::init, this);
    }

    void init() override {
        auto now = []() {
            using namespace std::chrono;
            return duration_cast<milliseconds>(
                system_clock::now().time_since_epoch());
        };

        auto a = now();
        populate_pages();
        auto b = now();
        std::cout << "time took to render pages: " << b - a << std::endl;
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

    int resolve(std::string uri) override {
        fz_link_dest dest = fz_resolve_link_dest(ctx, doc, uri.c_str());
        return dest.loc.page;
    }

    int count_pages() override {
        return page_count;
    }
};
