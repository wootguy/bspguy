//
// chatgpt wrote most of this
//

#include "quant.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <cfloat>
#include "q1_palette.h"

int color_distance(const COLOR3& c1, const COLOR3& c2) {
    return
        (c1.r - c2.r) * (c1.r - c2.r) +
        (c1.g - c2.g) * (c1.g - c2.g) +
        (c1.b - c2.b) * (c1.b - c2.b);
}

COLOR3 average_color(const std::vector<COLOR3>& colors) {
    if (colors.empty()) return COLOR3(0, 0, 0);

    uint64_t r_sum = 0, g_sum = 0, b_sum = 0;
    for (const auto& c : colors) {
        r_sum += c.r;
        g_sum += c.g;
        b_sum += c.b;
    }

    size_t count = colors.size();
    return COLOR3(r_sum / count, g_sum / count, b_sum / count);
}

struct Box {
    std::vector<COLOR3> colors;
    char split_channel;
    int range;

    Box(std::vector<COLOR3>&& input) : colors(std::move(input)) {
        compute_range_and_channel();
    }

    void compute_range_and_channel() {
        if (colors.empty()) {
            split_channel = 'r';
            range = 0;
            return;
        }

        uint8_t r_min = 255, r_max = 0;
        uint8_t g_min = 255, g_max = 0;
        uint8_t b_min = 255, b_max = 0;

        for (const auto& c : colors) {
            r_min = std::min(r_min, c.r); r_max = std::max(r_max, c.r);
            g_min = std::min(g_min, c.g); g_max = std::max(g_max, c.g);
            b_min = std::min(b_min, c.b); b_max = std::max(b_max, c.b);
        }

        int r_range = r_max - r_min;
        int g_range = g_max - g_min;
        int b_range = b_max - b_min;

        if (r_range >= g_range && r_range >= b_range) {
            split_channel = 'r';
            range = r_range;
        }
        else if (g_range >= r_range && g_range >= b_range) {
            split_channel = 'g';
            range = g_range;
        }
        else {
            split_channel = 'b';
            range = b_range;
        }
    }
};

bool sort_r(const COLOR3& a, const COLOR3& b) { return a.r < b.r; }
bool sort_g(const COLOR3& a, const COLOR3& b) { return a.g < b.g; }
bool sort_b(const COLOR3& a, const COLOR3& b) { return a.b < b.b; }

// Median Cut Quantization using cached range data
std::vector<COLOR3> median_cut_quantize(COLOR3* pixels, int pixel_count, int k) {
    std::vector<Box> boxes;
    boxes.emplace_back(std::vector<COLOR3>(pixels, pixels + pixel_count));

    while (boxes.size() < k) {
        // Find box with largest range
        auto it = std::max_element(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) {
            return a.range < b.range;
        });

        if (it == boxes.end() || it->colors.size() <= 1)
            break;

        char channel = it->split_channel;
        switch (channel) {
            case 'r': std::sort(it->colors.begin(), it->colors.end(), sort_r); break;
            case 'g': std::sort(it->colors.begin(), it->colors.end(), sort_g); break;
            case 'b': std::sort(it->colors.begin(), it->colors.end(), sort_b); break;
        }

        size_t mid = it->colors.size() / 2;
        std::vector<COLOR3> lower(it->colors.begin(), it->colors.begin() + mid);
        std::vector<COLOR3> upper(it->colors.begin() + mid, it->colors.end());

        *it = Box(std::move(lower));
        boxes.emplace_back(std::move(upper));
    }

    std::vector<COLOR3> palette;
    for (const Box& box : boxes)
        palette.push_back(average_color(box.colors));

    // quantize the image
    for (int i = 0; i < pixel_count; ++i) {
        int min_dist = INT32_MAX;
        int best = 0;
        for (int j = 0; j < palette.size(); ++j) {
            int d = color_distance(pixels[i], palette[j]);
            if (d < min_dist) {
                min_dist = d;
                best = j;
            }
        }
        pixels[i] = palette[best];
    }

    return palette;
}

int closest_q1_color(COLOR3& c) {
    static COLOR3* qpal = (COLOR3*)g_quake_pal;

    int min_dist = INT32_MAX;
    int best = 0;
    for (int j = 0; j < 256; ++j) {
        int d = color_distance(c, qpal[j]);
        if (d < min_dist) {
            min_dist = d;
            best = j;
        }
    }

    return best;
}

void quantize_to_q1_pal(COLOR3* pal) {
    COLOR3* qpal = (COLOR3*)g_quake_pal;

    for (int i = 0; i < 256; ++i) {
        pal[i] = qpal[closest_q1_color(pal[i])];
    }
}
