#include <string>
#include <vector>
#include <SFML/Graphics.hpp>

#pragma once

struct TOCEntry {
    std::string title;
    int page, level;
};

class Backend {
public:
    virtual std::vector<TOCEntry> load_outline() { return {}; };
    virtual std::optional<sf::Image> render_page(int page_number) = 0;
    virtual int count_pages() = 0;
};
