#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zlib.h"

#define IN_BYTES   599113
#define OUT_BYTES  1413124

/*
 * Inflates firmware image extracted from S2ISPFIRMWARE segment of AppleCameraInterface
 * Tested only with version 5.23.0 of com.apple.driver.AppleCameraInterface from OS X 10.10.1
 */

int main(int argc, char** argv) {
  unsigned char *buf_in, *buf_out;
  size_t buf_in_size;
  FILE *ip, *op;
  z_stream strm;

  int ret = 0;

  if (argc != 3) {
    printf("Usage: decompress <input> <output>");
    ret = -1;
    goto end;
  }

  if (!(ip = fopen(argv[1], "rb"))) {
    printf("Error: Cannot open %s!", argv[1]);
    ret = -1;
    goto end_fp;
  }

  fseek(ip, 0, SEEK_END);
  buf_in_size = ftell(ip);

  if (!(buf_in = malloc(buf_in_size))) {
    printf("Error: Cannot allocate input buffer of size %zu!", buf_in_size);
    ret = -1;
    goto end_in;
  }

  rewind(ip);
  if (fread(buf_in, sizeof(*buf_in), buf_in_size / sizeof(*buf_in), ip) != buf_in_size / sizeof(*buf_in)) {
    perror(NULL);
    printf("Error: Cannot read %zu bytes!", buf_in_size);
    ret = -1;
    goto end_in;
  }

  memset(&strm, Z_NULL, sizeof(strm));
  strm.avail_in = IN_BYTES;
  // zlib and gzip decoding with automatic header detection
  if (inflateInit2_(&strm, 15 + 32, "1.2.3", 112) != Z_OK) {
    printf("Error: Cannot initialize inflate!");
    ret = -1;
    goto end_in;
  }

  if (!(buf_out = malloc(OUT_BYTES))) {
    printf("Error: Cannot allocate output buffer!");
    ret = -1;
    goto end_out;
  }

  strm.next_in = buf_in;
  strm.avail_out = OUT_BYTES;
  strm.next_out = buf_out;

  if (!strm.avail_in || inflate(&strm, Z_NO_FLUSH) != Z_STREAM_END || strm.avail_out) {
    printf("Error: Deflate not successful!");
    ret = -1;
    goto end_inflate;
  }

  if (!(op = fopen(argv[2], "wb"))) {
    printf("Error: Cannot open %s!", argv[2]);
    ret = -1;
    goto end;
  }

  if (fwrite(buf_out, sizeof(*buf_out), OUT_BYTES / sizeof(*buf_out), op) != OUT_BYTES / sizeof(*buf_out)) {
    printf("Error: Cannot write to output file!");
    ret = -1;
  }

  fclose(op);

end_inflate:
  inflateEnd(&strm);

end_out:
  free(buf_out);

end_in:
  free(buf_in);

end_fp:
  fclose(ip);

end:
  return ret;
}
