//
//  bgl_reader.h
//  bgl
//
//  Created by kejinlu on 2026-02-02
//

#ifndef bgl_reader_h
#define bgl_reader_h

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "bgl_info.h"
#include "bgl_definition.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Status Codes
// ============================================================

typedef enum {
    BGL_OK                = 0,
    BGL_END               = 1,

    BGL_ERR_INVALID_PARAM = -1,
    BGL_ERR_IO            = -2,
    BGL_ERR_FORMAT        = -3,
    BGL_ERR_MEMORY        = -4
} bgl_status;

// ============================================================
// Data Types
// ============================================================

/**
 * @brief BGL dictionary entry information (standard format)
 */
typedef struct {
    char *word;                        /**< Headword (UTF-8, must be freed) */
    bgl_definition def;                /**< Definition fields (must be freed using bgl_free_definition) */
    char **alternates;                 /**< Alternate words array (must be freed, use alternate_count for size) */
    int alternate_count;               /**< Number of alternate words */
} bgl_entry;

/**
 * @brief BGL embedded resource (image, HTML, etc.)
 */
typedef struct {
    char *name;              /**< Resource name (e.g., "image.png", must be freed) */
    uint8_t *data;           /**< Resource data (must be freed) */
    size_t data_size;        /**< Data size in bytes */
} bgl_resource;

// ============================================================
// Forward Declarations (opaque types)
// ============================================================

/** @brief Opaque BGL reader type */
typedef struct bgl_reader bgl_reader;

/** @brief Opaque entry iterator type */
typedef struct bgl_entry_iterator bgl_entry_iterator;

/** @brief Opaque resource iterator type */
typedef struct bgl_resource_iterator bgl_resource_iterator;

// ============================================================
// Lifecycle Management
// ============================================================

/**
 * @brief Open BGL file and initialize parser
 * @param file_path Path to .bgl file
 * @return Parser object on success, NULL on failure
 */
bgl_reader *bgl_reader_open(const char *file_path);

/**
 * @brief Close BGL parser and release all resources
 * @param reader Parser object (NULL is safe)
 */
void bgl_reader_close(bgl_reader *reader);

/**
 * @brief Preload metadata by scanning the entire gzip stream
 * @param reader Parser object
 * @return 0 on success, -1 on failure
 *
 * This is optional. If not called explicitly, metadata is loaded
 * lazily on first access (bgl_get_info, iterators, etc).
 *
 * Use this when you want to control timing, e.g. run on a
 * background thread before the UI needs the data.
 *
 * Safe to call multiple times (idempotent).
 */
int bgl_reader_prepare(bgl_reader *reader);

// ============================================================
// Metadata Access
// ============================================================

/**
 * @brief Get dictionary info (auto on-demand loading)
 * @param reader Parser object
 * @return Info structure pointer
 */
const bgl_info *bgl_get_info(bgl_reader *reader);

/**
 * @brief Get total number of entries
 * @param reader Parser object
 * @return Entry count on success, -1 on failure
 */
int bgl_get_entry_count(bgl_reader *reader);

/**
 * @brief Get total number of resources
 * @param reader Parser object
 * @return Resource count on success, -1 on failure
 */
int bgl_get_resource_count(bgl_reader *reader);

// ============================================================
// Entry Cleanup
// ============================================================

/**
 * @brief Clean up an entry's text fields (word, alternates, definition)
 * @param entry Entry to clean up (modified in-place)
 * @return 0 on success, -1 on failure
 *
 * This function is optional. Call it only if you need cleaned text.
 * Without calling it, entry fields contain raw decoded content from the BGL file.
 */
int bgl_cleanup_entry(bgl_entry *entry);

// ============================================================
// Entry Iterator
// ============================================================

/**
 * @brief Create entry iterator
 * @param reader Parser object
 * @return Iterator object on success, NULL on failure
 *
 * Note: Cannot be used simultaneously with resource iterator (shared gzip stream).
 */
bgl_entry_iterator *bgl_entry_iterator_create(bgl_reader *reader);

/**
 * @brief Get next entry
 * @param iter Entry iterator
 * @param out_entry Output: pointer to current entry (owned by iterator, valid until next call)
 * @return BGL_OK on success, BGL_END if no more entries, BGL_ERR_* on error
 */
bgl_status bgl_entry_iterator_next(bgl_entry_iterator *iter, const bgl_entry **out_entry);

/**
 * @brief Free entry iterator
 * @param iter Entry iterator
 */
void bgl_entry_iterator_free(bgl_entry_iterator *iter);

// ============================================================
// Resource Iterator
// ============================================================

/**
 * @brief Create resource iterator
 * @param reader Parser object
 * @return Iterator object on success, NULL on failure (no resources in file)
 *
 * Note: Cannot be used simultaneously with entry iterator (shared gzip stream).
 */
bgl_resource_iterator *bgl_resource_iterator_create(bgl_reader *reader);

/**
 * @brief Get next resource
 * @param iter Resource iterator
 * @param out_resource Output: pointer to current resource (owned by iterator, valid until next call)
 * @return BGL_OK on success, BGL_END if no more resources, BGL_ERR_* on error
 */
bgl_status bgl_resource_iterator_next(bgl_resource_iterator *iter, const bgl_resource **out_resource);

/**
 * @brief Free resource iterator
 * @param iter Resource iterator
 */
void bgl_resource_iterator_free(bgl_resource_iterator *iter);

#ifdef __cplusplus
}
#endif

#endif /* bgl_reader_h */
