#include "Vector2D.h"

#include <cstdlib>
#include <iostream>

int main()
{
    // IntCmp determines whether objects occupy the same console collision cell.
    // Preserve the legacy truncation toward zero, including negative coordinates.
    struct CellCase
    {
        const char* description;
        Vector2D first;
        Vector2D second;
        bool sameCell;
    };

    const CellCase cases[] = {
        {"same positive cell", {4.125f, 7.875f}, {4.875f, 7.125f}, true},
        {"crossing the X boundary", {4.875f, 7.125f}, {5.0f, 7.125f}, false},
        {"crossing the Y boundary", {4.125f, 7.875f}, {4.125f, 8.0f}, false},
        {"same negative cell", {-4.125f, -7.875f}, {-4.875f, -7.125f}, true},
        {"negative X boundary", {-4.875f, -7.125f}, {-5.0f, -7.125f}, false},
        {"negative Y boundary", {-4.125f, -7.875f}, {-4.125f, -8.0f}, false},
        {"truncation around zero", {-0.875f, 0.875f}, {0.875f, -0.875f}, true},
        {"opposite nonzero cells", {-1.125f, 0.0f}, {1.125f, 0.0f}, false},
        {"same arena corner", {79.0f, 27.0f}, {79.875f, 27.875f}, true},
    };

    int failures = 0;
    for (const auto& test : cases)
    {
        // The existing API is non-const; copies let us check both directions.
        Vector2D first(test.first);
        Vector2D second(test.second);
        if (first.IntCmp(second) != test.sameCell || second.IntCmp(first) != test.sameCell)
        {
            std::cerr << "FAIL: " << test.description << '\n';
            ++failures;
        }
    }

    // Do not use assert: these checks must remain active in Release/NDEBUG.
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
