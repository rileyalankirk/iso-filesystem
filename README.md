This project creates a read-only FUSE-based filesystem that gets its data from a CD image in an "ISO" file.
Technically, it is an ISO-9660 filesystem.
In this project, there are many details of the ISO-9660 filesystem that are ignored.
It is definitely a strange format (for example, every value that isn’t a single byte is stored twice: once in little-endian and once in big-endian), but we focus on the important parts. 
The project explores how a filesystem is structured in memory using low-level features (such as file descriptors and memory mapping) and implements the API of a filesystem.