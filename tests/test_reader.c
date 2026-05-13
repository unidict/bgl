//
//  test_reader.c
//  libbgl tests
//
//  Integration tests for bgl_reader using a real BGL dictionary file.
//  Tests open, prepare, get_info, entry iterator, resource iterator, cleanup.
//

#include "unity.h"
#include "bgl_reader.h"
#include "bgl_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Test BGL file path
// ============================================================

// Derive test data path from this source file's location.
// __FILE__ is always the full path to test_reader.c at compile time.
// Works for Xcode, CMake, and CI without hardcoding an absolute path.

static const char *get_test_bgl_path(void) {
    // Find last '/' in __FILE__ to get the source directory
    const char *file = __FILE__;
    const char *last_slash = NULL;
    const char *p;
    for (p = file; *p; p++) {
        if (*p == '/' || *p == '\\') last_slash = p;
    }
    static char path[1024] = {0};
    if (path[0] == '\0') {
        if (last_slash) {
            int dir_len = (int)(last_slash - file);
            snprintf(path, sizeof(path), "%.*s/data/english_chinese_s_.bgl", dir_len, file);
        }
    }
    return path;
}

#define TEST_BGL_PATH get_test_bgl_path()

void test_reader_open_null_path(void) {
    bgl_reader *r = bgl_reader_open(NULL);
    TEST_ASSERT_NULL(r);
}

void test_reader_open_nonexistent(void) {
    bgl_reader *r = bgl_reader_open("/tmp/nonexistent_bgl_file_12345.bgl");
    TEST_ASSERT_NULL(r);
}

void test_reader_close_null(void) {
    bgl_reader_close(NULL);
}

void test_reader_open_valid(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_NOT_NULL(r);
    bgl_reader_close(r);
}

// ============================================================
// Tests: bgl_reader_prepare / get_info
// ============================================================

void test_reader_prepare(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_reader_close(r);
}

void test_reader_prepare_idempotent(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_reader_close(r);
}

void test_reader_prepare_null(void) {
    TEST_ASSERT_EQUAL_INT(-1, bgl_reader_prepare(NULL));
}

void test_reader_get_info_title(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    const bgl_info *info = bgl_get_info(r);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_NOT_NULL(info->title);
    TEST_ASSERT_EQUAL_STRING("Babylon English-Chinese (S)", info->title);
    bgl_reader_close(r);
}

void test_reader_get_info_author(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    const bgl_info *info = bgl_get_info(r);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_NOT_NULL(info->author);
    TEST_ASSERT_EQUAL_STRING("Babylon Ltd.", info->author);
    bgl_reader_close(r);
}

void test_reader_get_info_email(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    const bgl_info *info = bgl_get_info(r);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_NOT_NULL(info->email);
    TEST_ASSERT_EQUAL_STRING("linguistic-support@babylon.com", info->email);
    bgl_reader_close(r);
}

void test_reader_get_info_description(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    const bgl_info *info = bgl_get_info(r);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_NOT_NULL(info->description);
    TEST_ASSERT_NOT_NULL(strstr(info->description, "English"));
    TEST_ASSERT_NOT_NULL(strstr(info->description, "Chinese"));
    bgl_reader_close(r);
}

void test_reader_get_info_utf8_mode(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    const bgl_info *info = bgl_get_info(r);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_FALSE(info->utf8_mode);
    bgl_reader_close(r);
}

void test_reader_get_info_null(void) {
    const bgl_info *info = bgl_get_info(NULL);
    TEST_ASSERT_NULL(info);
}

// ============================================================
// Tests: entry / resource counts
// ============================================================

void test_reader_entry_count(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(70919, bgl_get_entry_count(r));
    bgl_reader_close(r);
}

void test_reader_resource_count(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(2, bgl_get_resource_count(r));
    bgl_reader_close(r);
}

void test_reader_entry_count_null(void) {
    TEST_ASSERT_EQUAL_INT(-1, bgl_get_entry_count(NULL));
}

void test_reader_resource_count_null(void) {
    TEST_ASSERT_EQUAL_INT(-1, bgl_get_resource_count(NULL));
}

// ============================================================
// Tests: entry iterator
// ============================================================

void test_reader_entry_iterator_create_null(void) {
    bgl_entry_iterator *it = bgl_entry_iterator_create(NULL);
    TEST_ASSERT_NULL(it);
}

void test_reader_entry_iterator_first_entry(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_entry_iterator *it = bgl_entry_iterator_create(r);
    TEST_ASSERT_NOT_NULL(it);

    const bgl_entry *e;
    TEST_ASSERT_EQUAL(BGL_OK, bgl_entry_iterator_next(it, &e));
    TEST_ASSERT_EQUAL_STRING("A Doll's House", e->word);
    TEST_ASSERT_NOT_NULL(e->def.body);
    TEST_ASSERT_EQUAL_STRING("小家家", e->def.body);
    TEST_ASSERT_EQUAL_INT(1, e->alternate_count);

    bgl_entry_iterator_free(it);
    bgl_reader_close(r);
}

