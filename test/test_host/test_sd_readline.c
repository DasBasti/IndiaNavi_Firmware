#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "helper.h"

void test_null_is_null() {
    char* next = readline(0, 0);
    TEST_ASSERT_NULL(next);

}

void test_empty_string_is_empty() {
    char dst[100]= {1};
    char* next = readline("", dst);
    TEST_ASSERT_EQUAL_CHAR(0, dst[0]);
    TEST_ASSERT_NULL(next);
}

void test_ignore_carriage_return() {
    char dst[100] = {1};
    char *src = "line one\rline two";
    char* next = readline(src, dst);
    TEST_ASSERT_EQUAL_STRING("line oneline two", dst);
    TEST_ASSERT_NULL(next);
}

void test_two_lines_string_is_two_lines() {
    char dst[100] = {1};
    char *src = "line one\r\nline two";
    char* next = readline(src, dst);
    TEST_ASSERT_EQUAL_STRING("line one", dst);
    next = readline(next, dst);
    TEST_ASSERT_EQUAL_STRING("line two", dst);
    TEST_ASSERT_NULL(next);
    next = readline(next, dst);
    TEST_ASSERT_NULL(next);
}

void RUN_TESTS_READLINE() {
    RUN_TEST(test_two_lines_string_is_two_lines);
    RUN_TEST(test_ignore_carriage_return);
    RUN_TEST(test_empty_string_is_empty);
    RUN_TEST(test_null_is_null);
}