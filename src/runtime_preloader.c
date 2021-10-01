#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ftw.h>

#include <sys/mman.h>

#if HAVE_LIBARCHIVE3 == 1 // CentOS
# include <archive3.h>
# include <archive_entry3.h>
#else // other systems
# include <archive.h>
# include <archive_entry.h>
#endif

#include <appimage/appimage_shared.h>
#include <linux/limits.h>

char runtime_preloader_directory[PATH_MAX] = {0};

static int remove_file(const char *pathname, const struct stat *sbuf, int type, struct FTW *ftwb)
{
  if(remove(pathname) < 0)
  {
    perror("ERROR: remove");
    return -1;
  }
  return 0;
}

static int
copy_data(struct archive *ar, struct archive *aw)
{
  int r;
  const void *buff;
  size_t size;
  int64_t offset;

  for (;;) {
    r = archive_read_data_block(ar, &buff, &size, &offset);
    if (r == ARCHIVE_EOF)
      return (ARCHIVE_OK);
    if (r != ARCHIVE_OK) {
      fprintf(stderr, "archive_read_data_block failed: %s\n", archive_error_string(ar));
      return (r);
    }
    r = archive_write_data_block(aw, buff, size, offset);
    if (r != ARCHIVE_OK) {
      fprintf(stderr, "archive_write_data_block failed: %s\n", archive_error_string(ar));
      return (r);
    }
  }
}

static int extract(struct archive* a, const char* target_directory)
{
  struct archive *ext;
  struct archive_entry *entry;
  int r;

  ext = archive_write_disk_new();

#ifndef NO_LOOKUP
  archive_write_disk_set_standard_lookup(ext);
#endif

  for (;;) {
    char dest_path[PATH_MAX];
    r = archive_read_next_header(a, &entry);
    if (r == ARCHIVE_EOF)
      break;
    if (r != ARCHIVE_OK) {
      fprintf(stderr, "archive_read_next_header failed: %s\n", archive_error_string(a));
      return 1;
    }
    snprintf(dest_path, sizeof(dest_path), "%s/%s", target_directory, archive_entry_pathname_utf8(entry));
    archive_entry_set_pathname(entry, dest_path);
    r = archive_write_header(ext, entry);
    if (r != ARCHIVE_OK) {
      fprintf(stderr, "archive_write_header failed[%s]: %s\n", dest_path, archive_error_string(ext));
    }
    else {
      r = copy_data(a, ext);
      if (r != ARCHIVE_OK) {
        fprintf(stderr, "copy_data failed: %d\n", r);
        break;
      }
    }
  }

  archive_write_close(ext);
  archive_write_free(ext);

  return 0;
}


/**
 * runtime_preloader
 *
 * @param appimage_path
 * @return
 *  0     : Then run the mount
 *  143   : Exit without mount
 *  other : exit with error code
 */
int runtime_preloader_exec(const char* appimage_path, const char* argv0_path) {
  int rc = 0;

  unsigned long offset = 0;
  unsigned long length = 0;

  int fd = 0;
  const char* map = NULL;
  char cmd[PATH_MAX] = {0};
  char fullpath[PATH_MAX] = {0};

  ssize_t slen;

  if ((slen = readlink(appimage_path, fullpath, sizeof(fullpath) - 1)) < 0) {
    fprintf(stderr, "Error getting realpath for %s\n", appimage_path);
    return EXIT_FAILURE;
  }
  fullpath[slen] = '\0';

  // libarchive status int -- passed to called functions
  int r;
  struct archive *a = NULL;

  appimage_get_elf_section_offset_and_length(appimage_path, ".aimg_pre_tar", &offset, &length);
  if (!offset || !length) {
    return 0;
  }

  do {
    snprintf(runtime_preloader_directory, sizeof(runtime_preloader_directory), "%s/.aipXXXXXX", P_tmpdir);
    if (!mkdtemp(runtime_preloader_directory)) {
      fprintf(stderr, "mktemp failed: %d\n", errno);
      rc = errno;
      break;
    }

    fd = open(appimage_path, O_RDONLY);
    if (fd < 0) {
      fprintf(stderr, "open failed: %d\n", errno);
      rc = errno;
      break;
    }

    map = (const char *) mmap(NULL, offset + length, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
      fprintf(stderr, "mmap failed(offset=%lu, length=%lu): %d\n", offset, length, errno);
      rc = errno;
      break;
    }

    a = archive_read_new();
    archive_read_support_format_tar(a);
    if ((r = archive_read_open_memory(a, map + offset, length)) != ARCHIVE_OK) {
      fprintf(stderr, "archive_read_open_memory failed: %d\n", errno);
      archive_read_free(a);
      rc = -1;
      break;
    }

    extract(a, runtime_preloader_directory);
    archive_read_close(a);
    archive_read_close(a);

    setenv( "ARGV0", argv0_path, 1 );
    setenv( "APPIMAGE", fullpath, 1);
    setenv("APPIMAGE_PREDIR", runtime_preloader_directory, 1);
    snprintf(cmd, sizeof(cmd), "%s/start", runtime_preloader_directory);
    rc = system(cmd);
  } while (0);

  if (a) {
    archive_free(a);
  }

  if (map && map != MAP_FAILED) {
    munmap((void*) map, length);
  }
  if (fd) {
    close(fd);
  }

  return rc;
}

void runtime_preloader_cleanup() {
  if (runtime_preloader_directory[0]) {
    nftw(runtime_preloader_directory, remove_file, 10, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
    rmdir(runtime_preloader_directory);
  }
}
