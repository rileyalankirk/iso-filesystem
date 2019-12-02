/**
 * Part 2 of ISO Filesystem: Gets information about a single file in an ISO image file.
 * 
 * This can be compiled with:
 *     gcc -Wall part2.c -o part2
 * 
 * You will need to copy your load_iso() and free_iso() functions from part 1.
 * 
 * The output can be checked against the provided samples. A list of files in an ISO can be seen
 * with the `isoinfo -R -f` command.
 * 
 * You will need to following functions complete this part:
 *      strcmp()    https://linux.die.net/man/3/strcmp
 * Along with structures and constants defined in iso.h and util.h along with the following
 * functions defined in util.h:
 *      get_path_names() and free_path_names()
 *      get_record_filename()
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
    if (!(iso = (ISO*) malloc(sizeof(ISO)))) { return NULL; }
    iso->pvd = NULL;

    // Open the ISO file
    // Setup the fd, size, and data fields in iso
    if ((iso->fd = open(filename, O_RDONLY)) == -1) { free(iso); return NULL; }
    struct stat stats;
    if (fstat(iso->fd, &stats) == -1) { close(iso->fd); free(iso); return NULL; }
    iso->size = stats.st_size;
    if ((iso->raw = mmap(NULL, iso->size, PROT_READ, MAP_PRIVATE, iso->fd, 0)) == (void *) -1) { close(iso->fd); free(iso); return NULL; }

    // Setup fields based on ISO data
    int offset = 0x8000;
    bool terminated = false;
    while(offset < iso->size) {
        VolumeDescriptor* curr_descr = (VolumeDescriptor*) &iso->raw[offset]; // The current volume descriptor
        if (curr_descr->type_code == VD_TERMINATOR) { terminated = true; break; }
        // Checks the version, id, type code, and if a primary volume descriptor has already been found
        if (curr_descr->version == 1 && memcmp(curr_descr->id, CD001, 5) == 0 && curr_descr->type_code == VD_PRIMARY && !iso->pvd) {
            iso->pvd = (PrimaryVolumeDescriptor*) &iso->raw[offset];
        }
        offset += 0x800;
    }
    // TODO: Check the ISO for problems and cleanup everything if there is a problem
    // TODO: Also find the Primary Volume Descriptor and set the pvd fields in the iso variable

    // TODO: Check if a Primary Volume Descriptor was not found or we got to the end of the file
    // before the Terminator was found
    if (!iso->pvd || !terminated) {
        errno = EINVAL;
        close(iso->fd);
        munmap(iso->raw, iso->size);
        free(iso);
        return NULL;
    }

    // Return the setup iso variable
    return iso;
}

/**
 * Cleans up an ISO structure after it is done being used. This means that the memory is unmapped,
 * the file descriptor is closed, and the allocated memory is freed.
 */
void free_iso(ISO* iso)
{
    close(iso->fd);
    if (iso->raw) { munmap(iso->raw, iso->size); }
    free(iso);
}

/**
 * Gets a single record from an ISO based on the given path. If the path cannot be found than NULL
 * is returned.
 * 
 * This starts from the root record in the primary volume descriptor of the ISO file. This matches
 * the / path of the ISO file. Using get_path_names(), the other parts of the path name can be
 * obtained from the given path. Each directory is searched, matching the current path name part
 * with the record's filename (obtained using get_record_filename()). If a part cannot be found,
 * than ernno is set to ENOENT (file not found) and NULL is returned. If any part (but the last
 * part) is not a directory, than errno is set to ENOTDIR and NULL is returned. This is also done
 * if the last part is not a directory and their is a trailing slash. 
 */
