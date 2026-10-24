#include "Vector2D.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <string>

struct CellCase
{
    const char* description;
    Vector2D first;
    Vector2D second;
    bool sameCell;
};

void PrintTo(const CellCase& value, std::ostream* stream)
{
    *stream << value.description;
}

class VectorCellTest : public testing::TestWithParam<CellCase>
{
};

TEST_P(VectorCellTest, TruncatesTowardZeroSymmetrically)
{
    const auto& test = GetParam();
    SCOPED_TRACE(test.description);
    Vector2D first(test.first);
    Vector2D second(test.second);
    EXPECT_EQ(first.IntCmp(second), test.sameCell);
    EXPECT_EQ(second.IntCmp(first), test.sameCell);
}

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


INSTANTIATE_TEST_SUITE_P(Coordinates, VectorCellTest, testing::ValuesIn(cases),
                         [](const testing::TestParamInfo<CellCase>& info)
                         {
                             auto name = std::string(info.param.description);
                             std::replace(name.begin(), name.end(), ' ', '_');
                             return name;
                         });
