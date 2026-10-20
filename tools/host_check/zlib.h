// Host stand-in for <zlib.h> - the sandbox has libz.so but no headers.
#ifndef WRESTLEFEST_HOST_STUB_ZLIB
#define WRESTLEFEST_HOST_STUB_ZLIB
#include <cstddef>
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef struct z_stream_s {
    Bytef *next_in;
    uInt avail_in;
    uLong total_in;
    Bytef *next_out;
    uInt avail_out;
    uLong total_out;
    char *msg;
    void *state;
    void *zalloc;
    void *zfree;
    void *opaque;
    int data_type;
    uLong adler;
    uLong reserved;
} z_stream;
#define MAX_WBITS 15
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_FINISH 4
extern "C" int inflateInit2_(z_stream *strm, int windowBits, const char *version, int stream_size);
extern "C" int inflate(z_stream *strm, int flush);
extern "C" int inflateEnd(z_stream *strm);
#define inflateInit2(strm, windowBits) \
    inflateInit2_((strm), (windowBits), "1.2.11", (int)sizeof(z_stream))
#endif
