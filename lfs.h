/*
 * The little filesystem
 *
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef LFS_H
#define LFS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif


/// Version info ///

// Software library version
// Major (top-nibble), incremented on backwards incompatible changes
// Minor (bottom-nibble), incremented on feature additions
#define LFS_VERSION 0x00020000
#define LFS_VERSION_MAJOR (0xffff & (LFS_VERSION >> 16))
#define LFS_VERSION_MINOR (0xffff & (LFS_VERSION >>  0))

// Version of On-disk data structures
// Major (top-nibble), incremented on backwards incompatible changes
// Minor (bottom-nibble), incremented on feature additions
#define LFS_DISK_VERSION 0x00020000
#define LFS_DISK_VERSION_MAJOR (0xffff & (LFS_DISK_VERSION >> 16))
#define LFS_DISK_VERSION_MINOR (0xffff & (LFS_DISK_VERSION >>  0))


/// Definitions ///

// Type definitions
typedef uint32_t lfs_size_t;
typedef uint32_t lfs_off_t;

typedef int32_t  lfs_ssize_t;
typedef int32_t  lfs_soff_t;

typedef uint32_t lfs_block_t;

// Maximum size of all attributes per file in bytes, may be redefined but a
// a smaller LFS_ATTR_MAX has no benefit. Stored in 12-bits and limited
// to <= 0xfff. Stored in superblock and must be respected by other
// littlefs drivers.
#ifndef LFS_ATTR_MAX
#define LFS_ATTR_MAX 0x1ffe
#endif

// Maximum name size in bytes, may be redefined to reduce the size of the
// info struct. Limited to <= LFS_ATTR_MAX. Stored in superblock and must
// be respected by other littlefs drivers.
#ifndef LFS_NAME_MAX
#define LFS_NAME_MAX 0xff
#endif

// Maximum inline file size in bytes. Large inline files require a larger
// cache size, but if a file can be inline it does not need its own data
// block. Limited to <= LFS_ATTR_MAX and <= cache_size. Stored in superblock
// and must be respected by other littlefs drivers.
#ifndef LFS_INLINE_MAX
#define LFS_INLINE_MAX 0x1ffe
#endif

// Possible error codes, these are negative to allow
// valid positive return values
enum lfs_error {
    LFS_ERR_OK          = 0,    // No error
    LFS_ERR_IO          = -5,   // Error during device operation
    LFS_ERR_CORRUPT     = -84,  // Corrupted
    LFS_ERR_NOENT       = -2,   // No directory entry
    LFS_ERR_EXIST       = -17,  // Entry already exists
    LFS_ERR_NOTDIR      = -20,  // Entry is not a dir
    LFS_ERR_ISDIR       = -21,  // Entry is a dir
    LFS_ERR_NOTEMPTY    = -39,  // Dir is not empty
    LFS_ERR_BADF        = -9,   // Bad file number
    LFS_ERR_INVAL       = -22,  // Invalid parameter
    LFS_ERR_NOSPC       = -28,  // No space left on device
    LFS_ERR_NOMEM       = -12,  // No more memory available
    LFS_ERR_NOATTR      = -61,  // No data/attr available
    LFS_ERR_NAMETOOLONG = -36,  // File name too long
};

// File types
enum lfs_type {
    // file types
    LFS_TYPE_REG            = 0x002,
    LFS_TYPE_DIR            = 0x003,

    // internally used types
    LFS_TYPE_USER           = 0x100,
    LFS_TYPE_NAME           = 0x000,
    LFS_TYPE_DELETE         = 0x020,
    LFS_TYPE_STRUCT         = 0x040,
    LFS_TYPE_TAIL           = 0x080,
    LFS_TYPE_SOFTTAIL       = 0x080,
    LFS_TYPE_HARDTAIL       = 0x081,
    LFS_TYPE_CRC            = 0x0a0,
    LFS_TYPE_SUPERBLOCK     = 0x001,
    LFS_TYPE_GLOBALS        = 0x0e0,

    LFS_TYPE_DIRSTRUCT      = 0x040,
    LFS_TYPE_INLINESTRUCT   = 0x041,
    LFS_TYPE_CTZSTRUCT      = 0x042,

    // internal chip sources
    LFS_FROM_MEM            = 0x000,
    LFS_FROM_DISK           = 0x200,
    LFS_FROM_MOVE           = 0x0c1,
    LFS_FROM_USERATTRS      = 0x0c2,
};

// File open flags
enum lfs_open_flags {
    // open flags
    LFS_O_RDONLY = 1,         // Open a file as read only
    LFS_O_WRONLY = 2,         // Open a file as write only
    LFS_O_RDWR   = 3,         // Open a file as read and write
    LFS_O_CREAT  = 0x0100,    // Create a file if it does not exist
    LFS_O_EXCL   = 0x0200,    // Fail if a file already exists
    LFS_O_TRUNC  = 0x0400,    // Truncate the existing file to zero size
    LFS_O_APPEND = 0x0800,    // Move to end of file on every write

    // internally used flags
    LFS_F_DIRTY   = 0x010000, // File does not match storage
    LFS_F_WRITING = 0x020000, // File has been written since last flush
    LFS_F_READING = 0x040000, // File has been read since last flush
    LFS_F_ERRED   = 0x080000, // An error occured during write
    LFS_F_INLINE  = 0x100000, // Currently inlined in directory entry
};

// File seek flags
enum lfs_whence_flags {
    LFS_SEEK_SET = 0,   // Seek relative to an absolute position
    LFS_SEEK_CUR = 1,   // Seek relative to the current file position
    LFS_SEEK_END = 2,   // Seek relative to the end of the file
};


// Configuration provided during initialization of the littlefs
struct lfs_config {
    // Opaque user provided context that can be used to pass
    // information to the block device operations
    void *context;

    // Read a region in a block. Negative error codes are propogated
    // to the user.
    int (*read)(const struct lfs_config *c, lfs_block_t block,
            lfs_off_t off, void *buffer, lfs_size_t size);

    // Program a region in a block. The block must have previously
    // been erased. Negative error codes are propogated to the user.
    // May return LFS_ERR_CORRUPT if the block should be considered bad.
    int (*prog)(const struct lfs_config *c, lfs_block_t block,
            lfs_off_t off, const void *buffer, lfs_size_t size);

    // Erase a block. A block must be erased before being programmed.
    // The state of an erased block is undefined. Negative error codes
    // are propogated to the user.
    // May return LFS_ERR_CORRUPT if the block should be considered bad.
    int (*erase)(const struct lfs_config *c, lfs_block_t block);

    // Sync the state of the underlying block device. Negative error codes
    // are propogated to the user.
    int (*sync)(const struct lfs_config *c);

    // Minimum size of a block read. All read operations will be a
    // multiple of this value.
    lfs_size_t read_size;

    // Minimum size of a block program. All program operations will be a
    // multiple of this value.
    lfs_size_t prog_size;

    // Size of an erasable block. This does not impact ram consumption and
    // may be larger than the physical erase size. However, this should be
    // kept small as each file currently takes up an entire block.
    // Must be a multiple of the read, program, and cache sizes.
    lfs_size_t block_size;

    // Number of erasable blocks on the device.
    lfs_size_t block_count;

    // Number of erase cycles before we should move data to another block.
    // May be zero to never move data, in which case no block-level
    // wear-leveling is performed.
    uint32_t block_cycles;

    // Size of block caches. Each cache buffers a portion of a block in RAM.
    // This determines the size of the read cache, the program cache, and a
    // cache per file. Larger caches can improve performance by storing more
    // data. Must be a multiple of the read and program sizes.
    lfs_size_t cache_size;

    // Number of blocks to lookahead during block allocation. A larger
    // lookahead reduces the number of passes required to allocate a block.
    // The lookahead buffer requires only 1 bit per block so it can be quite
    // large with little ram impact. Should be a multiple of 32.
    lfs_size_t lookahead;

    // Optional, statically allocated read buffer. Must be read sized.
    void *read_buffer;

    // Optional, statically allocated program buffer. Must be program sized.
    void *prog_buffer;

    // Optional, statically allocated lookahead buffer. Must be 1 bit per
    // lookahead block.
    void *lookahead_buffer;

    // Optional upper limit on file attributes in bytes. No downside for larger
    // attributes size but must be less than LFS_ATTR_MAX. Defaults to
    // LFS_ATTR_MAX when zero.Stored in superblock and must be respected by
    // other littlefs drivers.
    lfs_size_t attr_max;

    // Optional upper limit on length of file names in bytes. No downside for
    // larger names except the size of the info struct which is controlled by
    // the LFS_NAME_MAX define. Defaults to LFS_NAME_MAX when zero. Stored in
    // superblock and must be respected by other littlefs drivers.
    lfs_size_t name_max;

    // Optional upper limit on inlined files in bytes. Large inline files
    // require a larger cache size, but if a file can be inlined it does not
    // need its own data block. Must be smaller than cache_size and less than
    // LFS_INLINE_MAX. Defaults to min(LFS_INLINE_MAX, read_size) when zero.
    // Stored in superblock and must be respected by other littlefs drivers.
    lfs_size_t inline_max;
};

// File info structure
struct lfs_info {
    // Type of the file, either LFS_TYPE_REG or LFS_TYPE_DIR
    uint8_t type;

    // Size of the file, only valid for REG files
    lfs_size_t size;

    // Name of the file stored as a null-terminated string
    char name[LFS_NAME_MAX+1];
};

// Custom attribute structure
struct lfs_attr {
    // 8-bit type of attribute, provided by user and used to
    // identify the attribute
    uint8_t type;

    // Pointer to buffer containing the attribute
    void *buffer;

    // Size of attribute in bytes, limited to LFS_ATTR_MAX
    lfs_size_t size;

    // Pointer to next attribute in linked list
    struct lfs_attr *next;
};

// Optional configuration provided during lfs_file_opencfg
struct lfs_file_config {
    // Optional, statically allocated buffer for files. Must be program sized.
    // If NULL, malloc will be used by default.
    void *buffer;

    // Optional, linked list of custom attributes related to the file. If the
    // file is opened with read access, the attributes will be read from
    // during the open call. If the file is opened with write access, the
    // attributes will be written to disk every file sync or close. This
    // write occurs atomically with update to the file's contents.
    //
    // Custom attributes are uniquely identified by an 8-bit type and limited
    // to LFS_ATTR_MAX bytes. When read, if the stored attribute is smaller
    // than the buffer, it will be padded with zeros. If the stored attribute
    // is larger, then it will be silently truncated. If the attribute is not
    // found, it will be created implicitly.
    struct lfs_attr *attrs;
};


/// internal littlefs data structures ///
typedef struct lfs_cache {
    lfs_block_t block;
    lfs_off_t off;
    lfs_size_t size;
    uint8_t *buffer;
} lfs_cache_t;

typedef struct lfs_mdir {
    lfs_block_t pair[2];
    uint32_t rev;
    uint32_t etag;
    lfs_off_t off;
    uint16_t count;
    bool erased;
    bool split;
    lfs_block_t tail[2];
} lfs_mdir_t;

// littlefs directory type
typedef struct lfs_dir {
    struct lfs_dir *next;
    uint16_t id;
    uint8_t type;
    lfs_mdir_t m;

    lfs_off_t pos;
    lfs_block_t head[2];
} lfs_dir_t;

// littlefs file type
typedef struct lfs_file {
    struct lfs_file *next;
    uint16_t id;
    uint8_t type;
    lfs_mdir_t m;

    struct lfs_ctz {
        lfs_block_t head;
        lfs_size_t size;
    } ctz;

    uint32_t flags;
    lfs_off_t pos;
    lfs_block_t block;
    lfs_off_t off;
    lfs_cache_t cache;

    const struct lfs_file_config *cfg;
} lfs_file_t;

typedef struct lfs_superblock {
    char magic[8];
    uint32_t version;

    lfs_size_t block_size;
    lfs_size_t block_count;

    lfs_size_t attr_max;
    lfs_size_t name_max;
    lfs_size_t inline_max;
} lfs_superblock_t;

// The littlefs filesystem type
typedef struct lfs {
    lfs_cache_t rcache;
    lfs_cache_t pcache;

    lfs_block_t root[2];
    struct lfs_mlist {
        struct lfs_mlist *next;
        uint16_t id;
        uint8_t type;
        lfs_mdir_t m;
    } *mlist;
    uint32_t seed;

    struct lfs_globals {
        lfs_block_t pair[2];
        uint16_t id;
        bool hasmove;
        uint8_t orphans;
    } globals, locals;

    struct lfs_free {
        lfs_block_t off;
        lfs_block_t size;
        lfs_block_t i;
        lfs_block_t ack;
        uint32_t *buffer;
    } free;

    const struct lfs_config *cfg;
    lfs_size_t block_size;
    lfs_size_t block_count;
    lfs_size_t attr_max;
    lfs_size_t name_max;
    lfs_size_t inline_max;
} lfs_t;


/// Filesystem functions ///

// Format a block device with the littlefs
//
// Requires a littlefs object and config struct. This clobbers the littlefs
// object, and does not leave the filesystem mounted. The config struct must
// be zeroed for defaults and backwards compatibility.
//
// Returns a negative error code on failure.
int lfs_format(lfs_t *lfs, const struct lfs_config *config);

// Mounts a littlefs
//
// Requires a littlefs object and config struct. Multiple filesystems
// may be mounted simultaneously with multiple littlefs objects. Both
// lfs and config must be allocated while mounted. The config struct must
// be zeroed for defaults and backwards compatibility.
//
// Returns a negative error code on failure.
int lfs_mount(lfs_t *lfs, const struct lfs_config *config);

// Unmounts a littlefs
//
// Does nothing besides releasing any allocated resources.
// Returns a negative error code on failure.
int lfs_unmount(lfs_t *lfs);

/// General operations ///

// Removes a file or directory
//
// If removing a directory, the directory must be empty.
// Returns a negative error code on failure.
int lfs_remove(lfs_t *lfs, const char *path);

// Rename or move a file or directory
//
// If the destination exists, it must match the source in type.
// If the destination is a directory, the directory must be empty.
//
// Returns a negative error code on failure.
int lfs_rename(lfs_t *lfs, const char *oldpath, const char *newpath);

// Find info about a file or directory
//
// Fills out the info structure, based on the specified file or directory.
// Returns a negative error code on failure.
int lfs_stat(lfs_t *lfs, const char *path, struct lfs_info *info);

// Get a custom attribute
//
// Custom attributes are uniquely identified by an 8-bit type and limited
// to LFS_ATTR_MAX bytes. When read, if the stored attribute is smaller than
// the buffer, it will be padded with zeros. If the stored attribute is larger,
// then it will be silently truncated. If no attribute is found, the error
// LFS_ERR_NOATTR is returned and the buffer is filled with zeros.
//
// Returns the size of the attribute, or a negative error code on failure.
// Note, the returned size is the size of the attribute on disk, irrespective
// of the size of the buffer. This can be used to dynamically allocate a buffer
// or check for existance.
lfs_ssize_t lfs_getattr(lfs_t *lfs, const char *path,
        uint8_t type, void *buffer, lfs_size_t size);

// Set custom attributes
//
// Custom attributes are uniquely identified by an 8-bit type and limited
// to LFS_ATTR_MAX bytes. If an attribute is not found, it will be
// implicitly created.
//
// Returns a negative error code on failure.
int lfs_setattr(lfs_t *lfs, const char *path,
        uint8_t type, const void *buffer, lfs_size_t size);

// Removes a custom attribute
//
// If an attribute is not found, nothing happens.
//
// Returns a negative error code on failure.
int lfs_removeattr(lfs_t *lfs, const char *path, uint8_t type);


/// File operations ///

// Open a file
//
// The mode that the file is opened in is determined by the flags, which
// are values from the enum lfs_open_flags that are bitwise-ored together.
//
// Returns a negative error code on failure.
int lfs_file_open(lfs_t *lfs, lfs_file_t *file,
        const char *path, int flags);

// Open a file with extra configuration
//
// The mode that the file is opened in is determined by the flags, which
// are values from the enum lfs_open_flags that are bitwise-ored together.
//
// The config struct provides additional config options per file as described
// above. The config struct must be allocated while the file is open, and the
// config struct must be zeroed for defaults and backwards compatibility.
//
// Returns a negative error code on failure.
int lfs_file_opencfg(lfs_t *lfs, lfs_file_t *file,
        const char *path, int flags,
        const struct lfs_file_config *config);

// Close a file
//
// Any pending writes are written out to storage as though
// sync had been called and releases any allocated resources.
//
// Returns a negative error code on failure.
int lfs_file_close(lfs_t *lfs, lfs_file_t *file);

// Synchronize a file on storage
//
// Any pending writes are written out to storage.
// Returns a negative error code on failure.
int lfs_file_sync(lfs_t *lfs, lfs_file_t *file);

// Read data from file
//
// Takes a buffer and size indicating where to store the read data.
// Returns the number of bytes read, or a negative error code on failure.
lfs_ssize_t lfs_file_read(lfs_t *lfs, lfs_file_t *file,
        void *buffer, lfs_size_t size);

// Write data to file
//
// Takes a buffer and size indicating the data to write. The file will not
// actually be updated on the storage until either sync or close is called.
//
// Returns the number of bytes written, or a negative error code on failure.
lfs_ssize_t lfs_file_write(lfs_t *lfs, lfs_file_t *file,
        const void *buffer, lfs_size_t size);

// Change the position of the file
//
// The change in position is determined by the offset and whence flag.
// Returns the old position of the file, or a negative error code on failure.
lfs_soff_t lfs_file_seek(lfs_t *lfs, lfs_file_t *file,
        lfs_soff_t off, int whence);

// Truncates the size of the file to the specified size
//
// Returns a negative error code on failure.
int lfs_file_truncate(lfs_t *lfs, lfs_file_t *file, lfs_off_t size);

// Return the position of the file
//
// Equivalent to lfs_file_seek(lfs, file, 0, LFS_SEEK_CUR)
// Returns the position of the file, or a negative error code on failure.
lfs_soff_t lfs_file_tell(lfs_t *lfs, lfs_file_t *file);

// Change the position of the file to the beginning of the file
//
// Equivalent to lfs_file_seek(lfs, file, 0, LFS_SEEK_CUR)
// Returns a negative error code on failure.
int lfs_file_rewind(lfs_t *lfs, lfs_file_t *file);

// Return the size of the file
//
// Similar to lfs_file_seek(lfs, file, 0, LFS_SEEK_END)
// Returns the size of the file, or a negative error code on failure.
lfs_soff_t lfs_file_size(lfs_t *lfs, lfs_file_t *file);


/// Directory operations ///

// Create a directory
//
// Returns a negative error code on failure.
int lfs_mkdir(lfs_t *lfs, const char *path);

// Open a directory
//
// Once open a directory can be used with read to iterate over files.
// Returns a negative error code on failure.
int lfs_dir_open(lfs_t *lfs, lfs_dir_t *dir, const char *path);

// Close a directory
//
// Releases any allocated resources.
// Returns a negative error code on failure.
int lfs_dir_close(lfs_t *lfs, lfs_dir_t *dir);

// Read an entry in the directory
//
// Fills out the info structure, based on the specified file or directory.
// Returns a negative error code on failure.
int lfs_dir_read(lfs_t *lfs, lfs_dir_t *dir, struct lfs_info *info);

// Change the position of the directory
//
// The new off must be a value previous returned from tell and specifies
// an absolute offset in the directory seek.
//
// Returns a negative error code on failure.
int lfs_dir_seek(lfs_t *lfs, lfs_dir_t *dir, lfs_off_t off);

// Return the position of the directory
//
// The returned offset is only meant to be consumed by seek and may not make
// sense, but does indicate the current position in the directory iteration.
//
// Returns the position of the directory, or a negative error code on failure.
lfs_soff_t lfs_dir_tell(lfs_t *lfs, lfs_dir_t *dir);

// Change the position of the directory to the beginning of the directory
//
// Returns a negative error code on failure.
int lfs_dir_rewind(lfs_t *lfs, lfs_dir_t *dir);


/// Filesystem-level filesystem operations

// Finds the current size of the filesystem
//
// Note: Result is best effort. If files share COW structures, the returned
// size may be larger than the filesystem actually is.
//
// Returns the number of allocated blocks, or a negative error code on failure.
lfs_ssize_t lfs_fs_size(lfs_t *lfs);

// Traverse through all blocks in use by the filesystem
//
// The provided callback will be called with each block address that is
// currently in use by the filesystem. This can be used to determine which
// blocks are in use or how much of the storage is available.
//
// Returns a negative error code on failure.
int lfs_fs_traverse(lfs_t *lfs, int (*cb)(void*, lfs_block_t), void *data);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
