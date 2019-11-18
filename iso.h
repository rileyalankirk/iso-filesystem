/**
 * Types and constants used in ISO files. This also includes SUSP and Rock Ridge additions.
 */

#include <stdint.h>

#ifdef __GNUC__
#define PACKED __attribute__((__packed__))
#else
#define PACKED
#endif

// These are only defined for informational purposes
// They are really just chars but with hints about that acceptable values in them
// Strings of these are always padded on the right with spaces
typedef char char_a; // should only have space A-Z 0-9 _ ! " % & ' ( ) * + , - . / : ; < = > ?
typedef char char_d; // should only have space A-Z 0-9 _

// Little-endian (least significant bit) data types
// Used for most hardware nowadays, including Intel and ARM processors
typedef uint16_t uint16_lsb;
typedef uint32_t uint32_lsb;

// Big-endian (most significant bit) data types
// Would need to convert endian-ness to use, but we can just ignore them
typedef uint8_t uint16_msb[2];
typedef uint8_t uint32_msb[4];


////////////////////////////////////////////
////////// Path Table Definitions //////////
////////////////////////////////////////////
typedef struct PACKED _PathTableEntry {
    // See https://wiki.osdev.org/ISO_9660 for descriptions
    uint8_t length; // length of this entry in bytes (it includes the length byte itself)
    uint8_t extended_attr_length;
    uint32_t extent_location;
    uint16_t parent_directory;
    char_d directory_name[1]; // actually has all of the remaining bytes, if odd number of letters than ends with an extra null character ('\0')
} PathTableEntry;


////////////////////////////////////////
////////// Record Definitions //////////
////////////////////////////////////////

#define FILE_HIDDEN         0x01 // file doesn't need to be shown to user
#define FILE_DIRECTORY      0x02 // file is actually a directory
#define FILE_ASSOCIATED     0x04 // file is an "associated file"
#define FILE_EA_FORMAT      0x08 // file has file format information in the extended attributes
#define FILE_EA_PERMISSIONS 0x10 // file has owner and group permissions in extended attributes
#define FILE_ADDL_RECORDS   0x80 // file has additional records (likely means >4GiB data)

typedef struct PACKED _datetime {
    uint8_t year; // years since 1900
    uint8_t month; // 1 to 12
    uint8_t day; // 1 to 31
    uint8_t hour; // 0 to 23
    uint8_t minute; // 0 to 59
    uint8_t second; // 0 to 59
    uint8_t timezone; // offset from GMT-12 in 15 minute intervals
} datetime;

typedef struct PACKED _Record {
    // See https://wiki.osdev.org/ISO_9660 for descriptions
    // All fields starting with _ can be ignored (primarily the MSB copies of values)
    uint8_t length; // length of this record in bytes (it includes the length byte itself)
    uint8_t extended_attr_length;
    uint32_lsb extent_location;
    uint32_msb _extent_location;
    uint32_lsb extent_length;
    uint32_msb _extent_length;
    datetime datetime;
    uint8_t file_flags; // combination of the FILE_* flag constants
    uint8_t interleaved_unit_size; // 0 for non-interleaved files
    uint8_t interleaved_gap_size; // 0 for non-interleaved files
    uint16_lsb volume_sequence_number;
    uint16_msb _volume_sequence_number;
    uint8_t filename_length;
    char_d filename[1]; // actually has filename_length bytes
    uint8_t system_use_data[0];
} Record;


///////////////////////////////////////////////////
////////// Volume Descriptor Definitions //////////
///////////////////////////////////////////////////

// VolumeDescriptor ID value
#define CD001 "CD001"

// VolumeDescriptor type code values
#define VD_BOOT          0x00
#define VD_PRIMARY       0x01
#define VD_SUPPLEMENTARY 0x02
#define VD_PARTITION     0x03
#define VD_TERMINATOR    0xFF

