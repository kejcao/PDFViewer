#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

#pragma once

struct TOCEntry {
    std::string title;
    int page, level;
};

class Backend {
public:
    virtual sf::Image render_page(int page_number) = 0;
    virtual std::vector<TOCEntry> load_outline() { return {}; };
    virtual int count_pages() = 0;
};
