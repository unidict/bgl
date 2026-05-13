//
//  bgl_reader.c
//  libud
//
//  Created by kejinlu on 2026-02-02
//

#include "bgl_reader.h"
#include "bgl_definition.h"
#include "bgl_pos.h"
#include "bgl_text.h"
#include "bgl_util.h"
#include "bgl_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>

// ============================================================
// Constants
// ============================================================

/** BGL file signature */
#define BGL_SIGNATURE_1 0x12340001
#define BGL_SIGNATURE_2 0x12340002

/** BGL file header size */
#define BGL_HEADER_SIZE 6

/** Maximum buffer size */
#define BGL_MAX_BUFFER_SIZE (10 * 1024 * 1024)  // 10MB

// ============================================================
// Internal Data Types
// ============================================================

/**
 * @brief BGL block types
 */
typedef enum {
    BGL_BLOCK_TYPE_EXTENDED = 0,    /**< Extended type (followed by 1-byte type code) */
    BGL_BLOCK_TYPE_ENTRY = 1,       /**< Dictionary entry (standard format) */
    BGL_BLOCK_TYPE_RESOURCE = 2,    /**< Embedded resource (image/HTML) */
    BGL_BLOCK_TYPE_INFO = 3,        /**< Info block (Type 3 metadata item) */
    BGL_BLOCK_TYPE_ENTRIES_START = 6, /**< Entries section start marker */
    BGL_BLOCK_TYPE_EOF = 4,         /**< End of file */
    BGL_BLOCK_TYPE_ENTRY_TYPE7 = 7,  /**< Entry type 7 */
    BGL_BLOCK_TYPE_ENTRY_TYPE10 = 10, /**< Entry type 10 */
    BGL_BLOCK_TYPE_ENTRY_TYPE11 = 11, /**< Entry type 11 */
    BGL_BLOCK_TYPE_ENTRY_TYPE13 = 13, /**< Entry type 13 */
} bgl_block_type;

/**
 * @brief BGL block structure
 */
typedef struct {
    uint8_t type;           /**< Block type */
    uint8_t *data;          /**< Block data */
    size_t data_size;        /**< Data length */
    size_t offset;          /**< Offset in gzip stream */
} bgl_block;

/**
 * @brief BGL file header information
 */
typedef struct {
    uint32_t signature;      /**< File signature */
    uint32_t gzip_offset;    /**< Gzip data offset */
    size_t gzip_size;        /**< Gzip data size */
} bgl_header;

/**
 * @brief BGL reader (full definition, opaque to external code)
 */
struct bgl_reader {
    // File information
    char *file_path;         /**< File path */
    FILE *fp;                /**< File handle */
    size_t file_size;        /**< File size */

    // BGL file header
    bgl_header header;

    // Gzip decompression stream
    gzFile gzf;              /**< zlib gzip file handle */
    size_t gzip_offset;      /**< Gzip data offset */
    size_t entries_start_offset;    /**< Offset in gzip stream where entries section starts */
    size_t resources_start_offset;  /**< Offset in gzip stream where first resource starts */

    // Dictionary info (collection of all Type 3 info blocks)
    bgl_info info;

    // Encoding information (all point to static memory, no need to free)
    const char *source_encoding;   /**< Source language encoding */
    const char *target_encoding;   /**< Target language encoding */
    const char *default_encoding;  /**< Default charset (from Type 0 block, code 8) */

    // Info loading state
    bool info_loaded;        /**< Whether info has been loaded */

    // Counters (actual count from scanning)
    int entry_count;         /**< Actual number of entry blocks */
    int resource_count;      /**< Actual number of resource blocks */
};

/**
 * @brief BGL entry iterator (full definition, opaque to external code)
 */
struct bgl_entry_iterator {
    bgl_reader *reader;           /**< Associated parser */
    bool finished;                /**< Whether iteration is complete */
    bgl_entry current;            /**< Current entry (owned by iterator) */
};

/**
 * @brief BGL resource iterator (full definition, opaque to external code)
 */
struct bgl_resource_iterator {
    bgl_reader *reader;           /**< Associated parser */
    bool finished;                /**< Whether iteration is complete */
    bgl_resource current;         /**< Current resource (owned by iterator) */
};

// ============================================================
// Forward Declarations
// ============================================================