typedef struct PACKED _dec_datetime {
    // Note: if all strings are spaces and time zone is 0 it means the date/time is not specified
    char_d year[4]; // string 1 to 9999
    char_d month[2]; // string 1 to 12
    char_d day[2]; // string 1 to 31
    char_d hour[2]; // string 0 to 23
    char_d minute[2]; // string 0 to 59
    char_d second[2]; // string 0 to 59
    char_d hundredths[2]; // string 0 to 99
    uint8_t timezone; // offset from GMT-12 in 15 minute intervals
} dec_datetime;

typedef struct PACKED _VolumeDescriptor {
    // See https://wiki.osdev.org/ISO_9660 for descriptions
    uint8_t type_code; // one of the VG_* constants
    char id[5]; // always "CD001" (which is in CD001 constant)
    uint8_t version; // always 1
} VolumeDescriptor;

typedef struct PACKED _BootVolumeDescriptor {
    // See https://wiki.osdev.org/ISO_9660 for descriptions
    VolumeDescriptor header; // has a VG_BOOT type code
    char_a boot_system_id[32];
    char_a boot_id[32];
    uint8_t boot_system[1977];
} BootVolumeDescriptor;

typedef struct PACKED _PrimaryVolumeDescriptor {
    // See https://wiki.osdev.org/ISO_9660 for descriptions
    // All fields starting with _ can be ignored (primarily the MSB copies of values)
    VolumeDescriptor header; // has a VG_PRIMARY type code
    uint8_t _unused1; // always 0
    char_a system_id[32]; // padded on right with spaces
    char_d volume_id[32]; // padded on right with spaces
    uint8_t _unused2[8]; // all zeros
    uint32_lsb volume_space_size;
    uint32_msb _volume_space_size;
    uint8_t _unused3[32]; // all zeros
    uint16_lsb volume_set_size;
    uint16_msb _volume_set_size;
    uint16_lsb volume_sequence_number;
    uint16_msb _volume_sequence_number;
    uint16_lsb logical_block_size;
    uint16_msb _logical_block_size;
    uint32_lsb path_table_size;
    uint32_msb _path_table_size;
    uint32_lsb path_table_loc;
    uint32_lsb path_table_opt_loc;
    uint32_msb _path_table_loc;
    uint32_msb _path_table_opt_loc;
    Record root_record;
    char_d volume_set_id[128];
    char_a publisher_id[128];
    char_a data_preparer_id[128];
    char_a application_id[128];
    char_d copyright_file_id[38];
    char_d abstract_file_id[36];
    char_d bibliographic_file_id[37];
    dec_datetime creation;
    dec_datetime modification;
    dec_datetime expiration;
    dec_datetime effective;
    uint8_t file_structure_version;
    uint8_t _unused4; // always 0
    uint8_t application_data[512];
    uint8_t _reserved[653];
} PrimaryVolumeDescriptor;


//////////////////////////////////////
////////// SUSP Definitions //////////
//////////////////////////////////////

// The SUSP signatures are 2-letter strings, made as 16-bit integers here for ease of use
#define SUSP_SP 0x5053 // SUSP indicator
#define SUSP_ST 0x5453 // SUSP terminator
#define SUSP_CE 0x4543 // Continuation area
#define SUSP_PD 0x4450 // Padding field
#define SUSP_ER 0x5245 // Extensions reference
#define SUSP_ES 0x5345 // Extension selector
#define SUSP_RR 0x5252 // Rock Ridge
#define SUSP_PX 0x5850 // Rock Ridge: POSIX file attributes
#define SUSP_PN 0x4E50 // Rock Ridge: Character/block device
#define SUSP_SL 0x4C53 // Rock Ridge: Symlink
#define SUSP_NM 0x4D4E // Rock Ridge: Alternate name
#define SUSP_CL 0x4C43 // Rock Ridge: Relocated directory link
#define SUSP_TF 0x4654 // Rock Ridge: Timestamps

// Header value in the SUSP SP field (which is BEEF in hex)
#define SUSP_SP_BEEF 0xEFBE

