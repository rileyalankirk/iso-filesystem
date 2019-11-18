/**
 * Implements a FUSE Filesystem that allows read-only access to ISO image files.
 * 
 * To compile on a macOS computer:
 *     gcc -I/usr/local/include/osxfuse/fuse isofs.c -Wall -o isofs -losxfuse
 * You will need to install OSXFuse first (can be done with brew cask install oxsfuse).
 * 
 * To run it will be something along the lines of:
 *     ./isofs [-f] test.iso mount
 * Where [] indicates optional. The mount folder must exist and be empty (use mkdir to create it).
 * You can use the command `umount mount` to unmount the drive (or CTRL+C if you started the
 * program with -f). Note that if the program crashes you may still need to unmount it.
 */

// Enable POSIX 2008 functions
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE

// The FUSE API has been changed a number of times. We announce that we support v2.6.
#define FUSE_USE_VERSION 26

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

#include <fuse.h>
#ifdef __APPLE__
#include <fuse_darwin.h>
#endif

#define GET_ISO() ((const ISO*)fuse_get_context()->private_data)

// If you add -D_DEBUG to your compile command-line than every isofs_*() function will printout when
// it gets called (you would also need to run your program with -f to be in the foreground).
#ifdef _DEBUG
#define LOG(s, ...) printf(s, __VA_ARGS__);
#else
#define LOG(s, ...)
#endif

/**
 * Loads an ISO file into an ISO structure from the given file name. This opens the file, maps it
 * into memory, and finds the Primary Volume Descriptor while also checking that the headers of the
 * ISO file are valid. Returns NULL if there is an issue. If a problem is found with the actual
 * ISO headers than errno is set to EINVAL. In all other cases of problems, errno can be assumed to
 * be set by the called function.
 */
ISO* load_iso(const char* filename)
{
    // TODO: copy from part 1
}

/**
 * Cleans up an ISO structure after it is done being used. This means that the memory is unmapped,
 * the file descriptor is closed, and the allocated memory is freed.
 */
void free_iso(ISO* iso)
{
    // TODO: copy from part 1
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
    // TODO: copy from part 2
}

/**
 * Check that the current user is allowed to access the given Record/path in the ISO. The mask is a
 * combination of R_OK, W_OK, and X_OK flags as would be given to the access system function
 * (except F_OK is not allowed).
 */
