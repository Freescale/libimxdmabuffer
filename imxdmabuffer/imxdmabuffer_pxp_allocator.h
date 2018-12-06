#ifndef IMXDMABUFFER_PXP_ALLOCATOR_H
#define IMXDMABUFFER_PXP_ALLOCATOR_H

#include "imxdmabuffer.h"


#ifdef __cplusplus
extern "C" {
#endif


#define IMX_DMA_BUFFER_PXP_ALLOCATOR_DEFAULT_PXP_FD (-1)


/* Creates a new DMA buffer allocator that uses the PxP allocator.
 *
 * This allocator does not support file descriptors. imx_dma_buffer_get_fd()
 * function calls return -1.
 *
 * @param pxp_fd /dev/pxp_device file descriptor to use, or a negative value if the
 *        allocator shall open and use its own file descriptor. The preprocessor macro
 *        IMX_DMA_BUFFER_PXP_ALLOCATOR_DEFAULT_PXP_FD can be used for the latter case.
 * @param error If this pointer is non-NULL, and if an error occurs, then the integer
 *        the pointer refers to is set to an error code from errno.h. If creating
 *        the allocator succeeds, the integer is not modified.
 * @return Pointer to the newly created PxP DMA allocator, or NULL in case of an error.
 */
ImxDmaBufferAllocator* imx_dma_buffer_pxp_allocator_new(int pxp_fd, int *error);


#ifdef __cplusplus
}
#endif


#endif /* IMXDMABUFFER_PXP_ALLOCATOR_H */
