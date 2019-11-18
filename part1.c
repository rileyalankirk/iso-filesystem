/**
 * Part 1 of ISO Filesystem: Just load the ISO image and print out some relevant information from
 * the Primary Volume Descriptor.
 * 
 * This can be compiled with:
 *     gcc -Wall part1.c -o part1
 * 
 * The output can be checked against the provided samples or the `isoinfo -d` command (you would
 * have to install that and be aware that it prints out way more information).
 * 
 * You will need to following functions complete this part:
 *      open() and close()  https://linux.die.net/man/2/open and https://linux.die.net/man/2/close
 *      fstat()             https://linux.die.net/man/2/fstat
 *      mmap() and munmap() https://linux.die.net/man/2/mmap
 *      malloc() and free() https://linux.die.net/man/3/malloc
 *      memcmp()            https://linux.die.net/man/3/memcmp
 * See the util.h file for a definition of the ISO structure.
 */

// Enable POSIX 2008 functions
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE

// Tell FUSE that we support large files
#define _FILE_OFFSET_BITS 64

// Tons of includes...
#include "iso.h"
#include "util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

/**
 * Loads an ISO file into an ISO structure from the given file name. This opens the file, maps it
 * into memory, and finds the Primary Volume Descriptor while also checking that the headers of the
 * ISO file are valid. Returns NULL if there is an issue. If a problem is found with the actual
 * ISO headers than errno is set to EINVAL. In all other cases of problems, errno can be assumed to
 * be set by the called function.
 */
ISO* load_iso(const char* filename)
{
    // TODO: Allocate the memory for our filesystem, make sure that pvd is set to NULL
    ISO* iso;

    // Open the ISO file
    // TODO: setup the fd, size, and data fields in iso
    // TODO: make sure to check every return value and cleanup everything if there is a problem

    // Setup fields based on ISO data
    // TODO: Check the ISO for problems and cleanup everything if there is a problem
    // TODO: Also find the Primary Volume Descriptor and set the pvd fields in the iso variable

    // TODO: Check if a Primary Volume Descriptor was not found or we got to the end of the file
    // before the Terminator was found

    // Return the setup iso variable
    return iso;
}

/**
 * Cleans up an ISO structure after it is done being used. This means that the memory is unmapped,
 * the file descriptor is closed, and the allocated memory is freed.
 */
void free_iso(ISO* iso)
{
    // TODO
}

int main(int argc, char *argv[])
{
    // Perform some sanity checking on the command line
    if (argc != 2) {
        fprintf(stderr, "usage:  %s iso_file\n", argv[0]);
        return 1;
    }

    // Get the ISO file path out of the argument list
    const char* filename = argv[1];

    // Load the ISO file
    ISO* iso = load_iso(filename);
    if (!iso) { perror("opening iso"); return 1; }

    // Display information
    printf("Primary Volume Descriptor\n");
    printf("-------------------------\n");
    printf("Offset in file: 0x%lx\n", ((uint8_t*)iso->pvd) - iso->raw);
    printf("Type Code: 0x%02x  (always 0x01)\n", iso->pvd->header.type_code);
    printf("ID:        %c%c%c%c%c (always CD001)\n", iso->pvd->header.id[0], iso->pvd->header.id[1], iso->pvd->header.id[2], iso->pvd->header.id[3], iso->pvd->header.id[4]);
    printf("Version:   0x%02x  (always 0x01)\n", iso->pvd->header.version);
    printf("Volume Space Size:  0x%04x %u blocks\n", iso->pvd->volume_space_size, iso->pvd->volume_space_size);
    printf("Logical Block Size: 0x%04x %u bytes/block\n", iso->pvd->logical_block_size, iso->pvd->logical_block_size);

    // Cleanup
    free_iso(iso);
    return 0;
}
