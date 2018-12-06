#ifndef IMXDMABUFFER_G2D_ALLOCATOR_H
#define IMXDMABUFFER_G2D_ALLOCATOR_H

#include "imxdmabuffer.h"


#ifdef __cplusplus
extern "C" {
#endif


#define IMX_DMA_BUFFER_IPU_ALLOCATOR_DEFAULT_IPU_FD (-1)


/* Creates a new DMA buffer allocator that uses the Vivante G2D allocator.
 *
 * This allocator does not support file descriptors. imx_dma_buffer_get_fd()
 * function calls return -1. */
ImxDmaBufferAllocator* imx_dma_buffer_g2d_allocator_new(void);


#ifdef __cplusplus
}
#endif


#endif /* IMXDMABUFFER_G2D_ALLOCATOR_H */
