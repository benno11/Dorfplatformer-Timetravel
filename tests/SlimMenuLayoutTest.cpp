#include "SlimMenuLayout.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace SlimMenuLayout;
    try {
        const std::vector<Item> anchors{
            {200, 24, Align::Start, Align::Start},
            {300, 54, Align::Center, Align::Center},
            {200, 24, Align::End, Align::End}
        };
        const auto aligned = build(960, 540, 1, Align::Center, Align::Center, anchors);
        require(aligned.items[0].x == aligned.body.x && aligned.items[0].y == aligned.body.y, "Top-left alignment failed");
        require(aligned.items[1].x == aligned.body.x + (aligned.body.w - 300) / 2, "Horizontal centering failed");
        require(aligned.items[1].y == aligned.body.y + (aligned.body.h - 54) / 2, "Vertical centering failed");
        require(aligned.items[2].x + 200 == aligned.body.x + aligned.body.w, "Right alignment failed");
        require(aligned.items[2].y + 24 == aligned.body.y + aligned.body.h, "Bottom alignment failed");
        require(aligned.maxScroll == 0, "A fitting menu should not scroll");

        const auto smallLeft = build(960, 540, 0.5f, Align::Start, Align::Start, anchors);
        const auto smallRight = build(960, 540, 0.5f, Align::End, Align::End, anchors);
        require(smallLeft.panel.x * 0.5f == 16 && smallLeft.panel.y * 0.5f == 12, "Scaled menu start anchor failed");
        require((smallRight.panel.x + smallRight.panel.w) * 0.5f == 944, "Scaled menu right anchor failed");
        require((smallRight.panel.y + smallRight.panel.h) * 0.5f == 528, "Scaled menu bottom anchor failed");
        require(smallLeft.items[1].w == aligned.items[1].w, "Menu scaling changed logical object size");

        std::vector<Item> many(40, Item{520, 54, Align::Center, Align::Center});
        const auto longMenu = build(960, 540, 1, Align::Center, Align::Center, many);
        require(longMenu.maxScroll > 0, "Overflowing menu did not scroll");
        for (size_t i = 1; i < longMenu.items.size(); ++i)
            require(longMenu.items[i].y >= longMenu.items[i - 1].y + 54, "Overflowing rows overlap");
        const float bottom = reveal(longMenu, many.size() - 1, 0);
        require(bottom == longMenu.maxScroll, "Keyboard selection did not reveal the last item");
        const auto last = scrolled(longMenu.items.back(), bottom);
        require(last.y + last.h <= longMenu.body.y + longMenu.body.h, "Last button cannot be reached");
        require(hit(longMenu, many.size() - 1, last.x + 1, last.y + 1, bottom), "Scrolled button hit test failed");
        require(!hit(longMenu, 0, longMenu.items[0].x + 1, longMenu.items[0].y + 1, bottom), "Hidden button was clickable");
        require(!hit(longMenu, 0, longMenu.items[0].x + 1, longMenu.body.y - 1, 10), "Clipped part of button was clickable");
        require(hit(longMenu, 0, longMenu.items[0].x + 1, longMenu.body.y + 1, 10), "Visible part of clipped button was not clickable");
        require(reveal(longMenu, 0, bottom) == 0, "Keyboard selection did not return to the top");
        require(reveal(longMenu, 999, 99999) == longMenu.maxScroll, "Invalid selection did not clamp scrolling");

        std::vector<Item> grouped;
        for (auto vertical : {Align::Start, Align::Center, Align::End})
            for (int i = 0; i < 10; ++i) grouped.push_back({300, 54, Align::Center, vertical});
        const auto groups = build(960, 540, 1, Align::Center, Align::Center, grouped);
        for (size_t i = 1; i < groups.items.size(); ++i)
            require(groups.items[i].y >= groups.items[i - 1].y + 54, "Vertical alignment groups overlap on overflow");
        const auto zoomed = build(960, 540, 2, Align::Center, Align::Center, many);
        require(zoomed.maxScroll > longMenu.maxScroll, "Zooming did not account for the smaller logical viewport");
        require((zoomed.panel.x + zoomed.panel.w) * zoomed.scale <= 960, "Zoomed menu extended beyond the screen");
        const auto empty = build(960, 540, 1, Align::Center, Align::Center, {});
        require(empty.maxScroll == 0 && empty.items.empty(), "Empty menu layout failed");
        const auto tiny = build(120, 120, 4, Align::Center, Align::Center, many);
        require(tiny.body.y >= tiny.panel.y && tiny.body.y + tiny.body.h <= tiny.panel.y + tiny.panel.h,
                "Content viewport escaped a small zoomed panel");
        require(tiny.body.w > 0 && tiny.body.h > 0, "Small menu produced an empty viewport");
        require(reveal(tiny, 0, 0) == 0, "Oversized selected row should reveal its top");
        require(build(960, 540, std::numeric_limits<float>::quiet_NaN(), Align::Center, Align::Center, {}).scale == 1,
                "Non-finite scale was not handled");
        std::cout << "Slim menu layout tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
