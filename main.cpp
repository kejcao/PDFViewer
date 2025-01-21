// https://mupdf.readthedocs.io/en/latest/using-mupdf.html
// https://poe.com/chat/32am59gnfpdkuvwex7w

#include <SFML/Graphics.hpp>
#include <iostream>
#include <mupdf/fitz.h>
#include <mupdf/fitz/color.h>
#include <mupdf/fitz/context.h>
#include <mupdf/fitz/document.h>
#include <mupdf/fitz/geometry.h>

class PDFViewer {
private:
    int current_page, page_count;
    float zoom;
    sf::RenderWindow window;
    sf::Texture page_texture;
    sf::Sprite page_sprite;
    sf::Uint8* pixels = nullptr;

    bool dual_mode = true;

    fz_context* ctx;
    fz_document* doc;

public:
    sf::Vector2f lastMousePos;
    bool isPanning = false;

    void updatePanning() {
        if (sf::Mouse::isButtonPressed(sf::Mouse::Left)) {
            sf::Vector2i mousePos = sf::Mouse::getPosition(window);

            if (!isPanning) {
                isPanning = true;
                lastMousePos = sf::Vector2f(mousePos);
            } else {
                sf::Vector2f delta = sf::Vector2f(mousePos) - lastMousePos;
                page_sprite.move(delta);
                lastMousePos = sf::Vector2f(mousePos);
            }
        } else {
            isPanning = false;
        }
    }

    PDFViewer(const char* filename)
        : current_page(2)
        , zoom(100.0f) {

        /* Create a context to hold the exception stack and various caches. */
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
            = fz_open_document(ctx, "/home/kjc/closet/library/Iain E. Richardson - H264 (2nd edition).pdf");
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

    ~PDFViewer() {
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }

    fz_pixmap* getPixmap(int page_number) {
        auto [wx, wy] = window.getSize();
        fz_pixmap* pix;

        // render page to an RGB pixmap
        fz_try(ctx) {
            fz_page* page = fz_load_page(ctx, doc, page_number);
            fz_rect bbox = fz_bound_page(ctx, page);
            fz_drop_page(ctx, page);

            if (bbox.x1 / bbox.y1 < (float)wx / wy) {
                zoom = wy / bbox.y1;
            } else {
                zoom = wx / bbox.x1;
            }
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

    void renderPage() {
        std::vector<fz_pixmap*> pixmaps = {};
        pixmaps.push_back(getPixmap(current_page));
        if (dual_mode) {
            pixmaps.push_back(getPixmap(current_page + 1));
        }

        if (pixels != nullptr)
            delete[] pixels;
        int width = 0, height = pixmaps[0]->h;
        for (int i = 0; i < pixmaps.size(); ++i) {
            assert(pixmaps[i]->h == pixmaps[(i + 1) % pixmaps.size()]->h);
            width += pixmaps[i]->w;
        }
        pixels = new sf::Uint8[width * height * 4];

        int i = 0;
        for (int y = 0; y < height; ++y) {
            for (auto pix : pixmaps) {
                unsigned char* p = &pix->samples[y * pix->stride];

                for (int x = 0; x < pix->w; ++x) {
                    pixels[i * 4 + 0] = (sf::Uint8)p[0];
                    pixels[i * 4 + 1] = (sf::Uint8)p[1];
                    pixels[i * 4 + 2] = (sf::Uint8)p[2];
                    pixels[i * 4 + 3] = 255;

                    p += pix->n;
                    i += 1;
                }
            }
        }

        page_texture.create(width, height);
        page_texture.update(pixels);
        page_sprite = sf::Sprite();
        page_sprite.setTexture(page_texture);

        auto [tx, ty] = page_texture.getSize();
        auto [wx, wy] = window.getSize();
        page_sprite.setPosition(
            (wx - tx) / 2.0f,
            (wy - ty) / 2.0f);

        for (auto* pix : pixmaps)
            fz_drop_pixmap(ctx, pix);
    }

    void run() {
        window.create(sf::VideoMode(800, 600), "PDF Viewer");
        renderPage();

        while (window.isOpen()) {
            sf::Event event;
            while (window.pollEvent(event)) {
                handleEvent(event);
            }

            updatePanning();

            window.clear(sf::Color::White);
            window.draw(page_sprite);
            window.display();
        }
    }

private:
    void handleEvent(const sf::Event& event) {
        if (event.type == sf::Event::Closed)
            window.close();
        else if (event.type == sf::Event::KeyPressed) {
            switch (event.key.code) {
            case sf::Keyboard::N:
            case sf::Keyboard::Right:
                nextPage();
                break;
            case sf::Keyboard::P:
            case sf::Keyboard::Left:
                previousPage();
                break;
            case sf::Keyboard::Up:
                zoom *= 1.2f;
                renderPage();
                break;
            case sf::Keyboard::Down:
                zoom /= 1.2f;
                renderPage();
                break;
            case sf::Keyboard::Q:
                window.close();
                break;
            }
        } else if (event.type == sf::Event::Resized) {
            sf::FloatRect visibleArea(0, 0, event.size.width, event.size.height);
            window.setView(sf::View(visibleArea));
            renderPage();
        } else if (event.type == sf::Event::MouseWheelMoved) {
            if (event.mouseWheel.delta < 0) {
                zoom /= 1.2f;
                renderPage();
            } else {
                zoom *= 1.2f;
                renderPage();
            }
        }
    }

    void nextPage() {
        if (dual_mode) {
            if (current_page + 2 < page_count) {
                current_page += 2;
            }
        } else {
            if (current_page + 1 < page_count) {
                current_page += 1;
            }
        }
        renderPage();
    }

    void previousPage() {
        if (dual_mode) {
            if (current_page > 1) {
                current_page -= 2;
            }
        } else {
            if (current_page > 0) {
                current_page -= 1;
            }
        }
        renderPage();
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "USAGE: " << argv[0] << " <pdf_file>" << std::endl;
        return 1;
    }

    try {
        PDFViewer viewer(argv[1]);
        viewer.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