static bgl_status bgl_read_block(bgl_reader *reader, bgl_block **out_block);
static void bgl_free_block(bgl_block *block);
static int bgl_load_info(bgl_reader *reader);
static int bgl_seek_to_entries(bgl_reader *reader);
static int bgl_seek_to_resources(bgl_reader *reader);
static int bgl_parse_entry(bgl_reader *reader, const bgl_block *block, bgl_entry *entry);
static void bgl_free_entry(bgl_entry *entry);
static int bgl_parse_entry_type11(bgl_reader *reader, const bgl_block *block, bgl_entry *entry);
static int bgl_parse_resource(bgl_reader *reader, const bgl_block *block, bgl_resource *resource);
static void bgl_free_resource(bgl_resource *resource);
static int bgl_detect_encoding(bgl_reader *reader);
static int bgl_parse_type0(const uint8_t *data, size_t data_size, const char **default_charset_out);

/**
 * @brief Safe string duplication
 */
static char *safe_strdup(const char *src) {
    if (!src) return NULL;
    return bgl_strdup(src);
}

/**
 * @brief Safe memory allocation
 */
static void *safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

// ============================================================
// Lifecycle Management
// ============================================================

bgl_reader *bgl_reader_open(const char *file_path) {
    if (!file_path) {
        return NULL;
    }

    // 1. Create parser object
    bgl_reader *reader = (bgl_reader *)calloc(1, sizeof(bgl_reader));
    if (!reader) {
        return NULL;
    }

    // 2. Open file
    reader->file_path = safe_strdup(file_path);
    if (!reader->file_path) {
        free(reader);
        return NULL;
    }

    reader->fp = fopen(file_path, "rb");
    if (!reader->fp) {
        free(reader->file_path);
        free(reader);
        return NULL;
    }

    // Get file size
    fseek(reader->fp, 0, SEEK_END);
    reader->file_size = ftell(reader->fp);
    fseek(reader->fp, 0, SEEK_SET);

    // Initialize fields
    reader->gzf = NULL;
    reader->source_encoding = NULL;
    reader->target_encoding = NULL;
    reader->default_encoding = NULL;
    reader->info_loaded = false;
    reader->entries_start_offset = (size_t)-1;
    reader->resources_start_offset = (size_t)-1;

    // 3. Read and verify file header
    uint8_t header[BGL_HEADER_SIZE];
    if (fread(header, 1, BGL_HEADER_SIZE, reader->fp) != BGL_HEADER_SIZE) {
        bgl_reader_close(reader);
        return NULL;
    }

    reader->header.signature = bgl_read_uint32_be(header);
    if (reader->header.signature != BGL_SIGNATURE_1 &&
        reader->header.signature != BGL_SIGNATURE_2) {
        bgl_reader_close(reader);
        return NULL;
    }

    reader->header.gzip_offset = bgl_read_uint16_be(header + 4);
    reader->gzip_offset = reader->header.gzip_offset;

    if (reader->header.gzip_offset < BGL_HEADER_SIZE ||
        reader->header.gzip_offset >= reader->file_size) {
        bgl_reader_close(reader);
        return NULL;
    }

    // 4. Open gzip stream
    // Need to flush file buffer, otherwise some NFS mounts may not update file position correctly
    fflush(reader->fp);

    /* Under Mac OS X the above technique don't set reopen position properly */
    int fn = BGL_DUP(BGL_FILENO(reader->fp));
    BGL_LSEEK(fn, reader->header.gzip_offset, SEEK_SET);
    reader->gzf = gzdopen(fn, "r");
    if (!reader->gzf) {
        bgl_reader_close(reader);
        return NULL;
    }

    // Note: Infos are lazy-loaded, not scanned here
    // To load info immediately, call bgl_load_info()

    return reader;
}

void bgl_reader_close(bgl_reader *reader) {
    if (!reader) {
        return;
    }

    // Close gzip stream
    if (reader->gzf) {
        gzclose(reader->gzf);
        reader->gzf = NULL;
    }

    // Close file
    if (reader->fp) {
        fclose(reader->fp);
        reader->fp = NULL;
    }

    // Release file path
    if (reader->file_path) {
        free(reader->file_path);
        reader->file_path = NULL;
    }

    // Release info
    bgl_free_info(&reader->info);

    // Note: encoding attributes point to static memory, no need to free
    // (source_encoding, target_encoding, default_encoding)

    // Release parser object
    free(reader);
}

// ============================================================
// Metadata Access
// ============================================================

