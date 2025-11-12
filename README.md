# kres

kres is a work in progress, and experimental archive format.

kres is designed and optimized for random access to in archive sections.
This allows for reading only the data requested from the archive without loading the whole thing into memory.

Unfortunately in this early prototype stage, this advantage is offset by having to keep the whole archive in memory to
construct it.

The format does not have any internal compression support
but does allow for a user data section,
if compression is required the user is expected to
compress the data themselves, store info about the compression
in the user data and decompress when reading.