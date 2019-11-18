/**
 * Utilities for working with ISO files. You do not need to modify anything in this file, but you
 * will need to call most functions defined in this file. You may also look at them for some
 * inspiration.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>

/**
 * Represents an ISO file loaded from disk.
 */
typedef struct _ISO {
    int fd; // file descriptor of the ISO file, this is the value returned by open()
    uint8_t* raw; // the is the actual data in memory, the pointer is as returned by mmap()
    size_t size; // size of the file (and the memory), obtained with fstat on the file descriptor
    PrimaryVolumeDescriptor* pvd; // the primary description of the ISO volume
} ISO;

/**
 * An array of names representing the parts of a path. This only supports up to 32 parts.
 */
typedef struct _path_names {
    size_t count; // the number of names in `names`
    char* names[32]; // each of the names in the path
    bool trailing_slash; // if the path has a trailing /
} path_names;

/**
 * Frees a path_names pointer that was returned by get_path_names().
 */
void free_path_names(path_names* names)
{
    for (int i = 0; i < names->count; i++) {
        free(names->names[i]);
    }
    free(names);
}

/**
 * Get the parts that make up a path, returning a path_names pointer that must be freed with
 * free_path_names().
 * 
 * Some examples:
 *  * If given "/hello/world" the path_names will have 2 names: "hello" and "world"
 *  * If given "/hi/there/world/" the path_names will have 3 names: "hi", "there", "world" and the
 *    boolean `trailing_slash` will be true
 *  * If given "/" the path_names will have 0 names and `trailing_slash` will be true
 * 
 * If there are any problems, NULL is returned and errno is set appropriately.
 */
path_names* get_path_names(const char* path)
{
    if (path[0] != '/') { errno = ENOENT; return NULL; }

    // Allocate memory
    path_names* names = (path_names*)malloc(sizeof(path_names));
    if (!names) { return NULL; }
    names->count = 0;

    // Single / is handled special
    if (path[1] == '\0') {
        names->trailing_slash = true;
        return names;
    }

    // Go through each part
    size_t start = 1;
    while (names->count < 32) {
        const char* slash = strchr(path+start, '/'); // get pointer to the next /
        size_t end = slash ? slash - path : strlen(path); // compute index of next / or end
        if (end - start > 255) { // only supports up to 255 letter file names
            free_path_names(names);
            errno = ENAMETOOLONG;
            return NULL;
        }
        names->names[names->count] = strndup(path + start, end - start); // copy the file name
        if (!names->names[names->count]) { // allocation error
            free_path_names(names);
            return NULL;
        }
        names->count++;
        start = end + 1; // skip the / and use as the next start
        if (!slash || path[start] == '\0') { // all done
            names->trailing_slash = !!slash;
            return names;
        }
    }

    // Too many parts
    free_path_names(names);
    errno = ENAMETOOLONG;
    return NULL;
}

/**
 * Converts a datetime value to a POSIX time_t value.
 */
static time_t convert_datetime(const datetime* dt)
{
    struct tm time = {
        // my TODO: timezone information?
        .tm_year=dt->year,
        .tm_mon=dt->month-1,
        .tm_mday=dt->day,
        .tm_hour=dt->hour,
        .tm_min=dt->minute,
        .tm_sec=dt->second,
    };
    return mktime(&time);
}

/**
 * Converts a dec_datetime value to a POSIX time_t value.
 */
static time_t convert_dec_datetime(const dec_datetime* dt)
{
    struct tm time = {
        // my TODO: timezone information?
        .tm_year=atoi(dt->year)-1900,
        .tm_mon=atoi(dt->month)-1,
        .tm_mday=atoi(dt->day),
        .tm_hour=atoi(dt->hour),
        .tm_min=atoi(dt->minute),
        .tm_sec=atoi(dt->second),
    };
    return mktime(&time);
}


/**
 * Gets a SUSP field from the data while performing some minimal consistency checks.
 * Returns NULL if not a valid SUSP field is not at the beginning of the data.
 * 
 * NOTE: You never need to use this function! It is used by the read_rock_ridge_data() function.
 */
