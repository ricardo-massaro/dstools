/* dump_hkx.c */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bhd.h"
#include "dcx.h"
#include "hkx.h"
#include "reader.h"
#include "dump.h"
#include "util.h"

#define MODE_LIST     0
#define MODE_EXTRACT  1
#define MODE_DUMP     2

static int write_geometry(const char *in_filename, struct HKX_GEOMETRY *g)
{
  size_t filename_len = strlen(in_filename);
  char *filename = malloc(filename_len + 4 + 1);
  if (! filename) {
    printf("OUT OF MEMORY writing geometry\n");
    return 1;
  }
  strcpy(filename, in_filename);
  strcat(filename, ".obj");

  int ret = hkx_write_obj(filename, g);
  free(filename);
  return ret;
}

static void extract_file(void *data, size_t size, const char *filename, struct HKX_GEOMETRY *g)
{
  if (hkx_read_geometry(g, data, size) != 0)
    printf("OUT OF MEMORY for geometry\n");
}

static void dump_hkx(void *data, size_t size, const char *filename)
{
  uint32_t off = 8;

  printf("=============================================================================\n");
  printf("== %s\n", filename);
  printf("=============================================================================\n");
  
  while (off < size) {
    uint32_t chunk_size = get_u32_be(data, off) & 0x00ffffff;
    printf("\n");
    printf("%08x %.4s len=%u (0x%x)\n", off, (char *) data + off + 4, chunk_size - 8, chunk_size - 8);
    dump_mem((char *) data + off + 8, chunk_size - 8);
    off += chunk_size;
  }
  if (off != size)
    printf("warning: off != size (0x%08x != 0x%08x)\n", off, (uint32_t) size);
  printf("\n");
}

static int process_hkx(void *data, size_t size, const char *filename, int mode, struct HKX_GEOMETRY *g)
{
  switch (mode) {
  case MODE_LIST:
    printf("%8lu %s\n", (unsigned long) size, filename);
    return 0;
    
  case MODE_EXTRACT:
    extract_file(data, size, filename, g);
    return 0;
    
  case MODE_DUMP:
    dump_hkx(data, size, filename);
    return 0;
  }
  return 1;
}

static int process_single_file(const char *filename, int mode)
{
  char magic[4];
  if (read_file_data(filename, 0, magic, 4) != 0) {
    printf("Can't open '%s'\n", filename);
    return 1;
  }

  size_t size;
  void *data;
  if (memcmp(magic, "DCX", 4) == 0) {
    data = dcx_read_file(filename, &size);
  } else {
    data = read_file(filename, &size);
  }
  if (! data) {
    printf("Can't open '%s'\n", filename);
    return 1;
  }
  
  struct HKX_GEOMETRY g;
  hkx_init_geometry(&g);
  process_hkx(data, size, filename, mode, &g);
  write_geometry(filename, &g);
  hkx_free_geometry(&g);
  
  free(data);
  return 0;
}

static int process_bhd(const char *filename, int mode)
{
  struct BHD_FILE f;
  if (bhd_open(&f, filename) != 0) {
    printf("Can't open '%s'\n", filename);
    return 1;
  }

  struct HKX_GEOMETRY g;
  hkx_init_geometry(&g);

  for (uint32_t file_num = 0; file_num < f.n_files; file_num++) {
    char *hkx_filename;
    size_t comp_size;
    char *comp_data = bhd_get_file(&f, file_num, &comp_size, &hkx_filename);

    size_t size;
    void *data = dcx_read_mem(comp_data, comp_size, &size);
    if (! data) {
      printf("Can't inflate '%s'\n", hkx_filename);
    } else {
      process_hkx(data, size, hkx_filename, mode, &g);
      free(data);
    }
  }
  
  write_geometry(filename, &g);
  hkx_free_geometry(&g);
  bhd_close(&f);
  return 0;
}

static int read_cmdline(int argc, char *argv[])
{
  if (argc != 3) {
    printf("USAGE: hkxtool commands file.hkx\n");
    printf("       hkxtool commands file.hkxbhd\n");
    printf("\n");
    printf("Extract and list the contents of hkx or hkxbhd/hkxbnd files.");
    printf("\n");
    printf("use one of these commands:\n");
    printf("  l    list files\n");
    printf("  d    dump files (hexdump)\n");
    printf("  x    extract files\n");
    exit(1);
  }

  int mode = -1;
  for (char *p = argv[1]; *p != '\0'; p++) {
    switch (*p) {
    case 'x': mode = MODE_EXTRACT; break;
    case 'l': mode = MODE_LIST; break;
    case 'd': mode = MODE_DUMP; break;
    default:
      printf("Invalid command: '%c'\n", *p);
      exit(1);
    }
  }

  if (mode < 0) {
    printf("At least one of 'x', 'l', 'd' is required\n");
    exit(1);
  }
  return mode;
}

int main(int argc, char *argv[])
{
  int mode = read_cmdline(argc, argv);
  char *file = argv[2];

  char header[8];
  if (read_file_data(file, 0, header, 8) != 0) {
    printf("Can't open '%s'\n", file);
    return 1;
  }

  if (memcmp(header, "BHF3", 4) == 0)
    return process_bhd(file, mode);
  if (memcmp(header + 4, "TAG0", 4) == 0)
    return process_single_file(file, mode);

  printf("Unknown format in '%s'\n", file);
  return 1;
}