int bgl_reader_prepare(bgl_reader *reader) {
    if (!reader) {
        return -1;
    }
    return bgl_load_info(reader);
}

static int bgl_load_info(bgl_reader *reader) {
    if (!reader || !reader->gzf) {
        return -1;
    }

    // Idempotent: if already loaded, return directly
    if (reader->info_loaded) {
        return 0;
    }

    // Reset counters (will be counted during scan)
    reader->entry_count = 0;
    reader->resource_count = 0;


    // Scan all blocks until EOF to collect all info blocks
    // Some BGL files have info blocks scattered throughout, even at the end
    bool found_first_entry = false;
    while (true) {
        bgl_block *block;
        bgl_status status = bgl_read_block(reader, &block);
        if (status != BGL_OK) {
            break;
        }

        // Record the position of the first entry block
        if (!found_first_entry) {
            if (block->type == BGL_BLOCK_TYPE_ENTRY ||
                block->type == BGL_BLOCK_TYPE_ENTRY_TYPE7 ||
                block->type == BGL_BLOCK_TYPE_ENTRY_TYPE10 ||
                block->type == BGL_BLOCK_TYPE_ENTRY_TYPE11 ||
                block->type == BGL_BLOCK_TYPE_ENTRY_TYPE13) {
                reader->entries_start_offset = block->offset;
                found_first_entry = true;
            }
        }

        // Count entry blocks
        if (block->type == BGL_BLOCK_TYPE_ENTRY ||
            block->type == BGL_BLOCK_TYPE_ENTRY_TYPE7 ||
            block->type == BGL_BLOCK_TYPE_ENTRY_TYPE10 ||
            block->type == BGL_BLOCK_TYPE_ENTRY_TYPE11 ||
            block->type == BGL_BLOCK_TYPE_ENTRY_TYPE13) {
            reader->entry_count++;
        }

        // Record the position of the first resource block
        if (reader->resources_start_offset == (size_t)-1) {
            if (block->type == BGL_BLOCK_TYPE_RESOURCE) {
                reader->resources_start_offset = block->offset;
            }
        }

        // Count resource blocks
        if (block->type == BGL_BLOCK_TYPE_RESOURCE) {
            reader->resource_count++;
        }

        if (block->type == BGL_BLOCK_TYPE_EXTENDED) {
            // Type 0 block: special metadata (default charset, etc.)
            const char *default_charset = NULL;
            bgl_parse_type0(block->data, block->data_size, &default_charset);
            if (default_charset) {
                reader->default_encoding = default_charset;
            }
        } else if (block->type == BGL_BLOCK_TYPE_INFO) {
            bgl_parse_info_field(block->data, block->data_size, &reader->info);
        }

        bgl_free_block(block);
    }

    // Warn if actual entry count differs from metadata
    if (reader->info.entry_count > 0 && reader->entry_count != reader->info.entry_count) {
        fprintf(stderr, "warning: entry count mismatch: metadata=%d, actual=%d\n",
                reader->info.entry_count, reader->entry_count);
    }

    // Detect encoding
    if (bgl_detect_encoding(reader) != 0) {
        reader->source_encoding = "CP1252";
        reader->target_encoding = "CP1252";
    }

    // Convert info text fields to UTF-8 using target encoding
    // This must be done after bgl_detect_encoding so we know target_charset
    bgl_convert_info_to_utf8(&reader->info, reader->target_encoding);

    reader->info_loaded = true;

    return 0;
}

const bgl_info *bgl_get_info(bgl_reader *reader) {
    if (!reader) {
        return NULL;
    }

    // Auto on-demand loading of info
    if (!reader->info_loaded) {
        if (bgl_load_info(reader) != 0) {
            return NULL;
        }
    }

    return &reader->info;
}

int bgl_get_entry_count(bgl_reader *reader) {
    if (!reader) {
        return -1;
    }
    if (!reader->info_loaded) {
        if (bgl_load_info(reader) != 0) {
            return -1;
        }
    }
    return reader->entry_count;
}

int bgl_get_resource_count(bgl_reader *reader) {
    if (!reader) {
        return -1;
    }
    if (!reader->info_loaded) {
        if (bgl_load_info(reader) != 0) {
            return -1;
        }
    }
    return reader->resource_count;
}

