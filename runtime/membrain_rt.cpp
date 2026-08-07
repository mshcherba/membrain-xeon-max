#include "membrain_rt.h"
#include <stdlib.h>

extern "C" {

void *membrain_alloc(size_t size, uint32_t site_id) {
    (void)site_id;
    return malloc(size);
}

void membrain_free(void *ptr) {
    free(ptr);
}

} // extern "C"
