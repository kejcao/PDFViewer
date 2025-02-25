#include <SFML/Graphics.hpp>
#include <fstream>
#include <imgui-SFML.h>
#include <imgui.h>
#include <iostream>
#include <stdexcept>

#include "SFML/Window/Mouse.hpp"
#include "backends/backend.h"
#include "backends/cbz.h"
#include "backends/pdf.h"
#include "json.hpp"

using json = nlohmann::json;

struct Settings {
    bool dual_mode = false;
    int current_page = 0;
    std::map<char, int> bookmarks;
};

// Remember settings for each document/path that is opened.
class Metadata {
    const std::string METADATA_FILE = "/home/kjc/.pdfviewer.json";

    json data;

public:
    void init() {
        data = json::parse(std::ifstream(METADATA_FILE));
    }

    void save(const char* filename, Settings setting) {
        data[filename] = {
            { "dual_mode", setting.dual_mode },
            { "current_page", setting.current_page },
            { "bookmarks", setting.bookmarks },
        };
        std::ofstream out(METADATA_FILE);
        out << std::setw(4) << data << std::endl;
    }

    Settings query(const char* filename) {
        return data.contains(filename)
            ? Settings {
                  data[filename]["dual_mode"],
                  data[filename]["current_page"],
                  data[filename]["bookmarks"],
              }
            : Settings {};
    }
};

class PDFViewer {
private:
    const char* filename;

    float zoom = 1.0;
    int page_count;
    Backend* backend;
    Settings settings;
    std::vector<TOCEntry> toc;

    Metadata metadata;

    sf::RenderWindow window;
    sf::Texture page_texture;
    sf::Sprite* page_sprite;

    bool subpixel = true;
    bool is_current_page_large = false;

    void fitPage() {
        auto [ww, wh] = window.getSize();
        auto [pw, ph] = page_texture.getSize();

        if ((float)pw / ph < (float)ww / wh) {
            zoom = (float)wh / ph * zoom;
        } else {
            zoom = (float)ww / pw * zoom;
        }
    }

    // Only takes 4-8 ms, surprisingly. Thought using setPixel(), getPixel()
    // might be slow.
    sf::Image concatImagesHorizontally(const sf::Image& image1, const sf::Image& image2) {
        auto [w1, h1] = image1.getSize();
        auto [w2, h2] = image2.getSize();

        sf::Image newImage({ w1 + w2, std::max(h1, h2) }, sf::Color::Transparent);

        for (unsigned int y = 0; y < h1; ++y) {
            for (unsigned int x = 0; x < w1; ++x) {
                newImage.setPixel({ x, y }, image1.getPixel({ x, y }));
            }
        }
        for (unsigned int y = 0; y < h2; ++y) {
            for (unsigned int x = 0; x < w2; ++x) {
                newImage.setPixel({ w1 + x, y }, image2.getPixel({ x, y }));
            }
        }
        return newImage;
    }

    void renderPage(bool handle_special_case = false) {
        using std::chrono::duration_cast;
        using std::chrono::high_resolution_clock;
        using std::chrono::milliseconds;

        if (handle_special_case) {
            if (settings.current_page > 0) {
                settings.current_page -= 1;
            }
        }

        auto t1 = high_resolution_clock::now();

        sf::Image page = backend->render_page(settings.current_page, zoom, subpixel);
        auto [w, h] = page.getSize();
        is_current_page_large = w * 1.2 > h;
        if (handle_special_case && !is_current_page_large) {
            if (settings.current_page > 0) {
                settings.current_page -= 1;
            }
        }
        if (!is_current_page_large && settings.dual_mode && settings.current_page + 1 < page_count) {
            auto second_page = backend->render_page(settings.current_page + 1, zoom, subpixel);
            if (handle_special_case) {
                std::swap(page, second_page);
            }
            page = concatImagesHorizontally(page, second_page);
        }

        page_texture = sf::Texture(page);
        page_sprite = new sf::Sprite(page_texture);

        auto [tx, ty] = page_texture.getSize();
        auto [wx, wy] = window.getSize();
        page_sprite->setPosition({
            wx / 2.0f - tx / 2.0f,
            wy / 2.0f - ty / 2.0f,
        });

        auto t2 = high_resolution_clock::now();
        std::cout << duration_cast<milliseconds>(t2 - t1) << " to render" << std::endl;
    }