int bgl_cleanup_entry(bgl_entry *entry) {
    if (!entry) {
        return -1;
    }

    // Clean word
    if (entry->word) {
        bgl_strip_dollar_indexes(&entry->word);
        bgl_decode_html_entities(&entry->word, BGL_HTML_STRIP);
        bgl_remove_control_chars(&entry->word);
        bgl_remove_newlines(&entry->word);
        bgl_strip(&entry->word);
    }

    // Clean alternates
    for (int i = 0; i < entry->alternate_count; i++) {
        char **alt = &entry->alternates[i];
        if (*alt) {
            bgl_strip_dollar_indexes(alt);
            bgl_strip_slash_alt_key(alt);
            bgl_decode_html_entities(alt, BGL_HTML_STRIP);
            bgl_remove_control_chars(alt);
            bgl_remove_newlines(alt);
            bgl_strip(alt);
        }
    }

    // Clean definition
    if (entry->def.body) {
        bgl_fix_img_links(&entry->def.body);
        bgl_decode_html_entities(&entry->def.body, BGL_HTML_KEEP_TAGS);
        bgl_remove_control_chars(&entry->def.body);
        bgl_normalize_newlines(&entry->def.body);
        bgl_strip(&entry->def.body);
    }
    if (entry->def.title) {
        bgl_decode_html_entities(&entry->def.title, BGL_HTML_KEEP_TAGS);
        bgl_remove_control_chars(&entry->def.title);
    }
    if (entry->def.title_trans) {
        bgl_decode_html_entities(&entry->def.title_trans, BGL_HTML_KEEP_TAGS);
        bgl_remove_control_chars(&entry->def.title_trans);
    }
    if (entry->def.transcription) {
        bgl_decode_html_entities(&entry->def.transcription, BGL_HTML_KEEP_TAGS);
        bgl_remove_control_chars(&entry->def.transcription);
    }

    return 0;
}

static int bgl_seek_to_entries(bgl_reader *reader) {
    if (!reader || !reader->gzf) {
        return -1;
    }

    // Check if file has entries
    if (reader->entries_start_offset == (size_t)-1) {
        return -1;  // No entries
    }

    // Ensure info is loaded first (this also finds entries_start_offset)
    if (!reader->info_loaded) {
        if (bgl_load_info(reader) != 0) {
            return -1;
        }
    }

    // Rewind to start of gzip stream
    gzclearerr(reader->gzf);
    int ret = gzrewind(reader->gzf);
    if (ret != 0) {
        return -1;
    }

    // Seek to entries start position
    if (gzseek(reader->gzf, (z_off_t)reader->entries_start_offset, SEEK_SET) != (z_off_t)reader->entries_start_offset) {
        return -1;
    }

    return 0;
}

static int bgl_seek_to_resources(bgl_reader *reader) {
    if (!reader || !reader->gzf) {
        return -1;
    }

    // Check if file has resources
    if (reader->resources_start_offset == (size_t)-1) {
        return -1;  // No resources
    }

    // Ensure info is loaded first
    if (!reader->info_loaded) {
        if (bgl_load_info(reader) != 0) {
            return -1;
        }
    }

    // Rewind and seek to resources start position
    gzclearerr(reader->gzf);
    if (gzrewind(reader->gzf) != 0) {
        return -1;
    }

    if (gzseek(reader->gzf, (z_off_t)reader->resources_start_offset, SEEK_SET) !=
        (z_off_t)reader->resources_start_offset) {
        return -1;
    }

    return 0;
}

// ============================================================
// Block Reading
// ============================================================

