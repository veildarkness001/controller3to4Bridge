#ifndef DT_TEST_KERNEL_H
#define DT_TEST_KERNEL_H
#include <stdint.h>
#include <sys/types.h>
#include <stddef.h>
extern const size_t KERNEL_IMAGE_SIZE;
extern const intptr_t KERNEL_ADDRESS_IMAGE_BASE;
uint32_t kernel_get_fw_version(void);
int32_t kernel_copyout(intptr_t address, void *out, size_t size);
int kernel_patch(intptr_t address, const void *before, const void *after, uint16_t size);
intptr_t kernel_dynlib_dlsym(pid_t pid, uint32_t module, const char *symbol);
int kernel_dynlib_handle(pid_t pid, const char *basename, uint32_t *handle);
intptr_t kernel_dynlib_resolve(pid_t pid, uint32_t module, const char *nid);
uint64_t kernel_get_ucred_authid(pid_t pid);
int kernel_get_ucred_caps(pid_t pid, uint8_t *caps);
int kernel_set_ucred_authid(pid_t pid, uint64_t authid);
int kernel_set_ucred_caps(pid_t pid, const uint8_t *caps);
#endif
