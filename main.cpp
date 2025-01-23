#include <SFML/Graphics.hpp>
#include <fstream>
#include <imgui-SFML.h>
#include <imgui.h>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

#include "SFML/Window/Mouse.hpp"
#include "backends/backend.h"
#include "backends/cbz.h"
#include "backends/pdf.h"

struct Settings {
    bool dual_mode = false;
    int current_page = 0;
};

// Remember settings for each document/path that is opened.
class Metadata {
    const std::string METADATA_FILE = "/home/kjc/.pdf_viewer";

    std::unordered_map<std::string, Settings> data;

public:
    void init() {
        std::ifstream ifs(METADATA_FILE);
        std::string line;
		// very finicky
        while (std::getline(ifs, line)) {
            int i = line.find(' ');
            int page = std::stoi(line);
            int j = line.find(' ', i + 1);
			std::cout << i << " " << j << " " << line.substr(j + 1) << std::endl;
            data[line.substr(j + 1)] = { (bool)std::stoi(line.substr(i + 1, j)), page };
        }
    }

    void save(const char* filename, Settings setting) {
        data[filename] = setting;

        // bad serialisation lol, assumes no newline in filename (maybe use library?)
        std::ofstream out(METADATA_FILE);
        for (auto [k, v] : data) {
            out << v.current_page << " " << v.dual_mode << " " << k << "\n";
        }
    }

    Settings query(const char* filename) {
        return data.contains(filename) ? data[filename] : Settings {};
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

    void fitPage() {
        auto [wx, wy] = window.getSize();
        auto [pw, ph] = page_texture.getSize();
        pw *= zoom;
        ph *= zoom;

        if ((float)pw / ph < (float)wx / wy) {
            zoom = (float)wy / ph;
        } else {
            zoom = (float)wx / pw;
        }
    }

    sf::Image concatImagesHorizontally(const sf::Image& image1, const sf::Image& image2) {
        // Get the dimensions of the two images
        unsigned int width1 = image1.getSize().x;
        unsigned int height1 = image1.getSize().y;
        unsigned int width2 = image2.getSize().x;
        unsigned int height2 = image2.getSize().y;

        // Calculate the dimensions of the new image
        unsigned int newWidth = width1 + width2;
        unsigned int newHeight = std::max(height1, height2);

        // Create a new image with the calculated dimensions
        sf::Image newImage({ newWidth, newHeight }, sf::Color::Transparent);

        // Copy the pixels from the first image to the new image
        for (unsigned int y = 0; y < height1; ++y) {
            for (unsigned int x = 0; x < width1; ++x) {
                newImage.setPixel({ x, y }, image1.getPixel({ x, y }));
            }
        }

        // Copy the pixels from the second image to the new image
        for (unsigned int y = 0; y < height2; ++y) {
            for (unsigned int x = 0; x < width2; ++x) {
                newImage.setPixel({ width1 + x, y }, image2.getPixel({ x, y }));
            }
        }

        return newImage;
    }

    void renderPage() {
        sf::Image img = backend->render_page(settings.current_page);
        if (settings.dual_mode) {
            img = concatImagesHorizontally(img, backend->render_page(settings.current_page + 1));
        }

        page_texture = sf::Texture(img);
        page_texture.setSmooth(true);
        page_sprite = new sf::Sprite(page_texture);
        page_sprite->scale({ zoom, zoom });

        auto [tx, ty] = page_texture.getSize();
        tx *= zoom;
        ty *= zoom;
        auto [wx, wy] = window.getSize();
        page_sprite->setPosition({
            wx / 2.0f - tx / 2.0f,
            wy / 2.0f - ty / 2.0f,
        });
    }

    void renderGUI() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Table of Contents")) {
                if (toc.empty()) {
                    ImGui::MenuItem("Empty... file has no TOC");
                }

                for (const auto& entry : toc) {
                    ImGui::SetCursorPosX(20.0f * (entry.level + 1));

                    if (ImGui::MenuItem(entry.title.c_str())) {
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
    bool isPanning = false;
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
        } else if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
            if (io.WantCaptureKeyboard)
                return;
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
                zoom *= 1.2f;
                renderPage();
                break;
            case sf::Keyboard::Scancode::Down:
            case sf::Keyboard::Scancode::Hyphen:
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
                settings.dual_mode = !settings.dual_mode;
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
                zoom /= 1.2f;
                renderPage();
            } else {
                zoom *= 1.2f;
                renderPage();
            }
        }
    }

    void nextPage() {
        if (settings.dual_mode) {
            if (settings.current_page + 2 < page_count) {
                settings.current_page += 2;
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
            if (settings.current_page > 1) {
                settings.current_page -= 2;
            }
        } else {
            if (settings.current_page > 0) {
                settings.current_page -= 1;
            }
        }
        renderPage();
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
		fitPage();
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