bool check_access(const ISO* iso, const Record* record, const char* path, int mask)
{
    // First we are going to check that we can access the path itself - we need the execute
    // privilege on every parent directory. We do this recursively.
    // First we get the name of the parent directory.
    char parent[PATH_MAX];
    strncpy(parent, path, PATH_MAX); // copy the entire filename first
    char* slash = strrchr(parent, '/'); // find the last slash
    if (slash && !slash[1]) { *slash = 0; slash = strrchr(parent, '/'); } // remove trailing slash
    if (slash) {
        // Has a parent directory - check the execute bit on it
        slash[1] = 0; // chop the string off just after the slash to be only the parent directory name
        if (!check_access(iso, get_record(iso, parent), parent, X_OK)) { // recursively check access
            // We found the parent directory and it has bad access
            return false;
        }
    }

    // Check the access to the file itself
    bool is_root = fuse_get_context()->uid == 0;
    bool is_user = fuse_get_context()->uid == getuid();
    bool is_grp  = fuse_get_context()->gid == getgid();
    bool is_dir = record->file_flags & FILE_DIRECTORY;
    RRExtraData rr;
    read_rock_ridge_data(iso, record, &rr);
    int access = !(rr.flags & RR_HAS_STAT) ? (is_dir ? 5 : 4) :
                    (is_root ? ((rr.mode&(S_IXUSR|S_IXGRP|S_IXOTH)) ? 7 : 6) : // super/root user is special
                            (rr.mode >> (is_user ? 6 : (is_grp ? 3 : 0)))); // these just need to extract a different set of 3 bits
    return (access & 7 & mask) == mask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Prototypes for all these functions and the C-style come from /usr/include/fuse.h or            //
// /usr/local/include/osxfuse/fuse.h on macOS with brew.                                          //
////////////////////////////////////////////////////////////////////////////////////////////////////

////////// Setup and Tear-down /////////////////////////////////////////////////////////////////////

/**
 * Initialize filesystem. The return value will become the private_data field of the fuse_context()
 * value and passed as a parameter to the destroy() method.
 */
void *isofs_init(struct fuse_conn_info *conn)
{
    // This is just what we have to do here. It would be nice if we could open the ISO file in this
    // function, but we have no way to send error messages if it fails to open for some reason.
    // Instead that is all done in the main() function.
    return (void*)GET_ISO();
}

/**
 * Clean up filesystem. Called on filesystem exit.
 * 
 * It unmaps the file, closes the open file descriptor, and frees allocated memory.
 */
void isofs_destroy(void *userdata)
{
    free_iso((ISO*)userdata);
}


////////// Basic Information Operations ////////////////////////////////////////////////////////////

/** Get file system statistics
 *
 * The 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 */
// This is emulating the system call statvfs: https://linux.die.net/man/2/statvfs
//    That link also includes the details of the statvfs struct.
int isofs_statfs(const char *path, struct statvfs *statv)
{
    LOG("statfs(path=\"%s\", statvfs=%p)\n", path, statv);

    // Most of these values are just set to 0 or filler values. The only ones with actual values are
    // the logical size of a block and fragment (use the same value for both), the total size of the
    // file system, and the number of files.
    // TODO: fill in the ones that make sense (as listed above)

    // The rest are just filled in with whatever
    statv->f_bfree = 0;
    statv->f_bavail = 0;
    statv->f_ffree = 0;
    statv->f_namemax = PATH_MAX;
    return 0;
}

/** Get file attributes.
 *
 * Similar to stat(). The 'st_dev' and 'st_blksize' fields are
 * ignored. The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
// This is emulating the system call stat: https://linux.die.net/man/2/stat
//    That link also includes the details of the struct stat.
int isofs_getattr(const char *path, struct stat *statbuf)
{
    LOG("getattr(path=\"%s\", statbuf=%p)\n", path, statbuf);

    // TODO: Find the ISO record (which can be either a file or directory)
    // In the case of an error, return -errno

    // TODO: Get any extra available Rock Ridge data (see the main() in part 2 for an example)
    
    // TODO: Fill in the filesystem stat object using the record and the Rock Ridge data
    // Prefer Rock Ridge data over record data if available
    // If not otherwise available:
    //  directories should be readable and executable by all and files should be readable by all
    //  uid and gid should be the current user and group (use getuid() and getgid())
    //  nlink should be a fixed, reasonable, value

    // Always set rdev to 0 and don't touch dev and blksize
    statbuf->st_rdev = 0;
    return 0;
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call. If the 'default_permissions' mount option is
 * given, this method is not called.
 */
// This is emulating the system call access: https://linux.die.net/man/2/access
//    That link also includes the details of the possible mask values.
int isofs_access(const char *path, int mask)
{
    LOG("access(path=\"%s\", mask=%d)\n", path, mask);

    // TODO: Our filesystem is read-only, if they request W_OK access return -EROFS
    if (mask & W_OK) { return -EROFS; }

    // TODO: Find the ISO record (which can be either a file or directory)
    // In the case of an error, return -errno
    const Record* record = NULL; // TODO: remove this NULL and this comment once you actually write the code

    // Check the access bits (take note of the check_access() function here, you will need it later)
    if (mask == F_OK) { return 0; }
    if (!check_access(GET_ISO(), record, path, mask)) { return -EACCES; }
    return 0;
}

////////// Directory Reading ///////////////////////////////////////////////////////////////////////

/** Open directory
 *
 * Unless the 'default_permissions' mount option is given, this method should check if opendir is
 * permitted for this directory. Optionally opendir may also return an arbitrary filehandle in the
 * fuse_file_info structure, which will be passed to readdir, closedir and fsyncdir.
 */
// This is emulating the system function opendir: https://linux.die.net/man/3/opendir
// This needs to find the directory record (making sure it is a directory), check that it can be
// read, then assign the record as the "file-handle".
int isofs_opendir(const char *path, struct fuse_file_info *fi)
{
    LOG("opendir(path=\"%s\", fi=%p)\n", path, fi);

    // TODO: Get the directory record
    // In the case of an error, return -errno
    // If it isn't a directory, return -ENOTDIR, if it doesn't have R_OK access, return -EACCES
    const Record* record = NULL; // TODO: remove this NULL and this comment once you actually write the code

    // Set the file-handle as our directory object
	fi->fh = (uintptr_t)record;
	return 0;
}

/** Read directory
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and passes zero to the filler
 *    function's offset. The filler function will not return '1' (unless an error happens), so the
 *    whole directory is read in a single readdir operation. This works just like the old getdir()
 *    method.
 *
 * 2) [more advanced usage... we will use method 1]
 */
// This is emulating the system function readdir: https://linux.die.net/man/3/readdir
//
// However, unlike that function, it returns many directories at once instead of one-by-one. This
// function is a bit confusing to actually implement. A little guide on this function can be found
// at https://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/html/unclear.html.
//
// Important notes:
//   Don't use the path or offset parameters
//
//   The file-handle is located in the fi parameter (already handled for you)
//
//   The filler parameter is actually a _function_ that needs to be called with the first argument
//     the `buf` param given to readdir, second argument is the name of an item in the directory,
//     then NULL, and 0 (that NULL could actually be something, but NULL works fine).
//
//   If the filler function returns non-zero then -ENOMEM should be returned immediately.
int isofs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t _offset, struct fuse_file_info *fi)
{
    LOG("readdir(path=\"%s\", buf=%p, filler=%p, ..., fi=%p)\n", path, buf, filler, fi);

    const Record* directory = (const Record *)(uintptr_t)fi->fh;

    // TODO: Loop over every Record in the directory

        // TODO: Get the file name from the record and send it to the filler

        // TODO: Advance to the next record (make sure to account for end-of-sector issues)

 	return 0;
}