    void renderGUI() {
        int i = 0;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Table of Contents")) {
                if (toc.empty()) {
                    ImGui::MenuItem("Empty... file has no TOC");
                }

                for (const auto& entry : toc) {
                    ImGui::SetCursorPosX(20.0f * (entry.level + 1));

                    i += 1;
                    std::string s = entry.title + "##" + std::to_string(i);
                    if (ImGui::MenuItem(s.c_str())) {
                        settings.current_page = entry.page;
                        if (settings.dual_mode && settings.current_page % 2 == 1) {
                            settings.current_page -= 1;
                        }
                        renderPage();
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::Text("Page: %d/%d", settings.current_page + 1, page_count);

            float rightAlignPos = ImGui::GetWindowWidth() - ImGui::CalcTextSize(filename).x - ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine();
            ImGui::SetCursorPosX(rightAlignPos);
            ImGui::Text("%s", filename);

            ImGui::EndMainMenuBar();
        }
    }

    sf::Vector2f lastMousePos;
    bool isPanning = false, shifting = false;
    sf::Keyboard::Scan lastScanCode;
    void handleEvent(const sf::Event& event) {
        ImGuiIO& io = ImGui::GetIO();

        if (event.is<sf::Event::Closed>()) {
            window.close();
        } else if (const auto* mousePress = event.getIf<sf::Event::MouseButtonPressed>()) {
            if (io.WantCaptureMouse)
                return;

            if (mousePress->button == sf::Mouse::Button::Right) {
                nextPage();
            }
            if (mousePress->button == sf::Mouse::Button::Left) {
                if (!isPanning) {
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    isPanning = true;
                    lastMousePos = sf::Vector2f(mousePos);
                }
            }
        } else if (const auto* mousePress = event.getIf<sf::Event::MouseButtonReleased>()) {
            if (io.WantCaptureMouse)
                return;
            if (mousePress->button == sf::Mouse::Button::Left) {
                isPanning = false;
            }
        } else if (const auto* keyReleased = event.getIf<sf::Event::KeyReleased>()) {
            if (keyReleased->scancode == sf::Keyboard::Scancode::LShift) {
                shifting = false;
            }
        } else if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
            if (keyPressed->scancode == sf::Keyboard::Scancode::LShift) {
                shifting = true;
            }
            if (io.WantCaptureKeyboard)
                return;

            if (lastScanCode == sf::Keyboard::Scancode::M) {
                settings.bookmarks[(char)keyPressed->scancode] = settings.current_page;
                lastScanCode = sf::Keyboard::Scancode::N; // dummy value
                renderPage();
                return;
            }
            if (lastScanCode == sf::Keyboard::Scancode::Apostrophe) {
                settings.current_page = settings.bookmarks[(char)keyPressed->scancode];
                lastScanCode = sf::Keyboard::Scancode::N; // dummy value
                renderPage();
                return;
            }
            lastScanCode = keyPressed->scancode;

            switch (keyPressed->scancode) {
            case sf::Keyboard::Scancode::N:
            case sf::Keyboard::Scancode::Space:
            case sf::Keyboard::Scancode::Right:
                nextPage();
                break;
            case sf::Keyboard::Scancode::P:
            case sf::Keyboard::Scancode::Left:
                previousPage();
                break;
            case sf::Keyboard::Scancode::Up:
            case sf::Keyboard::Scancode::Equal:
                zoomIn();
                renderPage();
                break;
            case sf::Keyboard::Scancode::Down:
            case sf::Keyboard::Scancode::Hyphen:
                zoomOut();
                renderPage();
                break;
            case sf::Keyboard::Scancode::T:
                subpixel = !subpixel;
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
                settings.dual_mode = !settings.dual_mode;
                renderPage();
                break;
            case sf::Keyboard::Scancode::G:
                settings.current_page = shifting
                    ? (settings.dual_mode ? page_count - 2 : page_count - 1)
                    : 0;
                renderPage();
                break;
            }
        } else if (const auto* ev = event.getIf<sf::Event::Resized>()) {
            sf::FloatRect visibleArea({ 0, 0 }, { (float)ev->size.x, (float)ev->size.y });
            window.setView(sf::View(visibleArea));
            renderPage();
        } else if (const auto* mouseWheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
            if (io.WantCaptureMouse)
                return;
            if (mouseWheel->delta < 0) {
                zoomOut();
                renderPage();
            } else {
                zoomIn();
                renderPage();
            }
        }
    }

