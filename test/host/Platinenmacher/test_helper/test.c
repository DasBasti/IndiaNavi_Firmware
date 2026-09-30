#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "helper.h"

void test_null_is_null()
{
    char *next = readline(0, 0);
    TEST_ASSERT_NULL(next);
}

void test_empty_string_is_empty()
{
    char dst[100] = {1};
    char *next = readline("", dst);
    TEST_ASSERT_EQUAL_CHAR(0, dst[0]);
    TEST_ASSERT_NULL(next);
}

void test_ignore_carriage_return()
{
    char dst[100] = {1};
    char *src = "line one\rline two";
    char *next = readline(src, dst);
    TEST_ASSERT_EQUAL_STRING("line oneline two", dst);
    TEST_ASSERT_NULL(next);
}

void test_two_lines_string_is_two_lines()
{
    char dst[100] = {1};
    char *src = "line one\r\nline two";
    char *next = readline(src, dst);
    TEST_ASSERT_EQUAL_STRING("line one", dst);
    next = readline(next, dst);
    TEST_ASSERT_EQUAL_STRING("line two", dst);
    TEST_ASSERT_NULL(next);
    next = readline(next, dst);
    TEST_ASSERT_NULL(next);
}

void test_readline_n_truncates_long_lines()
{
    char dst[5];
    char *src = "0123456789\nnext";
    char *next = readline_n(src, dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("0123", dst);
    TEST_ASSERT_EQUAL_STRING("next", next);
    next = readline_n(next, dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("next", dst);
    TEST_ASSERT_NULL(next);
}

void test_readline_n_zero_size()
{
    char dst[1] = {1};
    TEST_ASSERT_NULL(readline_n("abc", dst, 0));
    TEST_ASSERT_EQUAL_CHAR(1, dst[0]);
}

void test_umlauts_are_converted()
{
    char text[] = "M\xc3\xbc" "nchen \xc3\xb6l \xc3\xa4 \xc3\x9f \xc3\x9c" "ber";
    convert_umlauts_inplace(text);
    TEST_ASSERT_EQUAL_STRING("Muenchen oel ae ss Ueber", text);
}

void test_umlaut_lead_byte_at_end_stays_in_bounds()
{
    // lead byte of a UTF-8 sequence as last character, followed by a canary
    char text[] = { 'a', (char)0xc3, 0, 0x7f, 0 };
    convert_umlauts_inplace(text);
    TEST_ASSERT_EQUAL_CHAR(0x7f, text[3]);
    convert_umlauts_inplace(NULL);
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_readline_n_truncates_long_lines);
    RUN_TEST(test_readline_n_zero_size);
    RUN_TEST(test_umlauts_are_converted);
    RUN_TEST(test_umlaut_lead_byte_at_end_stays_in_bounds);
    RUN_TEST(test_two_lines_string_is_two_lines);
    RUN_TEST(test_ignore_carriage_return);
    RUN_TEST(test_empty_string_is_empty);
    RUN_TEST(test_null_is_null);
    UNITY_END();
}