static bgl_status bgl_read_block(bgl_reader *reader, bgl_block **out_block) {
    if (!reader || !reader->gzf || !out_block) {
        return BGL_ERR_INVALID_PARAM;
    }
    *out_block = NULL;

    // Allocate block structure
    bgl_block *block = (bgl_block *)calloc(1, sizeof(bgl_block));
    if (!block) {
        return BGL_ERR_MEMORY;
    }

    // Record block start position (before reading any bytes, consistent with Python)
    block->offset = gztell(reader->gzf);

    // Read first byte of record header
    uint8_t first_byte;
    int bytes_read = gzread(reader->gzf, &first_byte, 1);
    if (bytes_read != 1) {
        bgl_free_block(block);
        int err;
        gzerror(reader->gzf, &err);
        return (err == Z_OK || err == Z_STREAM_END) ? BGL_END : BGL_ERR_IO;
    }

    // Parse type and length encoding
    block->type = first_byte & 0x0F;
    uint8_t length_code = first_byte >> 4;
    size_t data_size;

    if (length_code >= 4) {
        // Length is directly encoded in the first byte
        data_size = length_code - 4;
    } else {
        // Need to read additional length bytes (length_code + 1 bytes)
        size_t extra_bytes = length_code + 1;
        uint8_t extra_len[4];  // Maximum 4 bytes

        // extra_bytes maximum value is 4 (length_code=3), safe to cast to unsigned int
        bytes_read = gzread(reader->gzf, extra_len, (unsigned int)extra_bytes);
        if (bytes_read != (int)extra_bytes) {
            bgl_free_block(block);
            return BGL_ERR_IO;
        }

        // Parse length (big-endian)
        data_size = 0;
        for (size_t i = 0; i < extra_bytes; i++) {
            data_size = (data_size << 8) | extra_len[i];
        }
    }

    // Allocate and read data
    if (data_size > 0) {
        block->data = (uint8_t *)malloc(data_size + 1);  // +1 for null terminator
        if (!block->data) {
            bgl_free_block(block);
            return BGL_ERR_MEMORY;
        }

        // data_size is actual block size, theoretically not exceeding UINT_MAX
        bytes_read = gzread(reader->gzf, block->data, (unsigned int)data_size);
        if (bytes_read != (int)data_size) {
            bgl_free_block(block);
            return BGL_ERR_IO;
        }

        block->data[data_size] = '\0';  // null terminate
        block->data_size = data_size;
    }

    *out_block = block;
    return BGL_OK;
}

static void bgl_free_block(bgl_block *block) {
    if (!block) {
        return;
    }

    if (block->data) {
        free(block->data);
        block->data = NULL;
    }
    block->data_size = 0;
    free(block);
}

// ============================================================
// Entry Parsing
// ============================================================

static int bgl_parse_entry(bgl_reader *reader, const bgl_block *block, bgl_entry *entry) {
    if (!reader || !block || !entry) {
        return -1;
    }

    memset(entry, 0, sizeof(bgl_entry));

    const uint8_t *data = block->data;
    size_t pos = 0;

    // ============================================================
    // Step 1: Read word [1 byte length]
    // ============================================================
    if (pos + 1 > block->data_size) {
        return -1;
    }
    uint8_t word_len = data[pos];
    pos++;

    if (pos + word_len > block->data_size) {
        return -1;
    }

    // Decode word (using source encoding to UTF-8)
    entry->word = bgl_decode_text(data + pos, word_len, reader->source_encoding);
    if (entry->word == NULL) {
        // If decoding fails, use raw bytes
        entry->word = (char *)malloc(word_len + 1);
        if (!entry->word) {
            return -1;
        }
        memcpy(entry->word, data + pos, word_len);
        entry->word[word_len] = '\0';
    }

    pos += word_len;

    // ============================================================
    // Step 2: Read definition [2 bytes length]
    // ============================================================
    if (pos + 2 > block->data_size) {
        free(entry->word);
        return -1;
    }
    uint16_t defi_len = bgl_read_uint16_be(data + pos);
    pos += 2;

    if (pos + defi_len > block->data_size) {
        free(entry->word);
        return -1;
    }

    // Parse definition fields (extract title, title_trans, POS, etc.)
    if (bgl_parse_definition(data + pos, defi_len,
                                       reader->source_encoding,
                                       reader->target_encoding,
                                       reader->default_encoding,
                                       &entry->def) != 0) {
        bgl_free_entry(entry);
        return -1;
    }
    pos += defi_len;

    // ============================================================
    // Step 3: Read alternates (until EOF, 1 byte length each)
    // ============================================================
    // Dynamic array for alternates (grows as needed)
    size_t alts_capacity = 8;  // Initial capacity (covers most cases)
    entry->alternates = (char **)calloc(alts_capacity, sizeof(char *));
    if (!entry->alternates) {
        bgl_free_entry(entry);
        return -1;
    }

    while (pos < block->data_size) {
        if (pos + 1 > block->data_size) {
            break;
        }

        uint8_t alt_len = data[pos];
        pos++;

        if (pos + alt_len > block->data_size) {
            break;
        }

        // Skip empty strings
        if (alt_len == 0) {
            break;
        }

        // Expand array if needed
        if (entry->alternate_count >= (int)alts_capacity) {
            size_t new_capacity = alts_capacity * 2;
            char **new_alts = (char **)realloc(entry->alternates, new_capacity * sizeof(char *));
            if (!new_alts) {
                // Keep existing data, just stop adding more
                break;
            }
            entry->alternates = new_alts;
            alts_capacity = new_capacity;
        }

        // Decode alternate word
        char *alt_word = NULL;
        alt_word = bgl_decode_text(data + pos, alt_len, reader->source_encoding);
        if (alt_word == NULL) {
            // If decoding fails, use raw bytes
            alt_word = (char *)malloc(alt_len + 1);
            if (!alt_word) {
                bgl_free_entry(entry);
                return -1;
            }
            memcpy(alt_word, data + pos, alt_len);
            alt_word[alt_len] = '\0';
        }

        entry->alternates[entry->alternate_count++] = alt_word;
        pos += alt_len;
    }

    return 0;
}

