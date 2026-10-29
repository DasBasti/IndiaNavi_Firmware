#include <unity.h>

#include "content_range.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_a_whole_file(void)
{
    content_range_t r;
    TEST_ASSERT_TRUE(content_range_parse("bytes 0-301234566/301234567", &r));
    TEST_ASSERT_TRUE(r.start == 0 && r.end == 301234566 && r.total == 301234567);
}

void test_the_rest_of_a_file_bigger_than_4_gb(void)
{
    content_range_t r;
    TEST_ASSERT_TRUE(content_range_parse("bytes 4294967296-4294967299/4294967300", &r));
    TEST_ASSERT_TRUE(r.start == 4294967296ULL && r.end == 4294967299ULL && r.total == 4294967300ULL);
    TEST_ASSERT_TRUE(content_range_parse("bytes  10 - 19 / 20", &r));
    TEST_ASSERT_TRUE(r.start == 10 && r.end == 19 && r.total == 20);
}

void test_other_texts_are_refused(void)
{
    content_range_t r;
    TEST_ASSERT_FALSE(content_range_parse("", &r));
    TEST_ASSERT_FALSE(content_range_parse("bytes */100", &r));
    TEST_ASSERT_FALSE(content_range_parse("bytes 0-99/*", &r));
    TEST_ASSERT_FALSE(content_range_parse("items 0-99/100", &r));
    TEST_ASSERT_FALSE(content_range_parse("bytes 0-99/100 more", &r));
    TEST_ASSERT_FALSE(content_range_parse("bytes -5-99/100", &r));
    TEST_ASSERT_FALSE(content_range_parse("bytes 50-40/100", &r));
    TEST_ASSERT_FALSE(content_range_parse("bytes 0-100/100", &r));
    TEST_ASSERT_FALSE(content_range_parse("bytes 0-1/99999999999999999999", &r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_whole_file);
    RUN_TEST(test_the_rest_of_a_file_bigger_than_4_gb);
    RUN_TEST(test_other_texts_are_refused);
    return UNITY_END();
}
