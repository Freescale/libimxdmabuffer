#include <assert.h>
#include <stdlib.h>
#include <errno.h>

#include <g2d.h>

#include <imxdmabuffer_config.h>
#include "imxdmabuffer.h"
#include "imxdmabuffer_priv.h"
#include "imxdmabuffer_g2d_allocator.h"


typedef struct
{
	ImxDmaBuffer parent;

	size_t actual_size;
	size_t size;
	uint8_t* aligned_virtual_address;
	imx_physical_address_t aligned_physical_address;

	/* These are kept around to catch invalid redundant mapping attempts.
	 * It is good practice to check for those even if the underlying
	 * allocator (G2D in this case) does not actually need any mapping
	 * or mapping flags. */
	unsigned int map_flags;
	int mapping_refcount;

	struct g2d_buf *buf;
}
ImxDmaBufferG2dBuffer;


typedef struct
{
	ImxDmaBufferAllocator parent;
}
ImxDmaBufferG2dAllocator;


static void imx_dma_buffer_g2d_allocator_destroy(ImxDmaBufferAllocator *allocator);
static ImxDmaBuffer* imx_dma_buffer_g2d_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error);
static void imx_dma_buffer_g2d_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static uint8_t* imx_dma_buffer_g2d_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error);
static void imx_dma_buffer_g2d_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static imx_physical_address_t imx_dma_buffer_g2d_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static int imx_dma_buffer_g2d_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static size_t imx_dma_buffer_g2d_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);


static void imx_dma_buffer_g2d_allocator_destroy(ImxDmaBufferAllocator *allocator)
{
	ImxDmaBufferG2dAllocator *imx_g2d_allocator = (ImxDmaBufferG2dAllocator *)allocator;
	free(imx_g2d_allocator);
}


static ImxDmaBuffer* imx_dma_buffer_g2d_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error)
{
	size_t actual_size;
	ImxDmaBufferG2dBuffer *imx_g2d_buffer;
	ImxDmaBufferG2dAllocator *imx_g2d_allocator = (ImxDmaBufferG2dAllocator *)allocator;

	assert(imx_g2d_allocator != NULL);

	/* The G2D allocator does not have a parameter for alignment, so we resort to a trick.
	 * We allocate some extra bytes. Then, once allocated, we take the returned physical
	 * address, and add an offset to it to make sure the address is aligned as requested.
	 * This modified physical address is stored in aligned_physical_address . The maximum
	 * offset equals the alignment size, which is why we increase the allocation size by
	 * the alignment amount. Alignment of 0 or 1 however means "no alignment", so we don't
	 * actually do this trick in that case. */
	actual_size = size;
	if (alignment == 0)
		alignment = 1;
	if (alignment > 1)
		actual_size += alignment;

	/* Allocate system memory for the DMA buffer structure, and initialize its fields. */
	imx_g2d_buffer = (ImxDmaBufferG2dBuffer *)malloc(sizeof(ImxDmaBufferG2dBuffer));
	imx_g2d_buffer->parent.allocator = allocator;
	imx_g2d_buffer->actual_size = actual_size;
	imx_g2d_buffer->size = size;
	imx_g2d_buffer->mapping_refcount = 0;

	/* Perform the actual allocation. */
	if ((imx_g2d_buffer->buf = g2d_alloc(actual_size, 0)) == NULL)
	{
		if (error != NULL)
			*error = ENOMEM;
		goto cleanup;
	}

	/* Align the returned address. We also align the virtual address here, which isn't
	 * strictly necessary (alignment is only required for the physical address), but
	 * we do it regardless for sake of consistency. */
	imx_g2d_buffer->aligned_virtual_address = (uint8_t *)IMX_DMA_BUFFER_ALIGN_VAL_TO((uint8_t *)(imx_g2d_buffer->buf->buf_vaddr), alignment);
	imx_g2d_buffer->aligned_physical_address = (imx_physical_address_t)IMX_DMA_BUFFER_ALIGN_VAL_TO((imx_physical_address_t)(imx_g2d_buffer->buf->buf_paddr), alignment);

finish:
	return (ImxDmaBuffer *)imx_g2d_buffer;

cleanup:
	free(imx_g2d_buffer);
	imx_g2d_buffer = NULL;
	goto finish;
}


