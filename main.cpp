// https://mupdf.readthedocs.io/en/latest/using-mupdf.html
// https://poe.com/chat/32am59gnfpdkuvwex7w

#include <SFML/Graphics.hpp>
#include <iostream>
#include <mupdf/fitz.h>

class PDFViewer {
private:
    int current_page, page_count;
    float zoom;
    sf::RenderWindow window;
    sf::Texture page_texture;
    sf::Sprite page_sprite;
    fz_pixmap* pix = nullptr;
    sf::Uint8* pixels = nullptr;

    fz_context* ctx;
    fz_document* doc;

public:
    PDFViewer(const char* filename)
        : current_page(0)
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

    void renderPage() {
        fz_matrix ctm;

        /* Compute a transformation matrix for the zoom and rotation desired. */
        /* The default resolution without scaling is 72 dpi. */
        ctm = fz_scale(zoom / 100, zoom / 100);
        ctm = fz_pre_rotate(ctm, 0);

        /* Render page to an RGB pixmap. */
        if (pix != nullptr) {
            fz_drop_pixmap(ctx, pix);
            delete[] pixels;
        }

        fz_try(ctx)
            pix
            = fz_new_pixmap_from_page_number(ctx, doc, current_page, ctm, fz_device_rgb(ctx), 0);
        fz_catch(ctx) {
            fz_report_error(ctx);
            fz_drop_document(ctx, doc);
            fz_drop_context(ctx);
            throw std::runtime_error("cannot render page\n");
        }

        pixels = new sf::Uint8[pix->w * pix->h * 4];

        int i = 0;
        for (int y = 0; y < pix->h; ++y) {
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

        page_texture.create(pix->w, pix->h);
        page_texture.update(pixels);

        page_sprite = sf::Sprite();
        page_sprite.setTexture(page_texture);

        auto [wx, wy] = window.getSize();
        auto [tx, ty] = page_texture.getSize();
        page_sprite.setPosition(
            (wx - tx) / 2.0f,
            (wy - ty) / 2.0f);
    }

    void run() {
        window.create(sf::VideoMode(800, 600), "PDF Viewer");
        renderPage();

        while (window.isOpen()) {
            sf::Event event;
            while (window.pollEvent(event)) {
                handleEvent(event);
            }

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
        }
    }

    void nextPage() {
        if (current_page + 1 < page_count) {
            current_page += 1;
        }
        renderPage();
    }

    void previousPage() {
        if (current_page > 0) {
            current_page -= 1;
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
