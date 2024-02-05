#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "inode.h"
#include "blocks.h"
#include "bitmap.h"




/**
 * Prints information about the given inode.
 *
 * @param node Pointer to the inode to be printed.
 */
void print_inode(inode_t *node) {
    if (node != NULL) {
        printf("References: %d\n", node->refs);
        printf("Mode: %d\n", node->mode);
        printf("Size: %d\n", node->size);
        printf("Block: %d\n", node->block);
        printf("Large Block Pointers: %d, %d\n", node->pointers[0], node->pointers[1]);
    }
    else
    {
        printf("The given node is does not exist.\n");
    }
}




/**
 * Retrieves an inode by its number.
 *
 * @param inum The index of the inode to retrieve.
 * @return Pointer to the inode.
 */
inode_t *get_inode(int inum) {

    // Getting the start of the inode array
    inode_t *start_node = get_inode_bitmap() + BLOCK_BITMAP_SIZE;

    // Return the inode at the given index
    return &start_node[inum];
}


/**
 * Allocates a new inode and initializes it.
 *
 * @return The index of the newly allocated inode.
 *
 */
int alloc_inode() {

    // Get the bitmap for the inode
    void *curr_bitmap = get_inode_bitmap();

    // mock index
    int node_index = -1;

    // Allocating a block
    for (int i = 0; i < BLOCK_COUNT; ++i) {

        if(bitmap_get(curr_bitmap, i) == 0) {
            bitmap_put(curr_bitmap, i, 1);
            node_index = i;
            break;
        }
    }

    // New inode
    inode_t *inode = get_inode(node_index);
    inode->refs = 1;
    inode->mode = 0;
    inode->size = 0;
    inode->pointers[0] = alloc_block();

    return node_index;

}

/**
 * Frees an inode.
 *
 * @param inum The index of the inode to free.
 */
void free_inode(int inum) {

    // Get the bitmap for the inode
    void *curr_bitmap = get_inode_bitmap();

    inode_t *inode_delete = get_inode(inum);

    // Shrink the inode size to 0
    shrink_inode(inode_delete, 0);

    // Free the block associated with this inode
    free_block(inode_delete->pointers[0]);

    // Free the inode in the bitmap
    bitmap_put(curr_bitmap, inum, 0);

    // Ensure the indode's size is set to 0
    assert(inode_delete->size == 0);
}


/**
 * Grows an inode to the specified size, allocating additional blocks as needed.
 *
 * @param node Pointer to the inode to grow.
 * @param size The new size of the inode.
 * @return 0 on success.
 *
 */
int grow_inode(inode_t *node, int size) {

    // Get the current size of the node
    int curr_size = (node->size / BLOCK_SIZE) + 1;

    // How much size we will need
    int size_needed = size / BLOCK_SIZE;

    for (int i = curr_size; i <= size_needed; ++i) {

        // Checking if we have already allocated the maximum number of pointers 
        if (i >= 2) {
            if (node->block == 0) {
                // Allocate a new block as there is nothing present in the main block
                node->block = alloc_block();
            }
            // Retrieve the current main pointer so we can allocate a new page (Large Files)
            int *current_pointer = blocks_get_block(node->block);
            current_pointer[i-2] = alloc_block();
        }
        else {
            // We have space avaliable in the block so we allocate into it
            node->pointers[i] = alloc_block();
        }
    }

    // Update the current inode size to include the addition
    node->size = size;

    return 0;

}

/**
 * Shrinks an inode to the specified size, freeing extra block pointers if required.
 *
 * @param node Pointer to the inode to shrink.
 * @param size The new size of the inode.
 * @return 0 on success, or a negative error code on failure.
 *
 * This function is used for larger files where blocks may need to be freed.
 */
int shrink_inode(inode_t *node, int size) {

    // Get the current size of the node
    int curr_size = (node->size / BLOCK_SIZE) + 1;

    // How much size current is in relation to the BLOCK_SIZE
    int size_needed = size / BLOCK_SIZE;

    for (int i = curr_size; i >= size_needed; i--) {
        // Similar approach to grow_inode just opposite
        // Checking if we have already allocated the maximum number of pointers 
        if (i >= 2) {
            // Retrieve the current main pointer so we can allocate a new page (Large Files)
            int *current_pointer = blocks_get_block(node->block);
            int curr_point = current_pointer[i-2];

            // Free the current block
            free_block(curr_point);
            curr_point = 0;

            // Check to see if we deallocated everything requiored
            if (i == 2) {
                // Free block in the main pointer
                free_block(node->block);
                node->block = 0;
            }

        }
        else {
            // Free the blocks currently in the pointer
            free_block(node->pointers[i]);
            node->pointers[i] = 0;
        }
    }
    
    // Update the current inode size to include the addition
    node->size = size;

    return 0;
}

/**
 * Returns the block number for a given file block number in an inode.
 *
 * @param node Pointer to the inode.
 * @param fbnum The file block number.
 * @return The block number corresponding to the file block number.
 */
int inode_get_bnum(inode_t *node, int file_bnum) {

    // Get the current pointer
    int curr_pointer = file_bnum / BLOCK_SIZE;

    // Check to see if the pointer is in the block
    if (curr_pointer >= 2) {
        int *curr_block_pointer = blocks_get_block(node->block);
        return curr_block_pointer[curr_pointer - 2];
    }
    else {
        // return the block number 
        return node->pointers[curr_pointer];
    }

}