const Record* get_record(const ISO* iso, const char* path)
{
    // Get the root directory record, if path is just "/" then return it
    Record* curr_record;
    Record* curr_dir = &iso->pvd->root_record; // The current directory; beginning from the root
    if (strcmp(path, "/")) { return curr_dir; }
    
    // Get the path name parts from the given path
    path_names* path_parts = get_path_names(path);
    if (!path_parts) { return NULL; }

    // Go through each name in the set of path names
    for (int i = 0; i < path_parts->count; i++) {        
        // Check that the end of the extent is within the ISO file raw data
        uint32_t start_pos = curr_dir->extent_location*iso->pvd->logical_block_size; // Start of directory
        if (start_pos + curr_dir->extent_length > iso->size) {
            errno = EINVAL;
            return NULL;
        }

        // Go through each record in the directory, comparing the filename of the record
        // with the path name part
        curr_record = (Record*) &iso->raw[start_pos];
        uint32_t offset = 0;
        bool found_path_part = false;
        while (true) {
            // Update offset; jump to next block if necessary
            if (curr_record->length == 0) {
                offset = (((start_pos + offset)/iso->pvd->logical_block_size) + 1)*iso->pvd->logical_block_size - start_pos;
            }
            offset += curr_record->length;

            // Make sure we are not outside of the current directory
            if (offset >= curr_dir->extent_length) { break; }

            // Get the record's filename and check for a match
            char filename[256];
            get_record_filename(iso, curr_record, filename);
            if (found_path_part = strcmp(filename, path_parts->names[i])) { break; }

            // Advance to the next record (make sure to account for end-of-sector issues)
            curr_record = (Record*) &iso->raw[offset + start_pos];
        }

        // Check if we failed to find a match - file/directory does not exist
        if (!found_path_part) {
            if (i == 0) { errno = ENOENT; } // This is the case where no part of the path was found
            return NULL;
        }

        // Check if a regular file matched something supposed to be a directory
        if ((i != path_parts->count - 1 || path_parts->trailing_slash) && !(curr_record->file_flags & FILE_DIRECTORY)) {
            errno = ENOTDIR;
            return NULL;
        }

        curr_dir = (Record*) &iso->raw[curr_dir->extent_length + start_pos];
    }

    // Cleanup and return the found record
    free_path_names(path_parts);
    return curr_record;
}

int main(int argc, char *argv[])
{
    // Perform some sanity checking on the command line
    if (argc != 3) {
        fprintf(stderr, "usage:  %s iso_file file_path\n", argv[0]);
        fprintf(stderr, "The file_path must start with a / and is a path on the ISO image\n");
        return 1;
    }

    // Get arguments
    const char* isofilename = argv[1];
    const char* filename = argv[2];

    // Load the ISO file
    ISO* iso = load_iso(isofilename);
    if (!iso) { perror("opening iso"); return 1; }

    // Find the file
    const Record* record = get_record(iso, filename);
    if (!record) { free_iso(iso); perror("file not find in the ISO"); return 2; }

    // Display information about the record
    printf("Basic File Information\n");
    printf("-------------------------\n");
    printf("Record Length:   0x%02x %u bytes\n", record->length, record->length);
    printf("Extent Location: 0x%08x %u blocks\n", record->extent_location, record->extent_location);
    printf("Extent Length:   0x%08x %u bytes\n", record->extent_length, record->extent_length);
    char date[256];
    time_t dt = convert_datetime(&record->datetime);
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&dt));
    printf("Date/Time: %s\n", date);
    printf("Flags: %02x\n", record->file_flags);
    char name[256];
    strncpy(name, record->filename, record->filename_length);
    name[record->filename_length] = 0;
    printf("Raw Filename: %s\n", name);

    // Try to get Rock Ridge data and display it
    RRExtraData rr;
    read_rock_ridge_data(iso, record, &rr);
    if (rr.flags != 0) {
        printf("\nRock Ridge Extension Info\n");
        printf("-------------------------\n");
        if (rr.flags & RR_HAS_STAT) {
            printf("Mode:   0%04o\n", rr.mode);
            printf("#Links: %d\n", rr.nlinks);
            printf("UID:    %d\n", rr.uid);
            printf("GID:    %d\n", rr.gid);
        }
        if (rr.flags & RR_HAS_INO) {
            printf("Inode:  %d\n", rr.ino);
        }
        if (rr.flags & RR_HAS_FILENAME) {
            printf("Filename: %s\n", rr.filename);
        }
        if (rr.flags & RR_HAS_CREATION) {
            strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&rr.creation));
            printf("Creation:     %s\n", date);
        }
        if (rr.flags & RR_HAS_MODIFICATION) {
            strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&rr.modification));
            printf("Modification: %s\n", date);
        }
        if (rr.flags & RR_HAS_ACCESS) {
            strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&rr.access));
            printf("Access:       %s\n", date);
        }    }

    // Cleanup
    free_iso(iso);
    return 0;
}
