#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "slist.h"
#include "directory.h"
#include "storage.h"
#include "bitmap.h"


// Helper function declaration (Shall be described further later)
static void split_path(const char *fullPath, char *parentPath, char *childName);

/**
 * Initializes the storage system.
 *
 * @param path Path to the storage location.
 *
 */
void storage_init(const char *path) {

    // Initialize the block at the given path
    blocks_init(path);

    // Ensure that necessary blocks are allocated
    if (bitmap_get(get_blocks_bitmap(), 1) == 0) {

        // Allocating initial blocks
        for (int blockIndex = 0; blockIndex < 3; blockIndex++) {

            // Allocate a block if not already done
            alloc_block();
        }
    }

    // Initialize the root directory if its not already present
    if (bitmap_get(get_blocks_bitmap(), 4) == 0) {
        directory_init();
    }

}


/**
 * Retrieves the metadata for a given file.
 *
 * @param path Path to the file.
 * @param st Pointer to the stat structure to fill with file metadata.
 * @return 0 on success, or -1 if the path is invalid.
 *
 */
int storage_stat(const char *path, struct stat *st) {

    // Lookup the inode number
    int inodeNumber = path_lookup(path);

    // Checking if the file is found
    if (inodeNumber <= 0) {
        return -1;
    }

    // Get the inode 
    inode_t *inode = get_inode(inodeNumber);
    
    // Set link count
    st->st_nlink = inode->refs;

    // Set file mode
    st->st_mode = inode->mode;

    // Set file size
    st->st_size = inode->size;

    return 0;
}

/**
 * Truncates a file to a specified size.
 *
 * @param path Path to the file.
 * @param size New size of the file.
 * @return 0 on success, or -1 if the path is invalid.
 *
 * This function either grows or shrinks the file to the specified size.
 */
int storage_truncate(const char *path, off_t size) {

    // Lookup inode number
    int inodeNumber = path_lookup(path);

    // Checking if the file is found
    if (inodeNumber <= 0) {
        return -1;
    }

    // Get inode
    inode_t *inode = get_inode(inodeNumber);

    if (size > inode->size) {
        // Expand the file
        grow_inode(inode, size);
    }
    else {
        // Shrink file
        shrink_inode(inode, size);
    }

    return 0;
}

/**
 * Reads data from a file.
 *
 * @param path Path to the file.
 * @param buf Buffer to store the read data.
 * @param size Number of bytes to read.
 * @param offset Offset in the file to start reading from.
 * @return The number of bytes read, or -1 on error.
 *
 */
int storage_read(const char *path, char *buf, size_t size, off_t offset) {

    // Lookup inode number
    int inodeNumber = path_lookup(path);

    // Checking if the file is found
    if (inodeNumber <= 0) {
        return -1;
    }

    // Get inode
    inode_t *inode = get_inode(inodeNumber);

    if (offset >= inode->size) {
        return 0; // Offset beyond file size
    }

    int remainingSize = size;
    int bytesRead = 0;

    while (remainingSize > 0) {

    // Block Bitmap number
    int blockNum = inode_get_bnum(inode, offset + bytesRead);

    // Block Pointer
    char *blockPtr = blocks_get_block(blockNum) + offset % BLOCK_SIZE;

    // Offset Pointer
    int blockReadSize = BLOCK_SIZE - offset % BLOCK_SIZE;

    int readSize = (remainingSize < blockReadSize) ? remainingSize : blockReadSize;

    memcpy(buf + bytesRead, blockPtr, readSize);

    remainingSize -= readSize;
    bytesRead += readSize;
    }

    return bytesRead; // Total bytes read
}

/**
 * Writes data to a file.
 *
 * @param path Path to the file.
 * @param buf Buffer containing the data to write.
 * @param size Number of bytes to write.
 * @param offset Offset in the file to start writing to.
 * @return The number of bytes written, or -1 on error.
 *
 */
int storage_write(const char *path, const char *buf, size_t size, off_t offset) {

    int inodeNumber = path_lookup(path); // Lookup inode number

    if (inodeNumber <= 0)
    {
        return -1; // File not found
    }

    inode_t *inode = get_inode(inodeNumber); // Get inode

    int endOffset = offset + size;
    if (endOffset > inode->size)
    {
        storage_truncate(path, endOffset); // Expand file
    }

    int bytesWritten = 0;

    while (size > 0)
    {
        int blockNum = inode_get_bnum(inode, offset + bytesWritten);
        char *blockPtr = blocks_get_block(blockNum) + offset % BLOCK_SIZE;
        int blockWriteSize = BLOCK_SIZE - offset % BLOCK_SIZE;
        int writeSize = (size < blockWriteSize) ? size : blockWriteSize;

        memcpy(blockPtr, buf + bytesWritten, writeSize);

        size -= writeSize;
        bytesWritten += writeSize;
    }

    return bytesWritten; // Total bytes written

}


