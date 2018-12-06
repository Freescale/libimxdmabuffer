#ifndef IMXDMABUFFER_DWL_ALLOCATOR_H
#define IMXDMABUFFER_DWL_ALLOCATOR_H

#include "imxdmabuffer.h"


#ifdef __cplusplus
extern "C" {
#endif


/* Creates a new DMA buffer allocator that uses Hantro's DWL API.
 *
 * The allocator needs the Hantro decoder type (G1 or G2) to be specified in the
 * libimxdmabuffer build configuration.
 *
 * This allocator supports file descriptors.
 *
 * @param error If this pointer is non-NULL, and if an error occurs, then the integer
 *        the pointer refers to is set to an error code from errno.h. If creating
 *        the allocator succeeds, the integer is not modified.
 * @return Pointer to the newly created DWL DMA allocator, or NULL in case of an error.
 */
ImxDmaBufferAllocator* imx_dma_buffer_dwl_allocator_new(int *error);


#ifdef __cplusplus
}
#endif


#endif // IMXDMABUFFER_DWL_ALLOCATOR_H