static const susp_field* get_susp_field(const uint8_t* data, size_t length)
{
    if (length < 4) { return NULL; }
    const susp_field* susp = (const susp_field*)data;
    if (length < susp->length) { return NULL; }
    switch (susp->signature) {
        case SUSP_SP: // SUSP indicator
            if (susp->length != sizeof(susp_SP)+4 || susp->SP.beef != SUSP_SP_BEEF) { return NULL; }
            break;
        case SUSP_ST: // SUSP terminator
            if (susp->length != sizeof(susp_ST)+4) { return NULL; }
            break;
        case SUSP_CE: // Continuation area
            if (susp->length != sizeof(susp_CE)+4) { return NULL; }
            break;
        case SUSP_ER: // Extensions reference
            if (susp->length < sizeof(susp_ER) + 4 || susp->length != sizeof(susp_ER) + 4 + susp->ER.len_id + susp->ER.len_des + susp->ER.len_src) { return NULL; }
            break;
        case SUSP_ES: break; // Extension selector
            if (susp->length != sizeof(susp_ES)+4) { return NULL; }
            break;
        case SUSP_RR: break; // Rock Ridge
            if (susp->length != sizeof(susp_RR)+4) { return NULL; }
            break;
        case SUSP_PX: break; // Rock Ridge: POSIX file attributes
            if (susp->length != sizeof(susp_PX)+4 && susp->length != sizeof(susp_PX) + 4 - 2*sizeof(uint32_t)) { return NULL; }
            break;
        case SUSP_PN: break; // Rock Ridge: Character/block device
            if (susp->length != sizeof(susp_PN)+4) { return NULL; }
            break;
        case SUSP_SL: break; // Rock Ridge: Symlink
            if (susp->length >= sizeof(susp_SL)+4) { return NULL; }
            break;
        case SUSP_NM: break; // Rock Ridge: Alternate name
            if (susp->length >= sizeof(susp_NM)+4) { return NULL; }
            break;
        case SUSP_CL: break; // Rock Ridge: Relocated directory link
            if (susp->length == sizeof(susp_CL)+4) { return NULL; }
            break;
        case SUSP_TF: break; // Rock Ridge: Timestamps
            if (susp->length >= sizeof(susp_TF)+4) { return NULL; }
            break;
        default: break; // padding and unknown/ignored fields
    }
    return susp;
}

#define RR_HAS_STAT         0x01 // the mode, nlinks, uid, and gid fields are valid
#define RR_HAS_INO          0x02 // the ino field is valid
#define RR_HAS_FILENAME     0x04 // the filename field is valid
#define RR_HAS_CREATION     0x08 // the creation field is valid
#define RR_HAS_MODIFICATION 0x10 // the modification field is valid
#define RR_HAS_ACCESS       0x20 // the access field is valid

typedef struct _RRExtraData {
    uint8_t flags;       // Which other fields are filled out - some combination of RR_HAS_*
    uint32_t mode;       // The mode of the file
    uint32_t nlinks;     // The number of links to the file
    uint32_t uid, gid;   // The user and group ids of the owner of the file
    uint32_t ino;        // The inode value of the file
    char filename[256];  // The filename
    time_t creation;     // The creation time
    time_t modification; // The last modification time
    time_t access;       // The last access time
} RRExtraData;

/**
 * Reads the extra Rock Ridge data from the supplemental data section of a directory record. This
 * may not find any information or it may find lots of information. If fills out the given
 * RRExtraData structure with the values it finds and sets the RR_HAS_* flags indicating they are
 * found.
 */