/**
 * Creates a new file or directory.
 *
 * @param path Path where the new file or directory should be created.
 * @param mode The mode (permissions) for the new file or directory.
 * @return 0 on success, or an error code on failure.
 *
 */
int storage_mknod(const char *path, int mode){
    int inodeNumber = path_lookup(path);
    if (inodeNumber != -1)
    {
        return -EEXIST; // File already exists
    }

    char parentPath[strlen(path) + 1];
    char childName[DIR_NAME_LENGTH + 1];
    split_path(path, parentPath, childName);

    int parentInodeNum = path_lookup(parentPath);
    if (parentInodeNum < 0)
    {
        return -ENOENT; // Parent directory not found
    }

    inode_t *parentInode = get_inode(parentInodeNum);
    int childInodeNum = alloc_inode();
    inode_t *childInode = get_inode(childInodeNum);
    childInode->refs = 1;
    childInode->mode = mode;
    childInode->size = 0;

    directory_put(parentInode, childName, childInodeNum);

    return 0; // Success
}

/**
 * Unlinks (removes) a file or directory.
 *
 * @param path Path to the file or directory to be removed.
 * @return 0 on success, or an error code on failure.
 *
 */
int storage_unlink(const char *path){

    char parentPath[strlen(path) + 1];
    char fileName[DIR_NAME_LENGTH + 1];
    split_path(path, parentPath, fileName);

    int parentInodeNum = path_lookup(parentPath);
    inode_t *parentInode = get_inode(parentInodeNum);

    int unlinkResult = directory_delete(parentInode, fileName);

    return unlinkResult; // Result of unlink operation
}

/**
 * Creates a link (hard link) to a file.
 *
 * @param from The path of the target file.
 * @param to The path of the link to create.
 * @return 0 on success, or an error code on failure.
 *
 */
int storage_link(const char *from, const char *to){
    int toInodeNum = path_lookup(to);
    if (toInodeNum < 0)
    {
        return -1; // 'to' path not found
    }

    inode_t *toInode = get_inode(toInodeNum);

    char parentPath[strlen(from) + 1];
    char fileName[DIR_NAME_LENGTH + 1];
    split_path(from, parentPath, fileName);

    int parentInodeNum = path_lookup(parentPath);
    inode_t *parentInode = get_inode(parentInodeNum);

    directory_put(parentInode, fileName, toInodeNum);
    toInode->refs++;

    return 0; // Success

}

/**
 * Renames or moves a file or directory.
 *
 * @param from The current path of the file or directory.
 * @param to The new path of the file or directory.
 * @return 0 on success, or an error code on failure.
 *
 */
int storage_rename(const char *from, const char *to) {
    int linkResult = storage_link(to, from);
    if (linkResult != 0) {
        return linkResult; // Error in creating link
    }

    int unlinkResult = storage_unlink(from);
    return unlinkResult; // Result of unlink operation

}

/**
 * Helper function to update parent and child paths.
 *
 * @param path Full path of the file or directory.
 * @param parent Buffer to store the parent path.
 * @param new_path Buffer to store the child path.
 *
 * This function splits a full path into its parent and child components.
 */
void split_path(const char *fullPath, char *parentPath, char *childName){
    slist_t *pathComponents = s_explode(fullPath, '/');
    parentPath[0] = 0;

    slist_t *item;
    for (item = pathComponents; item->next != NULL; item = item->next)
    {
        strcat(parentPath, "/");
        strncat(parentPath, item->data, DIR_NAME_LENGTH);
    }

    strncpy(childName, item->data, DIR_NAME_LENGTH);
    childName[DIR_NAME_LENGTH] = 0;
    s_free(pathComponents);
}

/**
 * Lists the contents of a directory.
 *
 * @param path Path to the directory.
 * @return A list of directory entries, or NULL on error.
 *
 */
slist_t *storage_list(const char *path){
    return directory_list(path); // Delegate to directory_list function
}
