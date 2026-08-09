#ifndef MEMBRAIN_RT_H
#define MEMBRAIN_RT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void membrain_init(void);
void *membrain_alloc(size_t size, uint32_t site_id);
int membrain_posix_memalign(void **memptr, size_t alignment, size_t size, uint32_t site_id);
void membrain_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif // MEMBRAIN_RT_H