static void read_rock_ridge_data(const ISO* iso, const Record* record, RRExtraData* rr)
{
    // Clear some of the fields in the RR data
    rr->flags = 0;
    rr->filename[0] = 0;

    // Make sure the Record is within the ISO memory
    if (iso->raw+iso->size <= (uint8_t*)record || iso->raw+iso->size-record->length < (uint8_t*)record) { return; }

    // Get the supplemental data region
    if (record->length == 0) { return; }
    const uint8_t* data = ((uint8_t*)&record->filename) + record->filename_length + (1 - record->filename_length % 2);
    size_t length = ((uint8_t*)record) + record->length - data;

    // Go through the data looking at the SUSP fields
    size_t offset = 0;
    while (offset < length) {
        const susp_field* susp = get_susp_field(data+offset, length-offset);
        if (!susp) { return; } // Invalid SUSP data
        if (susp->signature == SUSP_SP) { offset += susp->SP.len_skp; } // only seems to be on the root record?
        else if (susp->signature == SUSP_CE) {
            size_t data_offset = susp->CE.location*iso->pvd->logical_block_size + susp->CE.offset;
            data = iso->raw + data_offset;
            offset = 0;
            length = susp->CE.length;
            // Make sure that the new memory block is within the ISO memory
            if (data_offset + length > iso->size) { return; }
        }
        else if (susp->signature == SUSP_PX) {
            // POSIX file attributes
            rr->flags |= RR_HAS_STAT;
            rr->mode = susp->PX.mode;
            rr->nlinks = susp->PX.nlinks;
            rr->uid = susp->PX.uid;
            rr->gid = susp->PX.gid;
            if (susp->length == (sizeof(susp_PX) + 4)) {
                rr->flags |= RR_HAS_INO;
                rr->ino = susp->PX.ino;
            }
        } else if (susp->signature == SUSP_NM) {
            // Alternate name
            if (susp->NM.flags == SUSP_RR_CURRENT) {
                rr->flags |= RR_HAS_FILENAME;
                strcpy(rr->filename, ".");
            } else if (susp->NM.flags == SUSP_RR_PARENT) {
                rr->flags |= RR_HAS_FILENAME;
                strcpy(rr->filename, "..");
            } else if (susp->NM.flags == SUSP_RR_CONTINUE || susp->NM.flags == 0) {
                rr->flags |= RR_HAS_FILENAME;
                size_t name_length = susp->length - 4 - sizeof(susp_NM);
                strncpy(rr->filename, susp->NM.name, name_length);
                rr->filename[name_length] = 0; // make sure it is null-terminated
            }
        } else if (susp->signature == SUSP_TF) {
            // Timestamps
            bool dec = susp->TF.flags & SUSP_TF_LONG_FORM;
            size_t tf_offset = offset + sizeof(susp_TF) + 4;
            size_t ts_size = dec ? sizeof(dec_datetime) : sizeof(datetime);
            // Creation time
            if (tf_offset+ts_size < length && susp->TF.flags & SUSP_TF_CREATION) {
                rr->flags |= RR_HAS_CREATION;
                rr->creation = dec ? convert_dec_datetime((dec_datetime*)(data+tf_offset)) : 
                                convert_datetime((datetime*)(data+tf_offset));
                tf_offset += ts_size;
            }
            // Modification time
            if (tf_offset+ts_size < length && susp->TF.flags & SUSP_TF_MODIFICATION) {
                rr->flags |= RR_HAS_MODIFICATION;
                rr->modification = dec ? convert_dec_datetime((dec_datetime*)(data+tf_offset)) : 
                                convert_datetime((datetime*)(data+tf_offset));
                tf_offset += ts_size;
            }
            // Access time
            if (tf_offset+ts_size < length && susp->TF.flags & SUSP_TF_ACCESS) {
                rr->flags |= RR_HAS_ACCESS;
                rr->access = dec ? convert_dec_datetime((dec_datetime*)(data+tf_offset)) : 
                                convert_datetime((datetime*)(data+tf_offset));
                tf_offset += ts_size;
            }
            // Skip all other timestamps...
        }

        // Ignored: ER, ES, (both of those part of SUSP), PN, SL, CL (those three part of Rock Ridge)

        // Advanced and possibly terminate
        if (susp->signature != SUSP_CE) { offset += susp->length; }
        if (susp->length == 0 || susp->signature == SUSP_ST) { return; }
    }
}

/**
 * Gets the filename from a record. This is a bit complicated due to the ISO file format. This
 * function first checks to see if there is a Rock Ridge alternate file name and uses that if
 * available. After that it looks at the filename field in the record itself, translating the
 * current and parent directory indicators and also removes any version information.
 * 
 * The given file name must be at least 256 characters long.
 */
void get_record_filename(const ISO* iso, const Record* record, char filename[256])
{
    RRExtraData rr;
    read_rock_ridge_data(iso, record, &rr);
    if (rr.flags & RR_HAS_FILENAME) {
        strcpy(filename, rr.filename);
    } else if (record->filename[0] == 0) {
        strcpy(filename, ".");
    } else if (record->filename[0] == 1) {
        strcpy(filename, "..");
    } else {
        strncpy(filename, record->filename, record->filename_length);
        // Make sure it is null-terminated
        filename[record->filename_length] = 0;
        // If it is a filename with a version remove the version
        char* semicolon = strrchr(filename, ';');
        if (semicolon) { semicolon[0] = 0; }
        // Remove a trailing period if it is there
        size_t length = strlen(filename);
        if (filename[length-1] == '.') { filename[length-1] = 0; }
    }
}

/**
 * Gets the approximate number of files by returning the number of entries in the path table. This
 * value is approximate in several circumstances, in particular this value will never be at more
 * than 65,536 even if there are more files than that. It may also stop under other circumstances
 * and report only a partial value.
 */
size_t get_number_of_files(const ISO* iso)
{
    size_t count = 0;
    size_t bs = iso->pvd->logical_block_size;
    size_t offset = iso->pvd->path_table_loc*bs;
    size_t end = offset + iso->pvd->path_table_size;
    while (offset < end && offset < iso->size) {
        count++;
        // First byte of path table entry is length of name, but needs to be rounded up to an even number
        offset += 8 + iso->raw[offset] + iso->raw[offset] % 2;
    }
    return count;
}
