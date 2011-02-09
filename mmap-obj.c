#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "whistlepig.h"

RAISING_STATIC(validate(mmap_obj_header* h, const char* magic)) {
  if(strncmp(magic, h->magic, MMAP_OBJ_MAGIC_SIZE)) RAISE_ERROR("invalid magic (expecting %s)", magic);
  if(h->size == (uint32_t)-1) RAISE_ERROR("invalid size %d", h->size);
  return NO_ERROR;
}

wp_error* mmap_obj_create(mmap_obj* o, const char* magic, const char* pathname, uint32_t initial_size) {
  o->fd = open(pathname, O_EXCL | O_CREAT | O_RDWR, 0640);
  if(o->fd == -1) RAISE_SYSERROR("cannot create %s", pathname);

  uint32_t size = initial_size + sizeof(mmap_obj_header);
  DEBUG("creating %s with %u + %u = %u bytes for %s object", pathname, initial_size, sizeof(mmap_obj_header), size, magic);
  lseek(o->fd, size - 1, SEEK_SET);
  ssize_t num_bytes = write(o->fd, "", 1);
  if(num_bytes == -1) RAISE_SYSERROR("write");
  o->header = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, o->fd, 0);
  if(o->header == MAP_FAILED) RAISE_SYSERROR("mmap");
  strncpy(o->header->magic, magic, MMAP_OBJ_MAGIC_SIZE);
  o->header->size = initial_size;
  DEBUG("created new %s object with %u bytes", magic, size);

  return NO_ERROR;
}

wp_error* mmap_obj_load(mmap_obj* o, const char* magic, const char* pathname) {
  DEBUG("trying to load %s object from %s", magic, pathname);
  o->fd = open(pathname, O_RDWR, 0640);
  if(o->fd == -1) RAISE_SYSERROR("cannot open %s", pathname);

  // load header
  o->header = mmap(NULL, sizeof(mmap_obj_header), PROT_READ | PROT_WRITE, MAP_SHARED, o->fd, 0);
  if(o->header == MAP_FAILED) RAISE_SYSERROR("header mmap");
  DEBUG("loaded header of %u bytes for %s object", sizeof(mmap_obj_header), magic);

  RELAY_ERROR(validate(o->header, magic));

  uint32_t size = o->header->size + sizeof(mmap_obj_header);
  DEBUG("full size is %u bytes (including %u-byte header)", size, sizeof(mmap_obj_header));
  if(munmap(o->header, sizeof(mmap_obj_header)) == -1) RAISE_SYSERROR("munmap");

  o->header = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, o->fd, 0);
  if(o->header == MAP_FAILED) RAISE_SYSERROR("full mmap");
  DEBUG("loaded full %s object of %u bytes", magic, size);

  return NO_ERROR;
}

wp_error* mmap_obj_resize(mmap_obj* o, uint32_t data_size) {
  DEBUG("going to expand from %u to %u bytes. current header is at %p", o->header->size, data_size, o->header);

  if(munmap(o->header, sizeof(mmap_obj_header) + o->header->size) == -1) RAISE_SYSERROR("munmap");
  uint32_t size = data_size + sizeof(mmap_obj_header);

  lseek(o->fd, size - 1, SEEK_SET);
  ssize_t num_bytes = write(o->fd, "", 1);
  if(num_bytes == -1) RAISE_SYSERROR("write");
  //lseek(fd, 0, SEEK_SET); // not necessary!
  o->header = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, o->fd, 0);
  if(o->header == MAP_FAILED) RAISE_SYSERROR("mmap");
  o->header->size = data_size;
  DEBUG("loaded %u bytes after resize. header is at %p", o->header->size, o->header);

  return NO_ERROR;
}

wp_error* mmap_obj_unload(mmap_obj* o) {
  DEBUG("unloading %u bytes", sizeof(mmap_obj_header) + o->header->size);
  if(munmap(o->header, sizeof(mmap_obj_header) + o->header->size) == -1) RAISE_SYSERROR("munmap");
  o->header = NULL;
  return NO_ERROR;
}