void test_reader_entry_with_title_and_pos(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_entry_iterator *it = bgl_entry_iterator_create(r);
    bgl_entry_iterator_next(it, &(const bgl_entry *){NULL}); // skip entry 0

    const bgl_entry *e;
    TEST_ASSERT_EQUAL(BGL_OK, bgl_entry_iterator_next(it, &e));
    TEST_ASSERT_NOT_NULL(strstr(e->word, "A.C."));
    TEST_ASSERT_EQUAL_STRING("交流电", e->def.body);
    TEST_ASSERT_NOT_NULL(e->def.title);
    TEST_ASSERT_NOT_NULL(strstr(e->def.title, "alternating current"));

    bgl_entry_iterator_free(it);
    bgl_reader_close(r);
}

void test_reader_entry_with_pos(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_entry_iterator *it = bgl_entry_iterator_create(r);
    bgl_entry_iterator_next(it, &(const bgl_entry *){NULL}); // skip 0
    bgl_entry_iterator_next(it, &(const bgl_entry *){NULL}); // skip 1

    const bgl_entry *e;
    TEST_ASSERT_EQUAL(BGL_OK, bgl_entry_iterator_next(it, &e));
    TEST_ASSERT_NOT_NULL(strstr(e->word, "A.D."));
    TEST_ASSERT_EQUAL_STRING("公元...年", e->def.body);
    TEST_ASSERT_NOT_NULL(e->def.part_of_speech);
    TEST_ASSERT_EQUAL_STRING("n.", e->def.part_of_speech);
    TEST_ASSERT_NOT_NULL(e->def.title);
    TEST_ASSERT_EQUAL_INT(2, e->alternate_count);

    bgl_entry_iterator_free(it);
    bgl_reader_close(r);
}

void test_reader_entry_with_alternates(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_entry_iterator *it = bgl_entry_iterator_create(r);
    bgl_entry_iterator_next(it, &(const bgl_entry *){NULL}); // skip 0
    bgl_entry_iterator_next(it, &(const bgl_entry *){NULL}); // skip 1
    bgl_entry_iterator_next(it, &(const bgl_entry *){NULL}); // skip 2

    const bgl_entry *e;
    TEST_ASSERT_EQUAL(BGL_OK, bgl_entry_iterator_next(it, &e));
    TEST_ASSERT_EQUAL_INT(2, e->alternate_count);
    TEST_ASSERT_EQUAL_STRING("A M", e->alternates[0]);
    TEST_ASSERT_EQUAL_STRING("AM", e->alternates[1]);
    TEST_ASSERT_NOT_NULL(e->def.body);
    TEST_ASSERT_NOT_NULL(strstr(e->def.body, "午前"));

    bgl_entry_iterator_free(it);
    bgl_reader_close(r);
}

void test_reader_entry_iterator_iterates_all(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_entry_iterator *it = bgl_entry_iterator_create(r);
    TEST_ASSERT_NOT_NULL(it);

    int count = 0;
    const bgl_entry *e;
    bgl_status status;
    while ((status = bgl_entry_iterator_next(it, &e)) == BGL_OK) {
        count++;
    }
    TEST_ASSERT_EQUAL_INT(70919, count);
    TEST_ASSERT_EQUAL(BGL_END, status);

    bgl_entry_iterator_free(it);
    bgl_reader_close(r);
}

void test_reader_entry_chinese_body_encoding(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_entry_iterator *it = bgl_entry_iterator_create(r);

    const bgl_entry *e;
    TEST_ASSERT_EQUAL(BGL_OK, bgl_entry_iterator_next(it, &e));
    TEST_ASSERT_NOT_NULL(e->def.body);

    // If encoding is wrong (CP1252 instead of CP936), body would contain
    // garbage bytes. Verify body starts with valid UTF-8 for "小" (E5 B0 8F).
    const uint8_t *p = (const uint8_t *)e->def.body;
    TEST_ASSERT_EQUAL_UINT8(0xE5, p[0]);
    TEST_ASSERT_EQUAL_UINT8(0xB0, p[1]);
    TEST_ASSERT_EQUAL_UINT8(0x8F, p[2]);

    bgl_entry_iterator_free(it);
    bgl_reader_close(r);
}

void test_reader_entry_iterator_free_null(void) {
    bgl_entry_iterator_free(NULL);
}

// ============================================================
// Tests: resource iterator
// ============================================================

void test_reader_resource_iterator_create_null(void) {
    bgl_resource_iterator *it = bgl_resource_iterator_create(NULL);
    TEST_ASSERT_NULL(it);
}

