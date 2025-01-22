#include <string>
#include <vector>
#include <SFML/Graphics.hpp>

#pragma once

struct TOCEntry {
    std::string title, uri;
    int level;
};

class Backend {
public:
    virtual sf::Image render_page(int page_number, float zoom) = 0;
    virtual std::vector<TOCEntry> load_outline() { return {}; };
    virtual int resolve(std::string uri) { return 0; };
    virtual std::pair<int, int> size(int page_number) = 0;
    virtual int count_pages() = 0;
};