// ============================================================
// Type 11 Entry Parsing (separate function due to format differences)
// ============================================================

/**
 * @brief Parse Type 11 entry from block
 *
 * Type 11 format differs significantly from standard entry format:
 * - Word: [1 byte flag] + [4 bytes length] + [word]
 * - Alternates: [4 bytes count] + N * ([4 bytes length] + [alternate])
 * - Definition: [4 bytes length] + [definition data]
 */
static int bgl_parse_entry_type11(bgl_reader *reader, const bgl_block *block, bgl_entry *entry) {
    if (!reader || !block || !entry) {
        return -1;
    }

    memset(entry, 0, sizeof(bgl_entry));

    const uint8_t *data = block->data;
    size_t pos = 0;

    // ============================================================
    // Step 1: Read word [1 byte flag] + [4 bytes length]
    // ============================================================
    if (pos + 5 > block->data_size) {
        return -1;
    }
    pos += 1;  // Skip flag byte
    uint32_t word_len = bgl_read_uint32_be(data + pos);
    pos += 4;

    if (pos + word_len > block->data_size) {
        return -1;
    }

    // Decode word (using source encoding to UTF-8)
    entry->word = bgl_decode_text(data + pos, word_len, reader->source_encoding);
    if (entry->word == NULL) {
        // If decoding fails, use raw bytes
        entry->word = (char *)malloc(word_len + 1);
        if (!entry->word) {
            return -1;
        }
        memcpy(entry->word, data + pos, word_len);
        entry->word[word_len] = '\0';
    }

    pos += word_len;

    // ============================================================
    // Step 2: Read alternates [4 bytes count] + N * ([4 bytes length] + [alternate])
    // ============================================================
    if (pos + 4 > block->data_size) {
        free(entry->word);
        return -1;
    }
    uint32_t alts_count = bgl_read_uint32_be(data + pos);
    pos += 4;

    // Allocate alternates array
    entry->alternates = (char **)calloc(alts_count, sizeof(char *));
    if (!entry->alternates) {
        free(entry->word);
        return -1;
    }

    // Read each alternate
    for (uint32_t i = 0; i < alts_count; i++) {
        if (pos + 4 > block->data_size) {
            break;
        }
        uint32_t alt_len = bgl_read_uint32_be(data + pos);
        pos += 4;

        if (alt_len == 0) {
            break;
        }

        if (pos + alt_len > block->data_size) {
            break;
        }

        // Decode alternate word
        char *alt = NULL;
        alt = bgl_decode_text(data + pos, alt_len, reader->source_encoding);
        if (alt == NULL) {
            // If decoding fails, use raw bytes
            alt = (char *)malloc(alt_len + 1);
            if (!alt) {
                bgl_free_entry(entry);
                return -1;
            }
            memcpy(alt, data + pos, alt_len);
            alt[alt_len] = '\0';
        }

        entry->alternates[entry->alternate_count++] = alt;
        pos += alt_len;
    }

    // ============================================================
    // Step 3: Read definition [4 bytes length]
    // ============================================================
    if (pos + 4 > block->data_size) {
        bgl_free_entry(entry);
        return -1;
    }
    uint32_t defi_len = bgl_read_uint32_be(data + pos);
    pos += 4;

    if (pos + defi_len > block->data_size) {
        bgl_free_entry(entry);
        return -1;
    }

    // Parse definition fields (extract title, title_trans, POS, etc.)
    if (bgl_parse_definition(data + pos, defi_len,
                                       reader->source_encoding,
                                       reader->target_encoding,
                                       reader->default_encoding,
                                       &entry->def) != 0) {
        bgl_free_entry(entry);
        return -1;
    }

    return 0;
}