/** Release directory
 */
// This is emulating the system function closedir: https://linux.die.net/man/3/closedir
int isofs_releasedir(const char *path, struct fuse_file_info *fi)
{
    LOG("releasedir(path=\"%s\", fi=%p)\n", path, fi);
    // Nothing to do here actually
	return 0;
}


////////// File Reading/Writing ////////////////////////////////////////////////////////////////////

// This is our "file" object
typedef struct _isofs_file {
    uint8_t* data;  // the data for the file
    size_t size;    // the size of the file data (in bytes)
} isofs_file;

/** File open operation
 *
 * No creation (O_CREAT, O_EXCL) and by default also no truncation (O_TRUNC) flags will be passed to
 * open(). If an application specifies O_TRUNC, fuse first calls truncate() and then open(). Only if
 * 'atomic_o_trunc' has been specified and kernel version is 2.6.24 or later, O_TRUNC is passed on
 * to open.
 *
 * Unless the 'default_permissions' mount option is given, open should check if the operation is
 * permitted for the given flags. Optionally open may also return an arbitrary filehandle in the
 * fuse_file_info structure, which will be passed to all file operations.
 */
 // This is emulating the system call open: https://linux.die.net/man/2/open
int isofs_open(const char *path, struct fuse_file_info *fi)
{
    LOG("open(path=\"%s\", fi=%p)\n", path, fi);

    // TODO: If either write or read/write access is requested (available in fi->flags) then return -EACCESS
    if ((fi->flags & O_RDWR) || (fi->flags & O_WRONLY)) { return -EACCES; }

    // TODO: Get the directory record
    // In the case of an error, return -errno
    // If it is a directory, return -EISDIR, if it doesn't have R_OK access, return -EACCES
    const Record* record = NULL; // TODO: remove this NULL and this comment once you actually write the code

    // TODO: Allocate a new isofs_file object (if it cannot be allocated return -ENOMEM)
    isofs_file *f = NULL; // TODO: remove this NULL and this comment once you actually write the code

    // TODO: Fill in the fields of the structure so they can be used later

    // Set the file-handle as our file object
    fi->fh = (uintptr_t)f;
    return 0;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except on EOF or error, otherwise the
 * rest of the data will be substituted with zeroes. An exception to this is when the 'direct_io'
 * mount option is specified, in which case the return value of the read system call will reflect
 * the return value of this operation.
 */
// This is emulating the system call pread: https://linux.die.net/man/2/pread
//
// The goal here is to fill in `buf` with the next `size` bytes from the file (whose handle is in
// the fi parameter).
//
// NOTE: you should not use the path parameter.
int isofs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    LOG("read(path=\"%s\", buf=%p, size=%zu, offset=%lld, fi=%p)\n", path, buf, size, offset, fi);
    
    // Get our file "handle"
    isofs_file *f = (isofs_file*)(uintptr_t)fi->fh;

    // TODO: copy the necessary data to the buffer and return the number of bytes copied
    size_t n = 0; // TODO: remove this 0 and this comment once you actually write the code

    return n;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open file: all file descriptors are
 * closed and all memory mappings are unmapped.
 *
 * For every open() call there will be exactly one release() call with the same flags and file
 * descriptor. It is possible to have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the file. The return value of
 * release is ignored.
 */
// This is emulating the system call close: https://linux.die.net/man/2/close
int isofs_release(const char *path, struct fuse_file_info *fi)
{
    LOG("release(path=\"%s\", fi=%p)\n", path, fi);

    // Get our file "handle"
    isofs_file *f = (isofs_file*)(uintptr_t)fi->fh;

    // TODO: Free the file "handle"

    return 0;
}


////////// Main Function ///////////////////////////////////////////////////////////////////////////

// This sets up the set of operations to give to the FUSE library for our filesystem
struct fuse_operations isofs_oper = {
    // Setup and Tear-down
    .init = isofs_init,
    .destroy = isofs_destroy,

    // Basic Information Operations
    .statfs = isofs_statfs,
    .getattr = isofs_getattr,
    //.access = isofs_access,

    // Directories
    //.opendir = isofs_opendir,
    //.readdir = isofs_readdir,
    //.releasedir = isofs_releasedir,

    // Files
    //.open = isofs_open,
    //.read = isofs_read,
    //.release = isofs_release,

    // There are lots of other functions we aren't implementing since we are read-only...
    //    create, write, flush, fsync, ftruncate, truncate, chmod, utime, rename, mkdir, unlink, rmdir
    // Skipping many other operations since they don't make sense for ISO files:
    //    mknod, readlink, symlink, link, chown, {set,get,list,remove}xattr, lock, ...
};

int main(int argc, char *argv[])
{
    if ((getuid() == 0) || (geteuid() == 0)) {
    	fprintf(stderr, "running as root opens unacceptable security holes\n");
    	return 1;
    }

    // See which version of libraries we're using
    fprintf(stderr, "FUSE v%d.%d\n", FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);
    #ifdef __APPLE__
        fprintf(stderr, "OSXFUSE v%s\n", osxfuse_version());
    #endif

    // Perform some sanity checking on the command line: make sure there are enough arguments, and
    // that neither of the last two start with a hyphen (this will break if you actually have a
    // root_dir or mount_point whose name starts with a hyphen... but enh)
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-')) {
        fprintf(stderr, "usage:  %s [FUSE and mount options] iso_file mount_point\n", argv[0]);
        return 1;
    }

    // Get the ISO file path out of the argument list
    const char* filename = argv[argc-2];
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;

    // Load the ISO file
    ISO* iso = load_iso(filename);

    // Turn over control to FUSE
    umask(0); // makes things a bit easier later
    return fuse_main(argc, argv, &isofs_oper, iso);
}
