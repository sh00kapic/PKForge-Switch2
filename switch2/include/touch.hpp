// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <optional>

namespace forge::ui
{
struct TouchPoint
{
    int x, y;
};

struct TouchRect
{
    int x, y, w, h;

    bool contains(TouchPoint p) const
    {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
};

struct TouchAction
{
    TouchPoint start, end;
    bool dragged = false;

    bool tap() const
    {
        return !dragged && std::abs(end.x - start.x) <= 20 && std::abs(end.y - start.y) <= 20;
    }
};

class TouchGesture
{
    std::optional<TouchPoint> start;
    TouchPoint last{};
    unsigned finger = 0;
    bool blocked = false;
    bool dragged = false;

public:
    std::optional<TouchAction> sample(int count, unsigned id, TouchPoint point, bool ended, bool interrupt)
    {
        if (interrupt || count > 1 || (count && start && id != finger))
        {
            start.reset();
            blocked = true;
        }
        if (count == 0 || (count == 1 && ended))
        {
            std::optional<TouchAction> action;
            if (start && !blocked)
                action = TouchAction{*start, count ? point : last, dragged};
            start.reset();
            blocked = false;
            return action;
        }
        if (blocked)
            return {};
        if (!start)
        {
            start = point;
            finger = id;
            dragged = false;
        }
        dragged |= !TouchAction{*start, point}.tap();
        last = point;
        return {};
    }
};
}