static void bgl_free_entry(bgl_entry *entry) {
    if (!entry) {
        return;
    }

    if (entry->word) {
        free(entry->word);
        entry->word = NULL;
    }

    // Free definition fields (including definition, title, title_trans, transcription, field_1a)
    bgl_free_definition(&entry->def);

    if (entry->alternates) {
        for (int i = 0; i < entry->alternate_count; i++) {
            if (entry->alternates[i]) {
                free(entry->alternates[i]);
            }
        }
        free(entry->alternates);
        entry->alternates = NULL;
    }

    entry->alternate_count = 0;
}

// ============================================================
// Resource Parsing
// ============================================================

static int bgl_parse_resource(bgl_reader *reader, const bgl_block *block, bgl_resource *resource) {
    if (!reader || !block || !resource) {
        return -1;
    }

    memset(resource, 0, sizeof(bgl_resource));

    const uint8_t *data = block->data;
    size_t pos = 0;

    // Read resource name
    if (pos >= block->data_size) {
        return -1;
    }

    uint8_t name_len = data[pos];
    pos++;

    if (pos + name_len > block->data_size) {
        return -1;
    }

    // Allocate resource name
    resource->name = (char *)malloc(name_len + 1);
    if (!resource->name) {
        return -1;
    }

    memcpy(resource->name, data + pos, name_len);
    resource->name[name_len] = '\0';
    pos += name_len;

    // Remaining data is resource content
    resource->data_size = block->data_size - pos;
    if (resource->data_size > 0) {
        resource->data = (uint8_t *)malloc(resource->data_size);
        if (!resource->data) {
            free(resource->name);
            resource->name = NULL;
            return -1;
        }

        memcpy(resource->data, data + pos, resource->data_size);
    }

    return 0;
}

static void bgl_free_resource(bgl_resource *resource) {
    if (!resource) {
        return;
    }

    if (resource->name) {
        free(resource->name);
        resource->name = NULL;
    }

    if (resource->data) {
        free(resource->data);
        resource->data = NULL;
    }

    resource->data_size = 0;
}

// ============================================================
// Entry Iterator
// ============================================================

bgl_entry_iterator *bgl_entry_iterator_create(bgl_reader *reader) {
    if (!reader || !reader->gzf) {
        return NULL;
    }

    // Seek to entries section start
    if (bgl_seek_to_entries(reader) != 0) {
        return NULL;
    }

    bgl_entry_iterator *iter = (bgl_entry_iterator *)calloc(1, sizeof(bgl_entry_iterator));
    if (!iter) {
        return NULL;
    }

    iter->reader = reader;
    iter->finished = false;

    return iter;
}

bgl_status bgl_entry_iterator_next(bgl_entry_iterator *iter, const bgl_entry **out_entry) {
    if (!iter || !iter->reader || !out_entry) {
        return BGL_ERR_INVALID_PARAM;
    }
    *out_entry = NULL;

    if (iter->finished) {
        return BGL_END;
    }

    // Free previous entry data
    bgl_free_entry(&iter->current);
    memset(&iter->current, 0, sizeof(bgl_entry));

    while (true) {
        bgl_block *block;
        bgl_status status = bgl_read_block(iter->reader, &block);
        if (status != BGL_OK) {
            iter->finished = true;
            return status;
        }

        // Only process entry types
        if (block->type == BGL_BLOCK_TYPE_ENTRY ||
            block->type == BGL_BLOCK_TYPE_ENTRY_TYPE7 ||
            block->type == BGL_BLOCK_TYPE_ENTRY_TYPE10 ||
            block->type == BGL_BLOCK_TYPE_ENTRY_TYPE13) {

            int ret = bgl_parse_entry(iter->reader, block, &iter->current);
            bgl_free_block(block);

            if (ret == 0) {
                *out_entry = &iter->current;
                return BGL_OK;
            }
            return BGL_ERR_FORMAT;
        } else if (block->type == BGL_BLOCK_TYPE_ENTRY_TYPE11) {
            int ret = bgl_parse_entry_type11(iter->reader, block, &iter->current);
            bgl_free_block(block);

            if (ret == 0) {
                *out_entry = &iter->current;
                return BGL_OK;
            }
            return BGL_ERR_FORMAT;
        } else {
            bgl_free_block(block);
        }
    }
}

void bgl_entry_iterator_free(bgl_entry_iterator *iter) {
    if (!iter) {
        return;
    }

    bgl_free_entry(&iter->current);
    free(iter);
}

