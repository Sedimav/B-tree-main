#include <gtest/gtest.h>
#include <filesystem>
#include <string>

#include "dbms/index/b_tree_disk_index.h"

namespace
{

std::filesystem::path fresh_index_path(const char* file_name)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "coursework_dbms_b_tree_tests";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);

    const std::filesystem::path path = root / file_name;
    std::filesystem::remove_all(path, ec);
    return path;
}

}

TEST(bTreeDiskIndexPositiveTests, test0)
{
    const std::string path = fresh_index_path("btree_index_test0.idx").string();
    dbms::BTreeDiskIndex<int> index(path);
    dbms::Rid rid1{1, 10};
    dbms::Rid rid2{2, 20};
    EXPECT_TRUE(index.insert(4, rid1));
    EXPECT_TRUE(index.insert(9, rid2));
    dbms::Rid out{};
    EXPECT_TRUE(index.find(4, out));
    EXPECT_EQ(out.page_id, rid1.page_id);
    EXPECT_EQ(out.slot_id, rid1.slot_id);
    EXPECT_TRUE(index.find(9, out));
    EXPECT_EQ(out.page_id, rid2.page_id);
    EXPECT_EQ(out.slot_id, rid2.slot_id);
}

TEST(bTreeDiskIndexPositiveTests, test1)
{
    const std::string path = fresh_index_path("btree_index_test1.idx").string();
    dbms::BTreeDiskIndex<int> index(path);
    for (int i = 1; i <= 50; ++i) {
        EXPECT_TRUE(index.insert(i, dbms::Rid{i, i * 2}));
    }
    EXPECT_FALSE(index.insert(25, dbms::Rid{25, 500}));
    dbms::Rid out{};
    EXPECT_TRUE(index.find(25, out));
    EXPECT_EQ(out.page_id, 25);
    EXPECT_EQ(out.slot_id, 50);
}

TEST(bTreeDiskIndexPositiveTests, test2)
{
    const std::string path = fresh_index_path("btree_index_test2.idx").string();
    dbms::BTreeDiskIndex<int> index(path);
    for (int i = 1; i <= 30; ++i) {
        EXPECT_TRUE(index.insert(i, dbms::Rid{i, i + 1}));
    }
    for (int i = 2; i <= 30; i += 2) {
        EXPECT_TRUE(index.erase(i));
    }
    dbms::Rid out{};
    for (int i = 1; i <= 30; ++i) {
        if (i % 2 == 0) {
            EXPECT_FALSE(index.find(i, out));
        } else {
            EXPECT_TRUE(index.find(i, out));
            EXPECT_EQ(out.page_id, i);
            EXPECT_EQ(out.slot_id, i + 1);
        }
    }
}

TEST(bTreeDiskIndexPositiveTests, test3)
{
    const std::string path = fresh_index_path("btree_index_test3.idx").string();
    dbms::BTreeDiskIndex<int> index(path);
    for (int i = 1; i <= 20; ++i) {
        EXPECT_TRUE(index.insert(i, dbms::Rid{i, i * 3}));
    }
    auto result = index.range(5, 9);
    ASSERT_EQ(result.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(result[i].first, 5 + i);
        EXPECT_EQ(result[i].second.page_id, 5 + i);
        EXPECT_EQ(result[i].second.slot_id, (5 + i) * 3);
    }
}

TEST(bTreeDiskIndexPositiveTests, test4)
{
    const std::string path = fresh_index_path("btree_index_test4.idx").string();
    {
        dbms::BTreeDiskIndex<int> index(path);
        for (int i = 1; i <= 10; ++i) {
            EXPECT_TRUE(index.insert(i, dbms::Rid{i, i * 5}));
        }
    }
    {
        dbms::BTreeDiskIndex<int> index(path);
        dbms::Rid out{};
        EXPECT_TRUE(index.find(1, out));
        EXPECT_EQ(out.page_id, 1);
        EXPECT_EQ(out.slot_id, 5);
        EXPECT_TRUE(index.find(10, out));
        EXPECT_EQ(out.page_id, 10);
        EXPECT_EQ(out.slot_id, 50);
    }
}

TEST(bTreeDiskIndexNegativeTests, test1)
{
    const std::string path = fresh_index_path("btree_index_test5.idx").string();
    dbms::BTreeDiskIndex<int> index(path);
    dbms::Rid out{};
    EXPECT_FALSE(index.find(123, out));
    EXPECT_FALSE(index.erase(123));
}

TEST(bTreeDiskIndexNegativeTests, insertReturnsFalseWhenIndexPathIsDirectory)
{
    const std::filesystem::path path = fresh_index_path("btree_index_path_is_dir.idx");
    std::error_code ec;
    std::filesystem::create_directories(path, ec);

    dbms::BTreeDiskIndex<int> index(path.string());
    EXPECT_FALSE(index.insert(1, dbms::Rid{1, 1}));

    std::filesystem::remove_all(path, ec);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