void test_reader_resource_iterator_first_resource(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_resource_iterator *it = bgl_resource_iterator_create(r);
    TEST_ASSERT_NOT_NULL(it);

    const bgl_resource *res;
    TEST_ASSERT_EQUAL(BGL_OK, bgl_resource_iterator_next(it, &res));
    TEST_ASSERT_EQUAL_STRING("8EAF66FD.bmp", res->name);
    TEST_ASSERT_TRUE(res->data_size > 0);
    TEST_ASSERT_EQUAL_UINT8(0x42, res->data[0]); // 'B'
    TEST_ASSERT_EQUAL_UINT8(0x4D, res->data[1]); // 'M'

    bgl_resource_iterator_free(it);
    bgl_reader_close(r);
}

void test_reader_resource_iterator_html_resource(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_resource_iterator *it = bgl_resource_iterator_create(r);
    TEST_ASSERT_NOT_NULL(it);

    bgl_resource_iterator_next(it, &(const bgl_resource *){NULL}); // skip BMP

    const bgl_resource *res;
    TEST_ASSERT_EQUAL(BGL_OK, bgl_resource_iterator_next(it, &res));
    TEST_ASSERT_EQUAL_STRING("C2EEF3F6.html", res->name);
    TEST_ASSERT_TRUE(res->data_size > 0);
    TEST_ASSERT_EQUAL_UINT8('<', res->data[0]);
    TEST_ASSERT_EQUAL_UINT8('h', res->data[1]);

    bgl_resource_iterator_free(it);
    bgl_reader_close(r);
}

void test_reader_resource_iterator_count(void) {
    bgl_reader *r = bgl_reader_open(TEST_BGL_PATH);
    TEST_ASSERT_EQUAL_INT(0, bgl_reader_prepare(r));
    bgl_resource_iterator *it = bgl_resource_iterator_create(r);
    TEST_ASSERT_NOT_NULL(it);

    int count = 0;
    const bgl_resource *res;
    bgl_status status;
    while ((status = bgl_resource_iterator_next(it, &res)) == BGL_OK) {
        count++;
    }
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL(BGL_END, status);

    bgl_resource_iterator_free(it);
    bgl_reader_close(r);
}

void test_reader_resource_iterator_free_null(void) {
    bgl_resource_iterator_free(NULL);
}

// ============================================================
// Tests: bgl_cleanup_entry
// ============================================================

void test_reader_cleanup_entry_null(void) {
    TEST_ASSERT_EQUAL_INT(-1, bgl_cleanup_entry(NULL));
}

void test_reader_cleanup_entry_strips_word(void) {
    bgl_entry e;
    memset(&e, 0, sizeof(e));
    e.word = bgl_strdup("test$123$word");
    e.def.body = bgl_strdup("body text");
    TEST_ASSERT_EQUAL_INT(0, bgl_cleanup_entry(&e));
    TEST_ASSERT_EQUAL_STRING("testword", e.word);
    free(e.word);
    free(e.def.body);
}

// ============================================================
// Test Runner
// ============================================================

void run_reader_tests(void) {
    UnityBegin("test_reader.c");

    // open / close
    RUN_TEST(test_reader_open_null_path);
    RUN_TEST(test_reader_open_nonexistent);
    RUN_TEST(test_reader_close_null);
    RUN_TEST(test_reader_open_valid);

    // prepare / info
    RUN_TEST(test_reader_prepare);
    RUN_TEST(test_reader_prepare_idempotent);
    RUN_TEST(test_reader_prepare_null);
    RUN_TEST(test_reader_get_info_title);
    RUN_TEST(test_reader_get_info_author);
    RUN_TEST(test_reader_get_info_email);
    RUN_TEST(test_reader_get_info_description);
    RUN_TEST(test_reader_get_info_utf8_mode);
    RUN_TEST(test_reader_get_info_null);

    // counts
    RUN_TEST(test_reader_entry_count);
    RUN_TEST(test_reader_resource_count);
    RUN_TEST(test_reader_entry_count_null);
    RUN_TEST(test_reader_resource_count_null);

    // entry iterator
    RUN_TEST(test_reader_entry_iterator_create_null);
    RUN_TEST(test_reader_entry_iterator_first_entry);
    RUN_TEST(test_reader_entry_with_title_and_pos);
    RUN_TEST(test_reader_entry_with_pos);
    RUN_TEST(test_reader_entry_with_alternates);
    RUN_TEST(test_reader_entry_iterator_iterates_all);
    RUN_TEST(test_reader_entry_chinese_body_encoding);
    RUN_TEST(test_reader_entry_iterator_free_null);

    // resource iterator
    RUN_TEST(test_reader_resource_iterator_create_null);
    RUN_TEST(test_reader_resource_iterator_first_resource);
    RUN_TEST(test_reader_resource_iterator_html_resource);
    RUN_TEST(test_reader_resource_iterator_count);
    RUN_TEST(test_reader_resource_iterator_free_null);

    // cleanup
    RUN_TEST(test_reader_cleanup_entry_null);
    RUN_TEST(test_reader_cleanup_entry_strips_word);

    UnityEnd();
}
