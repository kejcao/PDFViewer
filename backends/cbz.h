#include "backend.h"

#include <archive.h>
#include <archive_entry.h>
#include <stdexcept>
#include <string>

#pragma once

class CBZ : public Backend {
private:
    struct archive* a;
    struct archive_entry* entry;

    std::vector<std::pair<std::string, sf::Image>> pages;

public:
    ~CBZ() {
        archive_read_close(a);
        archive_read_free(a);
    }

    CBZ(const char* filename) {
        a = archive_read_new();
        archive_read_support_filter_all(a);
        archive_read_support_format_zip(a);

        int r = archive_read_open_filename(a, filename, 10240);
        if (r != ARCHIVE_OK) {
            throw std::runtime_error("Error: " + std::string(archive_error_string(a)));
        }

        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            std::string fp = archive_entry_pathname(entry);
            std::vector<unsigned char> stream;
            const void* buff;
            size_t size;
            la_int64_t offset;
            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                stream.insert(stream.end(), (char*)buff, (char*)buff + size);
            }
            if (stream.size() > 0 && (fp.ends_with(".jpg") || fp.ends_with(".jpeg") || fp.ends_with(".png"))) {
                std::cout << fp << " " << stream.size() << std::endl;
                pages.push_back({ fp, sf::Image((void*)stream.data(), stream.size()) });
            }
        }
    }

    sf::Image render_page(int page_number, float zoom) override {
        return pages[page_number].second;
    }

    std::pair<int, int> size(int page_number) override {
        return { 200, 200 };
    }

    int count_pages() override {
        return pages.size();
    }
};