// ============================================================
// Resource Iterator
// ============================================================

bgl_resource_iterator *bgl_resource_iterator_create(bgl_reader *reader) {
    if (!reader || !reader->gzf) {
        return NULL;
    }

    // Seek to resources section start
    if (bgl_seek_to_resources(reader) != 0) {
        return NULL;
    }

    bgl_resource_iterator *iter = (bgl_resource_iterator *)calloc(1, sizeof(bgl_resource_iterator));
    if (!iter) {
        return NULL;
    }

    iter->reader = reader;
    iter->finished = false;

    return iter;
}

bgl_status bgl_resource_iterator_next(bgl_resource_iterator *iter, const bgl_resource **out_resource) {
    if (!iter || !iter->reader || !out_resource) {
        return BGL_ERR_INVALID_PARAM;
    }
    *out_resource = NULL;

    if (iter->finished) {
        return BGL_END;
    }

    // Free previous resource data
    bgl_free_resource(&iter->current);
    memset(&iter->current, 0, sizeof(bgl_resource));

    while (true) {
        bgl_block *block;
        bgl_status status = bgl_read_block(iter->reader, &block);
        if (status != BGL_OK) {
            iter->finished = true;
            return status;
        }

        if (block->type == BGL_BLOCK_TYPE_RESOURCE) {
            int ret = bgl_parse_resource(iter->reader, block, &iter->current);
            bgl_free_block(block);

            if (ret == 0) {
                *out_resource = &iter->current;
                return BGL_OK;
            }
            return BGL_ERR_FORMAT;
        } else {
            bgl_free_block(block);
        }
    }

    iter->finished = true;
    return BGL_END;
}

void bgl_resource_iterator_free(bgl_resource_iterator *iter) {
    if (!iter) {
        return;
    }

    bgl_free_resource(&iter->current);
    free(iter);
}

// ============================================================
// Type 0 Block Parsing
// ============================================================

/**
 * @brief Parse Type 0 block (extended metadata block)
 *
 * Type 0 block codes:
 * - code 2: A number close to entry count (but not always equal)
 * - code 8: Default charset (used when source/target charset are not specified)
 */
static int bgl_parse_type0(const uint8_t *data, size_t data_size, const char **default_charset_out) {
    if (!data || data_size < 1 || !default_charset_out) {
        return -1;
    }

    uint8_t code = data[0];

    switch (code) {
        case 2:
            // A number close to entry count (but not always equal)
            break;

        case 8:
            // Default charset
            if (data_size >= 2) {
                int charset_code = data[1];
                const char *encoding = bgl_charset_by_code(charset_code);
                if (encoding) {
                    *default_charset_out = encoding;
                }
            }
            break;

        default:
            break;
    }

    return 0;
}

// ============================================================
// Encoding Detection
// ============================================================

static int bgl_detect_encoding(bgl_reader *reader) {
    if (!reader) {
        return -1;
    }

    // If UTF-8 mode is already set
    if (reader->info.utf8_mode) {
        reader->source_encoding = "UTF-8";
        reader->target_encoding = "UTF-8";
        return 0;
    }

    // Try to get encoding from metadata
    const char *source_enc = NULL;
    const char *target_enc = NULL;

    // Get encoding from charset
    if (reader->info.source_charset) {
        source_enc = reader->info.source_charset;
    }
    if (reader->info.target_charset) {
        target_enc = reader->info.target_charset;
    }

    // Get encoding from language
    if (!source_enc && reader->info.source_lang) {
        const bgl_language *lang = bgl_language_by_name(reader->info.source_lang);
        if (lang) {
            source_enc = lang->encoding;
        }
    }
    if (!target_enc && reader->info.target_lang) {
        const bgl_language *lang = bgl_language_by_name(reader->info.target_lang);
        if (lang) {
            target_enc = lang->encoding;
        }
    }

    // Use default charset from Type 0 block if available
    if (!source_enc && reader->default_encoding) {
        source_enc = reader->default_encoding;
    }
    if (!target_enc && reader->default_encoding) {
        target_enc = reader->default_encoding;
    }

    // Use fallback encoding
    if (!source_enc) {
        source_enc = "CP1252";
    }
    if (!target_enc) {
        target_enc = "CP1252";
    }

    reader->source_encoding = source_enc;
    reader->target_encoding = target_enc;

    return (reader->source_encoding && reader->target_encoding) ? 0 : -1;
}