    void zoomIn() {
        if (zoom < 2) {
            zoom *= 1.2;
        }
    }

    void zoomOut() {
        if (zoom > .2) {
            zoom /= 1.2;
        }
    }

    void nextPage() {
        if (settings.dual_mode) {
            if (is_current_page_large) {
                if (settings.current_page + 1 < page_count) {
                    settings.current_page += 1;
                }
            } else {
                if (settings.current_page + 2 < page_count) {
                    settings.current_page += 2;
                }
            }
        } else {
            if (settings.current_page + 1 < page_count) {
                settings.current_page += 1;
            }
        }
        renderPage();
    }

    void previousPage() {
        if (settings.dual_mode) {
            renderPage(true);
        } else {
            if (settings.current_page > 0) {
                settings.current_page -= 1;
            }
            renderPage();
        }
    }

public:
    ~PDFViewer() {
        metadata.save(filename, settings);
    }

    PDFViewer(const char* filename)
        : filename { filename } {

        std::string s = filename;
        if (s.ends_with(".pdf")) {
            backend = new PDF(filename);
        } else if (s.ends_with(".cbz")) {
            backend = new CBZ(filename);
        } else {
            throw std::runtime_error("error: unknown file extension");
        }

        toc = backend->load_outline();
        page_count = backend->count_pages();

        metadata.init();
        settings = metadata.query(filename);
    }

    void run() {
        window.create(sf::VideoMode({ 800, 600 }), "PDF Viewer");
        window.setVerticalSyncEnabled(true);
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

            renderGUI();
            if (isPanning) {
                sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                sf::Vector2f delta = sf::Vector2f(mousePos) - lastMousePos;
                page_sprite->move(delta);
                lastMousePos = sf::Vector2f(mousePos);
            }

            window.clear(sf::Color::Black);
            window.draw(*page_sprite);
            ImGui::SFML::Render(window);
            window.display();
        }
        ImGui::SFML::Shutdown();
    }
};

int main(int argc, char** argv) {
    //  if (true) {
    //      using std::chrono::duration_cast;
    //      using std::chrono::high_resolution_clock;
    //      using std::chrono::milliseconds;
    //
    //      Backend* backend = new PDF("/home/kjc/closet/library/Roberto Casati_ Patrick Cavanagh - The Visual World of Shadows-MIT Press (2019).pdf");
    //      int page_count = backend->count_pages();
    //      for (int i = 0; i < page_count; ++i) {
    //          auto t1 = high_resolution_clock::now();
    // sf::Image a = backend->render_page(i, 2, true);
    // sf::Image b = backend->render_page(i+1, 2, true);
    // sf::Image s = concatImagesHorizontally(a,b);
    //          auto t2 = high_resolution_clock::now();
    //          std::cout << duration_cast<milliseconds>(t2 - t1) << std::endl;
    //      }
    //      return 0;
    //  }

    if (argc < 2) {
        std::cout << "USAGE: " << argv[0] << " <pdf_file>" << std::endl;
        return 1;
    }

    char* path = realpath(argv[1], NULL); // no need to free
    try {
        PDFViewer viewer(path);
        viewer.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
