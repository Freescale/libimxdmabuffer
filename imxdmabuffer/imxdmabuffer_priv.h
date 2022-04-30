#ifndef IMXDMABUFFER_PRIV_H
#define IMXDMABUFFER_PRIV_H

#include <stdint.h>
#include <stddef.h>

#include "imxdmabuffer.h"


#ifdef __cplusplus
extern "C" {
#endif


#define IMX_DMA_BUFFER_UNUSED_PARAM(x) ((void)(x))
#define IMX_DMA_BUFFER_ALIGN_VAL_TO(LENGTH, ALIGN_SIZE)  ( ((uintptr_t)(((uint8_t*)(LENGTH)) + (ALIGN_SIZE) - 1) / (ALIGN_SIZE)) * (ALIGN_SIZE) )


/* These two functions exist since most allocators do not allocate
 * cached DMA memory and thus do not need any syncing. */

static inline void imx_dma_buffer_noop_start_sync_session_func(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	IMX_DMA_BUFFER_UNUSED_PARAM(buffer);
}

static inline void imx_dma_buffer_noop_stop_sync_session_func(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	IMX_DMA_BUFFER_UNUSED_PARAM(buffer);
}



#ifdef __cplusplus
}
#endif


#endif /* IMXDMABUFFER_PRIV_H */