// RR SUSP field flags (these line up with possible SUSP fields)
#define SUSP_RR_PX 0x01
#define SUSP_RR_PN 0x02
#define SUSP_RR_SL 0x04
#define SUSP_RR_NM 0x08
#define SUSP_RR_CL 0x10
#define SUSP_RR_PL 0x20 // unknown usage
#define SUSP_RR_RE 0x40 // unknown usage
#define SUSP_RR_TF 0x80

// RR SUSP symlink and alternate name flags
#define SUSP_RR_CONTINUE 0x01
#define SUSP_RR_CURRENT  0x02
#define SUSP_RR_PARENT   0x04
#define SUSP_RR_ROOT     0x08 // symlink only

// RR SUSP Timestamp flags
#define SUSP_TF_CREATION     0x01
#define SUSP_TF_MODIFICATION 0x02
#define SUSP_TF_ACCESS       0x04
#define SUSP_TF_ATTRIBUTES   0x08
#define SUSP_TF_BACKUP       0x10
#define SUSP_TF_EXPIRATION   0x20
#define SUSP_TF_EFFECTIVE    0x40
#define SUSP_TF_LONG_FORM    0x80

typedef struct PACKED _susp_SP { // SUSP indicator
    uint16_t beef;
    uint8_t len_skp;
} susp_SP;

typedef struct PACKED _susp_ST { // SUSP terminator
} susp_ST;

typedef struct PACKED _susp_CE { // Continuation area
    uint32_lsb location;
    uint32_msb _location;
    uint32_lsb offset;
    uint32_msb _offset;
    uint32_lsb length;
    uint32_msb _length;
} susp_CE;

typedef struct PACKED _susp_PD { // Padding field
    uint8_t _unused[0];
} susp_PD;

typedef struct PACKED _susp_ER { // Extensions reference
    uint8_t len_id;
    uint8_t len_des;
    uint8_t len_src;
    uint8_t ext_ver;
    uint8_t ext_id[0]; // len_id bytes
    uint8_t ext_des[0]; // len_des bytes
    uint8_t ext_src[0]; // len_src bytes
} susp_ER;

typedef struct PACKED _susp_ES { // Extension selector
    uint8_t ext_seq;
} susp_ES;

typedef struct PACKED _susp_RR {  // Rock Ridge
    uint8_t flags;
} susp_RR;

typedef struct PACKED _susp_PX { // Rock Ridge: POSIX file attributes
    uint32_lsb mode;
    uint32_msb _mode;
    uint32_lsb nlinks;
    uint32_msb _nlinks;
    uint32_lsb uid;
    uint32_msb _uid;
    uint32_lsb gid;
    uint32_msb _gid;
    uint32_lsb ino; // not always there...
    uint32_msb _ino; // not always there...
} susp_PX;

typedef struct PACKED _susp_PN { // Rock Ridge: Character/block device
    uint32_lsb high;
    uint32_msb _high;
    uint32_lsb low;
    uint32_msb _low;
} susp_PN;

typedef struct PACKED _susp_SL { // Rock Ridge: Symlink
    uint8_t flags;
    struct {
        uint8_t flags;
        uint8_t length;
        uint8_t content[0];
    } components[1];
} susp_SL;

typedef struct PACKED _susp_NM { // Rock Ridge: Alternate name
    uint8_t flags;
    char name[0];
} susp_NM;

typedef struct PACKED _susp_CL { // Rock Ridge: Relocated directory link
    uint32_lsb child_loc;
    uint32_msb _child_loc;
} susp_CL;

typedef struct PACKED _susp_TF { // Rock Ridge: Timestamps
    uint8_t flags;
    union {
        datetime timestamps[0];
        dec_datetime timestamps_long[0];
    };
} susp_TF;

typedef struct PACKED _susp_field {
    uint16_t signature; // actually a 2 character string, but this is easier to work with
    uint8_t length;
    uint8_t version;
    union {
        susp_SP SP;
        susp_ST ST;
        susp_CE CE;
        susp_PD PD;
        susp_ER ER;
        susp_ES ES;
        susp_RR RR;
        susp_PX PX;
        susp_PN PN;
        susp_SL SL;
        susp_NM NM;
        susp_CL CL;
        susp_TF TF;
    };
} susp_field;
