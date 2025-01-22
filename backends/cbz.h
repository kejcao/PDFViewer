#include "backend.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <zip.h>

#pragma once

class CBZ : public Backend {
private:
    std::vector<std::string> pages;
    zip_t* zip;

public:
    ~CBZ() {
        zip_close(zip);
    }

    CBZ(const char* filename) {
        int err = 0;
        zip = zip_open(filename, ZIP_RDONLY, &err);
        if (zip == NULL) {
            throw std::runtime_error(
                "Failed to open cbz file: error " + std::to_string(err));
        }

        for (zip_int64_t i = 0; i < zip_get_num_entries(zip, 0); ++i) {
            std::string fp = zip_get_name(zip, i, 0);
            if (fp.ends_with(".jpg") || fp.ends_with(".png") || fp.ends_with(".jpeg")) { // not robust at all!!!
				std::cout << fp << std::endl;
                pages.push_back(fp);
            }
        }
        std::sort(pages.begin(), pages.end());
    }

    sf::Image render_page(int page_number) override {
        zip_int64_t index = zip_name_locate(zip, pages[page_number].c_str(), 0);
        zip_file_t* file = zip_fopen_index(zip, index, 0);

        char buffer[4096];
        zip_int64_t total_read = 0;
        zip_int64_t bytes_read;
        char* content = NULL;
        size_t content_size = 0;

        while ((bytes_read = zip_fread(file, buffer, sizeof(buffer))) > 0) {
            char* new_content = (char*)realloc(content, content_size + bytes_read);
            content = new_content;
            memcpy(content + content_size, buffer, bytes_read);
            content_size += bytes_read;
            total_read += bytes_read;
        }

        zip_fclose(file);

        auto res = sf::Image(content, content_size);
        free(content);
        return res;
    }

    int count_pages() override {
        return pages.size();
    }
};
