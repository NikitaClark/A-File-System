#include "blocks.h"
#include "inode.h"
#include "slist.h"
#include "directory.h"
#include "bitmap.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/**
 * Initializes the root directory in the filesystem
 * This function allocates an inode for the root directory, sets its mode,
 * and allocates a block for the directory entries.
 * 
 * It also handles Errors.
 * 
*/
void directory_init() {
    
    // Current inum
    int inum = alloc_inode();

    // Handle error: inode allocation failed
    if (inum < 0) {
        return;
    }

    // Current inode
    inode_t *directory_inode = get_inode(inum);

    // Handle error: inode retrieval failed
    if (!directory_inode) {
        return;
    }

    // Setting permissions
    directory_inode->mode=040755;

}

/**
 * Looks up a directory entry by name within a given directory.
 * 
 * @param di Pointer to the inode of the directory in which to search.
 * @param name The name of the entry to look for
 * @return The inode number of the found entry or -1 if not found.
*/
int directory_lookup(inode_t *di, const char *name) {

    // Max number of entries
    int num_entries = 64;

    // Checking if the given directory is the root directory, if so then return 0
    if (strcmp("",  name) == 0) {
        return 0; // Root directory
    }

    // Gets the array of the directory entries
    dirent_t *dir_entries = blocks_get_block(di->pointers[0]);

    // Getting the inum for the current directory name
    for (int i = 0; i < num_entries; ++i) {
        if (strcmp(dir_entries[i].name, name) == 0 && dir_entries[i].input_allocation == 1) {
            return dir_entries[i].inum;
        }
    }
    return -1;

}

/**
 * Looks up the inode number for a given path in the filesystem.
 * 
 * @param path The file path for which to find the inode number.
 * @return The inode number of the directory or file at the given path.
*/
int path_lookup(const char *path) {

    // root node
    int inum = 0;

    // Getting all the directories
    slist_t *all_directories = s_explode(path, '/');

    for (slist_t *curr_dir=all_directories; curr_dir != NULL; curr_dir = curr_dir->next) {

        // Current directory node
        inode_t *dir_node = get_inode(inum);

        inum = directory_lookup(dir_node, curr_dir->data);

        // Freeing the list if the lookup is not found
        if (inum == -1) {
            s_free(all_directories);
            return -1;
        }
    }

    s_free(all_directories); // Clearing up the list

    // Return the found inode number
    return inum;

}

/**
 * This function adds a directory entry for the given name and inode number.
 *
 * @param di Pointer to the inode of the directory where the entry will be added.
 * @param name Name of the new entry to be added.
 * @param inum Inode number of the new entry.
 * @return 0 on success.
 *
 */
int directory_put(inode_t *di, const char *name, int inum) {
    
    // Total number of entries
    int entries = di->size / sizeof(dirent_t);
    dirent_t *directory_entries = blocks_get_block(di->pointers[0]);
    
    // mock allocated check
    int allocated_check = 0;

    // new mock dirent structure with inum and allocated entry
    dirent_t mock_dir;
    strncpy(mock_dir.name, name, DIR_NAME_LENGTH); 
    mock_dir.inum = inum; 
    mock_dir.input_allocation = 1; 

    // Insert the new entry into the directory
    for (int i = 1; i < entries; i++) {
        if (directory_entries[i].input_allocation == 0) {
            directory_entries[i] = mock_dir;
            allocated_check = 1;
            return 0;
        }
    }

    // If no free space is found for the mock dir add at the end
    directory_entries[entries] = mock_dir;

    // Update the size of the entry array
    di->size = di->size + sizeof(dirent_t);

    return 0;
}

/**
 * This function finds the directory entry by name and marks it as deallocated.
 * If the inode's reference count reaches zero, it frees the inode.
 * 
 * @param dd Pointer to the inode of the directory from which the entry will be deleted.
 * @param name The name of the entry to be deleted.
 * @return 0 on successful deletion
 */
int directory_delete(inode_t *di, const char *name) {

    // Total number of entries
    int entries = di->size / sizeof(dirent_t);
    dirent_t *directory_entries = blocks_get_block(di->pointers[0]);

    for (int i = 0; i < entries; i++) {
        
        // Check the current entry if the name matches
        if (strcmp(directory_entries[i].name, name) == 0 && directory_entries[i].input_allocation) {
            
            // Delete the current entry
            int inum = directory_entries[i].inum;
            inode_t *curr_inode = get_inode(inum);
            curr_inode->refs--;

            // Proceed to free the inode if refs is 0 or less
            if (curr_inode->refs <= 0) {
                free_inode(inum);
            }

            // Deallocate the entry from the directory
            directory_entries[i].input_allocation = 0;

            return 0;
        }
    }

    // Return an error code if the directory is not found
    return -ENOENT;
}

/**
 * Creates a list of the names of all entries in the specified directory.
 *
 * @param path The path to the directory whose entries are to be listed.
 * @return A singly-linked list (slist_t) of entry names. Returns NULL
 *         if the directory is not found or an error occurs.
 *
 */
slist_t *directory_list(const char *path) {

    // get the inum of the directory
    int dir_inum = path_lookup(path);

    // get the inode
    inode_t *dir_inode = get_inode(dir_inum);

    // Number of entries in the directoy 
    int entries = dir_inode->size / sizeof(dirent_t);
    dirent_t *directory_entries = blocks_get_block(dir_inode->pointers[0]);

    // Initialize an empty directory list
    slist_t *new_dir = NULL;

    // Updae the directory list
    for (int i = 0; i < entries; ++i) {
        if (directory_entries[i].input_allocation) {
            // Add the entry name to the list
            new_dir = s_cons(directory_entries[i].name, new_dir);
        }
    }

    // Return the lst of directory entries
    return new_dir;

}

/**
 * Prints the names of all entries in the specified directory.
 *
 * @param dd Pointer to the inode of the directory to be printed.
 */
void print_directory(inode_t *dd) {

    // Number of entries in the directoy 
    int entries = dd->size / sizeof(dirent_t);
    dirent_t *directory_entries = blocks_get_block(dd->pointers[0]);

    //print the entries present in the directory
    for (int i = 0; i < entries; ++i) {
        printf(" %s\n", directory_entries[i].name);
    }
}