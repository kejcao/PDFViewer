// https://mupdf.readthedocs.io/en/latest/using-mupdf.html
// https://mupdf.readthedocs.io/_/downloads/en/latest/pdf/
// https://poe.com/chat/32am59gnfpdkuvwex7w

#include <SFML/Graphics.hpp>
#include <imgui-SFML.h>
#include <imgui.h>
#include <iostream>
#include <mupdf/fitz.h>
#include <mupdf/fitz/color.h>
#include <mupdf/fitz/context.h>
#include <mupdf/fitz/document.h>
#include <mupdf/fitz/geometry.h>
#include <mupdf/fitz/outline.h>

class PDFViewer {
private:
    int current_page, page_count;
    float zoom;
    sf::RenderWindow window;
    sf::Texture page_texture;
    sf::Sprite* page_sprite;
    unsigned char* pixels = nullptr;

    bool dual_mode = false;

    fz_context* ctx;
    fz_document* doc;

public:
    sf::Vector2f lastMousePos;
    bool isPanning = false;

    void updatePanning() {
        if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
            sf::Vector2i mousePos = sf::Mouse::getPosition(window);

            if (!isPanning) {
                isPanning = true;
                lastMousePos = sf::Vector2f(mousePos);
            } else {
                sf::Vector2f delta = sf::Vector2f(mousePos) - lastMousePos;
                page_sprite->move(delta);
                lastMousePos = sf::Vector2f(mousePos);
            }
        } else {
            isPanning = false;
        }
    }

    void print_outline(fz_context* ctx, fz_outline* outline, int level) {
        while (outline) {
            for (int i = 0; i < level; i++) {
                printf("  ");
            }

            // Print the title and URI of the outline item
            printf("%s", outline->title ? outline->title : "No Title");
            if (outline->uri) {
                printf(" -> %s", outline->uri);
            }
            printf("\n");

            // Recursively print the child items
            if (outline->down) {
                print_outline(ctx, outline->down, level + 1);
            }

            // Move to the next item at the same level
            outline = outline->next;
        }
    }

    PDFViewer(const char* filename)
        : current_page(0)
        , zoom(1.0f) {

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

        fz_outline* outline;
        fz_try(ctx) {
            outline = fz_load_outline(ctx, doc);
        }
        fz_catch(ctx) {
            throw std::runtime_error("cannot load table of contents\n");
        }
        print_outline(ctx, outline, 0);
        fz_drop_outline(ctx, outline);
    }

    ~PDFViewer() {
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }

    void fitPage() {
        auto [wx, wy] = window.getSize();
        fz_page* page = fz_load_page(ctx, doc, current_page);
        fz_rect bbox = fz_bound_page(ctx, page);
        fz_drop_page(ctx, page);

        if (bbox.x1 / bbox.y1 < (float)wx / wy) {
            zoom = wy / bbox.y1;
        } else {
            zoom = wx / bbox.x1;
        }
    }

    fz_pixmap* getPixmap(int page_number) {
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

    void renderPage() {
        std::vector<fz_pixmap*> pixmaps = {};
        pixmaps.push_back(getPixmap(current_page));
        if (dual_mode) {
            pixmaps.push_back(getPixmap(current_page + 1));
        }

        if (pixels != nullptr)
            delete[] pixels;
        unsigned int width = 0, height = 0;
        for (int i = 0; i < pixmaps.size(); ++i) {
            if (pixmaps[i]->h > height) {
                height = pixmaps[i]->h;
            }
            width += pixmaps[i]->w;
        }
        std::cout << width << " " << height << "\n";
        pixels = new unsigned char[width * height * 4];

        int i = 0;
        for (int y = 0; y < height; ++y) {
            for (auto pix : pixmaps) {
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
        }

        page_texture = sf::Texture({ width, height });
        page_texture.update(pixels);
        page_sprite = new sf::Sprite(page_texture);

        auto [tx, ty] = page_texture.getSize();
        auto [wx, wy] = window.getSize();
        page_sprite->setPosition({
            (wx - tx) / 2.0f,
            (wy - ty) / 2.0f,
        });

        for (auto* pix : pixmaps)
            fz_drop_pixmap(ctx, pix);
    }

    void run() {
        window.create(sf::VideoMode({ 800, 600 }), "PDF Viewer");
        window.setFramerateLimit(60);
        auto _ = ImGui::SFML::Init(window);
        renderPage();

        sf::Clock deltaClock;
        while (window.isOpen()) {
            while (const std::optional event = window.pollEvent()) {
                if (event.has_value()) {
                    ImGui::SFML::ProcessEvent(window, event.value());
                    handleEvent(event.value());
                }
            }
            ImGui::SFML::Update(window, deltaClock.restart());

            updatePanning();
            renderGUI();

            window.clear(sf::Color::White);
            window.draw(*page_sprite);
            ImGui::SFML::Render(window);
            window.display();
        }
        ImGui::SFML::Shutdown();
    }

private:
    void renderGUI() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("View")) {
                ImGui::EndMenu();
            }

            // Display current page number
            ImGui::Text("Page: %d/%d", current_page + 1, fz_count_pages(ctx, doc));

            ImGui::EndMainMenuBar();
        }
    }
    void handleEvent(const sf::Event& event) {
        if (event.is<sf::Event::Closed>()) {
            window.close();
        } else if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
            switch (keyPressed->scancode) {
            case sf::Keyboard::Scancode::N:
            case sf::Keyboard::Scancode::Right:
                nextPage();
                break;
            case sf::Keyboard::Scancode::P:
            case sf::Keyboard::Scancode::Left:
                previousPage();
                break;
            case sf::Keyboard::Scancode::Up:
                zoom *= 1.2f;
                renderPage();
                break;
            case sf::Keyboard::Scancode::Down:
                zoom /= 1.2f;
                renderPage();
                break;
            case sf::Keyboard::Scancode::Q:
                window.close();
                break;
            case sf::Keyboard::Scancode::W:
                fitPage();
                renderPage();
                break;
            case sf::Keyboard::Scancode::D:
                dual_mode = !dual_mode;
                renderPage();
                break;
            }
        } else if (const auto* ev = event.getIf<sf::Event::Resized>()) {
            sf::FloatRect visibleArea({ 0, 0 }, { (float)ev->size.x, (float)ev->size.y });
            window.setView(sf::View(visibleArea));
            renderPage();
        } else if (const auto* mouseWheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
            if (mouseWheel->delta < 0) {
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
