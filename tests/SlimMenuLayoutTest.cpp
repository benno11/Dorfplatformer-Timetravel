#include "SlimMenuLayout.h"
#include "DifficultySelector.h"

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

        std::vector<Item> docked{
            {300, 24, Align::Center, Align::Start, true},
            {120, 54, Align::Start, Align::End, true},
            {80, 54, Align::Start, Align::Center, true},
            {80, 54, Align::End, Align::Center, true}
        };
        for (int n = 0; n < 20; ++n) docked.push_back({300, 54});
        const auto fixed = build(960, 540, 1, Align::Center, Align::Center, docked, 8, 12);
        require(fixed.maxScroll > 0, "Docked menu should still scroll center content");
        require(fixed.items[0].y + fixed.items[0].h <= fixed.body.y, "Header overlaps center");
        require(fixed.items[1].y >= fixed.columns[0].y + fixed.columns[0].h, "Footer overlaps its own column");
        require(fixed.items[2].x + fixed.items[2].w <= fixed.body.x, "Left dock overlaps center");
        require(fixed.items[3].x >= fixed.body.x + fixed.body.w, "Right dock overlaps center");
        for (size_t n = 0; n < 4; ++n) {
            const auto r = itemRect(fixed, n, (float)fixed.maxScroll);
            require(r.y == fixed.items[n].y, "Fixed controls moved with scrolling");
            require(hit(fixed, n, r.x + r.w / 2, r.y + r.h / 2, (float)fixed.maxScroll), "Fixed control cannot be clicked or tapped");
            require(reveal(fixed, n, 30) == 30, "Selecting a docked control scrolled center");
        }
        const auto moving = itemRect(fixed, 4, 30);
        require(moving.y == fixed.items[4].y - 30, "Center reset did not restore scrolling");
        require(!hit(fixed, 4, moving.x, fixed.items[0].y, 30), "Clipped center intercepted a fixed header");

        Align snapX, snapY;
        require(parseSnap("bottom", "left", snapX, snapY) && snapX == Align::Start && snapY == Align::End,
                "Vertical-first snap syntax was interpreted as another corner");
        require(parseSnap("right", "top", snapX, snapY) && snapX == Align::End && snapY == Align::Start,
                "Horizontal-first snap syntax broke");
        require(!parseSnap("left", "right", snapX, snapY), "Invalid snap axes were accepted");
        std::vector<Item> independent;
        for (Align v : {Align::Start, Align::Center, Align::End})
            for (Align h : {Align::Start, Align::Center, Align::End})
                independent.push_back({100, 30, h, v, true});
        const auto nine = build(960, 540, 1, Align::Center, Align::Center, independent);
        require(nine.items[0].y == nine.items[1].y && nine.items[1].y == nine.items[2].y,
                "Top anchors share a stacking cursor");
        require(nine.items[6].y == nine.items[7].y && nine.items[7].y == nine.items[8].y,
                "Bottom anchors share a stacking cursor");
        for (size_t changed = 0; changed < 9; ++changed) {
            auto expanded = independent;
            expanded.push_back(independent[changed]);
            const auto adjusted = build(960, 540, 1, Align::Center, Align::Center, expanded);
            for (size_t other = 0; other < 9; ++other) if (other != changed)
                require(adjusted.items[other].x == nine.items[other].x && adjusted.items[other].y == nine.items[other].y,
                    "Adding to one snap area moved an unrelated area");
        }
        // The local-level footer has two four-row columns and a one-row center.
        std::vector<Item> footer;
        for (int n = 0; n < 4; ++n) footer.push_back({150, 54, Align::Start, Align::End, true});
        for (int n = 0; n < 4; ++n) footer.push_back({150, 54, Align::End, Align::End, true});
        footer.push_back({150, 54, Align::Center, Align::End, true});
        const auto columns = build(960, 540, 1, Align::Center, Align::Center, footer);
        require(columns.body.h > 100, "Independent footer columns consumed the sum of all their heights");
        require(columns.items[3].y == columns.items[7].y && columns.items[7].y == columns.items[8].y,
                "Footer columns do not end at the same anchor");
        for (size_t n = 0; n < footer.size(); ++n) {
            const auto r = itemRect(columns, n, 100);
            require(hit(columns, n, r.x + r.w / 2, r.y + r.h / 2, 100), "Independent footer lost pointer input");
        }

        std::vector<Item> centralList(20, Item{384, 54});
        centralList.push_back({150, 54, Align::Center, Align::End, true});
        const auto centerOnly = build(960, 540, 1, Align::Center, Align::Center, centralList, 8, 8);
        for (int n = 0; n < 4; ++n) {
            centralList.push_back({384, 54, Align::Start, Align::End, true});
            centralList.push_back({384, 54, Align::End, Align::End, true});
        }
        const auto withSideFooters = build(960, 540, 1, Align::Center, Align::Center, centralList, 8, 8);
        require(withSideFooters.body.y == centerOnly.body.y && withSideFooters.body.h == centerOnly.body.h,
                "Side footers changed the center list clipping height");
        require(withSideFooters.maxScroll == centerOnly.maxScroll, "Side footers changed center scroll range");
        require(withSideFooters.columns[0].h < withSideFooters.body.h && withSideFooters.columns[2].h < withSideFooters.body.h,
                "Each footer must cut only its own column");
        require(withSideFooters.items[0].x >= withSideFooters.columns[0].x + withSideFooters.columns[0].w &&
                withSideFooters.items[0].x + withSideFooters.items[0].w <= withSideFooters.columns[2].x,
                "Center list overlaps another horizontal zone");
        const float endScroll = reveal(withSideFooters, 19, 0);
        const auto finalRow = itemRect(withSideFooters, 19, endScroll);
        require(hit(withSideFooters, 19, finalRow.x + finalRow.w / 2, finalRow.y + finalRow.h / 2, endScroll),
                "Center list end cannot be reached with side footers");
        const float sideX = (float)(withSideFooters.columns[0].x + withSideFooters.columns[0].w - 1);
        const float sharedY = finalRow.y + finalRow.h / 2.0f;
        require(!hit(withSideFooters, 19, sideX, sharedY, endScroll),
                "Center list intercepts pointer input in a side column");
        require(hit(withSideFooters, 25, sideX, sharedY, endScroll),
                "Side footer cannot be activated beside a visible center row");
        centralList.push_back({150, 30, Align::Start, Align::Start, true});
        const auto sideHeader = build(960, 540, 1, Align::Center, Align::Center, centralList, 8, 8);
        require(sideHeader.body.y == withSideFooters.body.y && sideHeader.body.h == withSideFooters.body.h,
                "Side header changed center clipping");

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
        for (const auto size : {std::array<int, 2>{320, 240}, {960, 540}, {1920, 1080}}) {
            const auto popup = DifficultySelector::build(size[0], size[1]);
            for (int option = 0; option < 12; ++option) {
                const auto& r = popup.buttons[option];
                require(r.w > 0 && r.h > 0 && r.x >= popup.panel.x && r.y >= popup.panel.y &&
                    r.x + r.w <= popup.panel.x + popup.panel.w && r.y + r.h <= popup.panel.y + popup.panel.h,
                    "Difficulty popup control escaped its panel");
                require(DifficultySelector::hit(popup, r.x + r.w / 2, r.y + r.h / 2) == option,
                    "Difficulty popup option is obscured or cannot be selected");
            }
            require(DifficultySelector::hit(popup, -1, -1) == -1, "Outside popup click selected a rating");
        }
        int focus = 2;
        for (int next : {5, 8, 0, 10, 2}) {
            focus = DifficultySelector::navigate(focus, 0, 1);
            require(focus == next, "Difficulty keyboard navigation cannot reach the rating and action rows");
        }
        require(DifficultySelector::navigate(10, 1, 0) == 11, "Cannot navigate to Cancel");
        require(DifficultySelector::navigate(11, -1, 0) == 10, "Cannot navigate back to Apply");
        std::cout << "Slim menu layout tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