static void imx_dma_buffer_g2d_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferG2dBuffer *imx_g2d_buffer = (ImxDmaBufferG2dBuffer *)buffer;
	ImxDmaBufferG2dAllocator *imx_g2d_allocator = (ImxDmaBufferG2dAllocator *)allocator;

	assert(imx_g2d_allocator != NULL);
	assert(imx_g2d_buffer != NULL);
	assert(imx_g2d_buffer->buf != 0);

	g2d_free(imx_g2d_buffer->buf);

	free(imx_g2d_buffer);
}


static uint8_t* imx_dma_buffer_g2d_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error)
{
	ImxDmaBufferG2dBuffer *imx_g2d_buffer = (ImxDmaBufferG2dBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	IMX_DMA_BUFFER_UNUSED_PARAM(error);

	assert(imx_g2d_buffer != NULL);

	if (flags == 0)
		flags = IMX_DMA_BUFFER_MAPPING_FLAG_READ | IMX_DMA_BUFFER_MAPPING_FLAG_WRITE;

	/* As mentioned above, we keep the refcount and flags around
	 * just to check correct API usage. Do this check here.
	 * (Other allocators perform more steps than this.) */
	if (imx_g2d_buffer->mapping_refcount > 0)
	{
		assert((imx_g2d_buffer->map_flags & flags & IMX_DMA_BUFFER_MAPPING_READWRITE_FLAG_MASK) == (flags & IMX_DMA_BUFFER_MAPPING_READWRITE_FLAG_MASK));
		imx_g2d_buffer->mapping_refcount++;
	}
	else
	{
		imx_g2d_buffer->map_flags = flags;
		imx_g2d_buffer->mapping_refcount = 1;
	}

	/* G2D allocated memory is always mapped, so we just returned the aligned virtual
	 * address we stored in imx_dma_buffer_g2d_allocator_allocate(). */

	return imx_g2d_buffer->aligned_virtual_address;
}


static void imx_dma_buffer_g2d_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferG2dBuffer *imx_g2d_buffer = (ImxDmaBufferG2dBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);

	if (imx_g2d_buffer->mapping_refcount > 0)
		imx_g2d_buffer->mapping_refcount--;

	/* G2D allocated memory is always mapped, so we don't do anything here. */
}


static imx_physical_address_t imx_dma_buffer_g2d_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferG2dBuffer *imx_g2d_buffer = (ImxDmaBufferG2dBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_g2d_buffer != NULL);
	return imx_g2d_buffer->aligned_physical_address;
}


static int imx_dma_buffer_g2d_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	IMX_DMA_BUFFER_UNUSED_PARAM(buffer);
	return -1;
}


static size_t imx_dma_buffer_g2d_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferG2dBuffer *imx_g2d_buffer = (ImxDmaBufferG2dBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_g2d_buffer != NULL);
	return imx_g2d_buffer->size;
}


ImxDmaBufferAllocator* imx_dma_buffer_g2d_allocator_new(void)
{
	ImxDmaBufferG2dAllocator *imx_g2d_allocator = (ImxDmaBufferG2dAllocator *)malloc(sizeof(ImxDmaBufferG2dAllocator));
	imx_g2d_allocator->parent.destroy = imx_dma_buffer_g2d_allocator_destroy;
	imx_g2d_allocator->parent.allocate = imx_dma_buffer_g2d_allocator_allocate;
	imx_g2d_allocator->parent.deallocate = imx_dma_buffer_g2d_allocator_deallocate;
	imx_g2d_allocator->parent.map = imx_dma_buffer_g2d_allocator_map;
	imx_g2d_allocator->parent.unmap = imx_dma_buffer_g2d_allocator_unmap;
	imx_g2d_allocator->parent.start_sync_session = imx_dma_buffer_noop_start_sync_session_func;
	imx_g2d_allocator->parent.stop_sync_session = imx_dma_buffer_noop_stop_sync_session_func;
	imx_g2d_allocator->parent.get_physical_address = imx_dma_buffer_g2d_allocator_get_physical_address;
	imx_g2d_allocator->parent.get_fd = imx_dma_buffer_g2d_allocator_get_fd;
	imx_g2d_allocator->parent.get_size = imx_dma_buffer_g2d_allocator_get_size;

	return (ImxDmaBufferAllocator*)imx_g2d_allocator;
}
