#include <assert.h>
#include <string.h>
#include <imxdmabuffer_config.h>
#include "imxdmabuffer.h"
#include "imxdmabuffer_priv.h"

#ifdef IMXDMABUFFER_ION_ALLOCATOR_ENABLED
#include "imxdmabuffer_ion_allocator.h"
#endif

#ifdef IMXDMABUFFER_DWL_ALLOCATOR_ENABLED
#include "imxdmabuffer_dwl_allocator.h"
#endif

#ifdef IMXDMABUFFER_IPU_ALLOCATOR_ENABLED
#include "imxdmabuffer_ipu_allocator.h"
#endif

#ifdef IMXDMABUFFER_G2D_ALLOCATOR_ENABLED
#include "imxdmabuffer_g2d_allocator.h"
#endif

#ifdef IMXDMABUFFER_PXP_ALLOCATOR_ENABLED
#include "imxdmabuffer_pxp_allocator.h"
#endif


ImxDmaBufferAllocator* imx_dma_buffer_allocator_new(int *error)
{
#ifdef IMXDMABUFFER_ION_ALLOCATOR_ENABLED
	return imx_dma_buffer_ion_allocator_new(
		IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_ION_FD,
		IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_ID_MASK,
		IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_FLAGS,
		error
	);
#endif
#ifdef IMXDMABUFFER_DWL_ALLOCATOR_ENABLED
	return imx_dma_buffer_dwl_allocator_new(error);
#endif
#ifdef IMXDMABUFFER_IPU_ALLOCATOR_ENABLED
	return imx_dma_buffer_ipu_allocator_new(IMX_DMA_BUFFER_IPU_ALLOCATOR_DEFAULT_IPU_FD, error);
#endif
#ifdef IMXDMABUFFER_G2D_ALLOCATOR_ENABLED
	return imx_dma_buffer_g2d_allocator_new();
#endif
#ifdef IMXDMABUFFER_PXP_ALLOCATOR_ENABLED
	return imx_dma_buffer_pxp_allocator_new(IMX_DMA_BUFFER_PXP_ALLOCATOR_DEFAULT_PXP_FD, error);
#endif
}


void imx_dma_buffer_allocator_destroy(ImxDmaBufferAllocator *allocator)
{
	assert(allocator != NULL);
	assert(allocator->destroy != NULL);
	allocator->destroy(allocator);
}


ImxDmaBuffer* imx_dma_buffer_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error)
{
	assert(allocator != NULL);
	assert(allocator->allocate != NULL);
	assert(size >= 1);
	return allocator->allocate(allocator, size, alignment, error);
}


void imx_dma_buffer_deallocate(ImxDmaBuffer *buffer)
{
	assert(buffer != NULL);
	assert(buffer->allocator != NULL);
	assert(buffer->allocator->deallocate != NULL);
	buffer->allocator->deallocate(buffer->allocator, buffer);
}


uint8_t* imx_dma_buffer_map(ImxDmaBuffer *buffer, unsigned int flags, int *error)
{
	assert(buffer != NULL);
	assert(buffer->allocator != NULL);
	assert(buffer->allocator->map != NULL);
	return buffer->allocator->map(buffer->allocator, buffer, flags, error);
}


void imx_dma_buffer_unmap(ImxDmaBuffer *buffer)
{
	assert(buffer != NULL);
	assert(buffer->allocator != NULL);
	assert(buffer->allocator->unmap != NULL);
	buffer->allocator->unmap(buffer->allocator, buffer);
}


imx_physical_address_t imx_dma_buffer_get_physical_address(ImxDmaBuffer *buffer)
{
	assert(buffer != NULL);
	assert(buffer->allocator != NULL);
	assert(buffer->allocator->get_physical_address != NULL);
	return buffer->allocator->get_physical_address(buffer->allocator, buffer);
}


int imx_dma_buffer_get_fd(ImxDmaBuffer *buffer)
{
	assert(buffer != NULL);
	assert(buffer->allocator != NULL);
	return (buffer->allocator->get_fd != NULL) ? buffer->allocator->get_fd(buffer->allocator, buffer) : -1;
}


size_t imx_dma_buffer_get_size(ImxDmaBuffer *buffer)
{
	assert(buffer != NULL);
	assert(buffer->allocator != NULL);
	return buffer->allocator->get_size(buffer->allocator, buffer);
}





static ImxDmaBuffer* wrapped_dma_buffer_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error)
{
	/* This allocator is used for wrapping existing DMA memory. Therefore,
	 * it doesn't actually allocate anything. This also means that the
	 * NULL return value does not actually indicate an error. This
	 * inconsistency is okay, since the allocator will never be accessible
	 * from the outside. */
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	IMX_DMA_BUFFER_UNUSED_PARAM(size);
	IMX_DMA_BUFFER_UNUSED_PARAM(alignment);
	IMX_DMA_BUFFER_UNUSED_PARAM(error);
	return NULL;
}


static void wrapped_dma_buffer_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	IMX_DMA_BUFFER_UNUSED_PARAM(buffer);
}


static uint8_t* wrapped_dma_buffer_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	ImxWrappedDmaBuffer *wrapped_buf = (ImxWrappedDmaBuffer *)(buffer);
	return (wrapped_buf->map != NULL) ? wrapped_buf->map(wrapped_buf, flags, error) : NULL;
}


static void wrapped_dma_buffer_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	ImxWrappedDmaBuffer *wrapped_buf = (ImxWrappedDmaBuffer *)(buffer);
	if (wrapped_buf->unmap != NULL)
		wrapped_buf->unmap(wrapped_buf);
}


static imx_physical_address_t wrapped_dma_buffer_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	return ((ImxWrappedDmaBuffer *)(buffer))->physical_address;
}


static int wrapped_dma_buffer_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	return ((ImxWrappedDmaBuffer *)(buffer))->fd;
}


static size_t wrapped_dma_buffer_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	return ((ImxWrappedDmaBuffer *)(buffer))->size;
}


static ImxDmaBufferAllocator wrapped_dma_buffer_allocator =
{
	NULL, /* the wrapped allocator is static and internal, so a destroy() function makes no sense */
	wrapped_dma_buffer_allocator_allocate,
	wrapped_dma_buffer_allocator_deallocate,
	wrapped_dma_buffer_allocator_map,
	wrapped_dma_buffer_allocator_unmap,
	wrapped_dma_buffer_allocator_get_physical_address,
	wrapped_dma_buffer_allocator_get_fd,
	wrapped_dma_buffer_allocator_get_size,
	{ 0, }
};


void imx_dma_buffer_init_wrapped_buffer(ImxWrappedDmaBuffer *buffer)
{
	memset(buffer, 0, sizeof(ImxWrappedDmaBuffer));
	buffer->parent.allocator = &wrapped_dma_buffer_allocator;
}
