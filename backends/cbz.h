#include "backend.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <zip.h>

#pragma once

class CBZ : public Backend {
private:
    std::vector<std::string> pages;
    zip_t* zip;

    // Follows the definition on Wikipedia:
    // https://en.wikipedia.org/wiki/Lanczos_resampling
    double lanczos2(double x) {
        constexpr double a = 2;
        if (x == 0)
            return 1;
        else if (-a <= x && x < a)
            return a * sin(M_PI * x) * sin(M_PI * x / a) / (M_PI * M_PI * x * x);
        else
            return 0;
    }

    sf::Image resize(sf::Image img, float zoom) {
        // src & out data are len(w * h * 4); 4 channels, RGBA.
        auto [src_w, src_h] = img.getSize();
        const uint8_t* src_data = img.getPixelsPtr();

        unsigned int out_w = src_w * zoom;
        unsigned int out_h = src_h * zoom;
        uint8_t* out_data = (uint8_t*)malloc(out_w * out_h * 4);

        // For each output pixel we can map/scale it to a corresponding
        // position in the source image; we then take the five nearest pixels
        // to this position and compute their distances, and we apply the
        // lanczos function to these distances (its called lanczos resampling,
        // after all). The resulting numbers are called "weights."
        //
        // This is a memoization/caching step -- computing lanczos2 25-times
        // for each output pixel (x,y) is expensive, but by mapping the output
        // pixel to a position in the source image and pre-computing the
        // weights we gain huge savings/speedups.
        struct KernelEntry {
            int lo, hi;
            double weights[5];
        };

        // compute the weights in the y-direction.
        struct KernelEntry weights_ys[out_h];
        for (int y = 0; y < out_h; ++y) {
            double srcY = (double)y / zoom;

            int lo = ceil(srcY - 3);
            if (lo < 0)
                lo = 0;
            int hi = floor(srcY + 3 - 1e-6f);
            if (hi > src_h - 1)
                hi = src_h - 1;
            assert(hi - lo <= 5);

            weights_ys[y] = (struct KernelEntry) { lo, hi };
            for (int y_ = lo; y_ <= hi; ++y_) {
                weights_ys[y].weights[y_ - lo] = lanczos2(y_ - srcY);
            }
        }

        // compute the weights in the x-direction.
        struct KernelEntry weights_xs[out_w];
        for (int x = 0; x < out_w; ++x) {
            double srcX = (double)x / zoom;

            int lo = ceil(srcX - 3);
            if (lo < 0)
                lo = 0;
            int hi = floor(srcX + 3 - 1e-6f);
            if (hi > src_w - 1)
                hi = src_w - 1;
            assert(hi - lo <= 5);

            weights_xs[x] = (struct KernelEntry) { lo, hi };
            for (int x_ = lo; x_ <= hi; ++x_) {
                weights_xs[x].weights[x_ - lo] = lanczos2(x_ - srcX);
            }
        }

        // loop through each output pixel
        for (int y = 0; y < out_h; ++y) {
            for (int x = 0; x < out_w; ++x) {
                int out_i = (y * out_w + x) * 4;
                out_data[out_i + 3] = 255; // alpha = 255

                // loop through each color channel independently (RGB)
                for (int channel = 0; channel < 3; ++channel) {
                    double sum = 0;
                    double total_weight = 0;

                    // compute the kernel stuff
                    for (int y_ = weights_ys[y].lo; y_ <= weights_ys[y].hi; ++y_) {
                        for (int x_ = weights_xs[x].lo; x_ <= weights_xs[x].hi; ++x_) {
                            double wy = weights_ys[y].weights[y_ - weights_ys[y].lo];
                            double wx = weights_xs[x].weights[x_ - weights_xs[x].lo];

                            double weight = wx * wy;
                            sum += src_data[(y_ * src_w + x_) * 4 + channel] * weight;
                            total_weight += weight;
                        }
                    }
                    out_data[out_i + channel] = std::clamp((int)(sum / total_weight), 0, 255);
                }
            }
        }

        return sf::Image({ out_w, out_h }, out_data);
    }

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

    sf::Image render_page(int page_number, float zoom, bool subpixel) override {
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
        return resize(res, zoom);
    }

    int count_pages() override {
        return pages.size();
    }
};
