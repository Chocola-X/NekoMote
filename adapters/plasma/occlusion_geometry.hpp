#pragma once

#include <algorithm>
#include <vector>

namespace nekomote::plasma
{
struct OcclusionRect
{
    int left;
    int top;
    int right;
    int bottom;
};

inline OcclusionRect intersect(OcclusionRect rect, OcclusionRect bounds)
{
    return {std::max(rect.left, bounds.left), std::max(rect.top, bounds.top),
            std::min(rect.right, bounds.right), std::min(rect.bottom, bounds.bottom)};
}

inline OcclusionRect expand(OcclusionRect rect, int left, int right, int top, int bottom)
{
    return {rect.left - left, rect.top - top, rect.right + right, rect.bottom + bottom};
}

inline bool contains(const std::vector<OcclusionRect>& rects, int left, int top, int right,
                     int bottom)
{
    for (const auto& rect : rects)
        if (left >= rect.left && right <= rect.right && top >= rect.top && bottom <= rect.bottom)
            return true;
    return false;
}

inline double covered_fraction(const std::vector<OcclusionRect>& rects, OcclusionRect bounds)
{
    std::vector<int> xs{bounds.left, bounds.right};
    std::vector<int> ys{bounds.top, bounds.bottom};
    for (const auto& rect : rects)
    {
        xs.push_back(std::clamp(rect.left, bounds.left, bounds.right));
        xs.push_back(std::clamp(rect.right, bounds.left, bounds.right));
        ys.push_back(std::clamp(rect.top, bounds.top, bounds.bottom));
        ys.push_back(std::clamp(rect.bottom, bounds.top, bounds.bottom));
    }
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    double covered = 0;
    for (std::size_t i = 0; i + 1 < xs.size(); ++i)
        for (std::size_t j = 0; j + 1 < ys.size(); ++j)
        {
            const int left = xs[i];
            const int right = xs[i + 1];
            const int top = ys[j];
            const int bottom = ys[j + 1];
            if (left < right && top < bottom && contains(rects, left, top, right, bottom))
                covered += static_cast<double>(right - left) * (bottom - top);
        }
    const double total = static_cast<double>(bounds.right - bounds.left) *
                         static_cast<double>(bounds.bottom - bounds.top);
    return total > 0 ? covered / total : 0;
}

constexpr double kOcclusionThreshold = 0.995;

inline bool fully_covered(const std::vector<OcclusionRect>& rects, OcclusionRect bounds,
                          double threshold = kOcclusionThreshold)
{
    const double total = static_cast<double>(bounds.right - bounds.left) *
                         static_cast<double>(bounds.bottom - bounds.top);
    if (total <= 0)
        return false;
    double rectangle_area = 0;
    for (const auto& rect : rects)
        rectangle_area += static_cast<double>(rect.right - rect.left) *
                          static_cast<double>(rect.bottom - rect.top);
    if (rectangle_area < total * threshold)
        return false;
    return covered_fraction(rects, bounds) >= threshold;
}
} // namespace nekomote::plasma
