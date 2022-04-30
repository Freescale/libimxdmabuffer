#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/pxp_device.h>

#include <imxdmabuffer_config.h>
#include "imxdmabuffer.h"
#include "imxdmabuffer_priv.h"
#include "imxdmabuffer_pxp_allocator.h"


typedef struct
{
	ImxDmaBuffer parent;

	imx_physical_address_t physical_address;

	size_t actual_size;
	size_t size;
	uint8_t* mapped_virtual_address;
	imx_physical_address_t aligned_physical_address;
	unsigned int map_flags;

	int mapping_refcount;

	struct pxp_mem_desc mem_desc;
}
ImxDmaBufferPxpBuffer;


typedef struct
{
	ImxDmaBufferAllocator parent;
	int pxp_fd;
	int pxp_fd_is_internal;
}
ImxDmaBufferPxpAllocator;


static void imx_dma_buffer_pxp_allocator_destroy(ImxDmaBufferAllocator *allocator);
static ImxDmaBuffer* imx_dma_buffer_pxp_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error);
static void imx_dma_buffer_pxp_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static uint8_t* imx_dma_buffer_pxp_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error);
static void imx_dma_buffer_pxp_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static imx_physical_address_t imx_dma_buffer_pxp_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static int imx_dma_buffer_pxp_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static size_t imx_dma_buffer_pxp_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);


static void imx_dma_buffer_pxp_allocator_destroy(ImxDmaBufferAllocator *allocator)
{
	ImxDmaBufferPxpAllocator *imx_pxp_allocator = (ImxDmaBufferPxpAllocator *)allocator;

	assert(imx_pxp_allocator != NULL);

	if ((imx_pxp_allocator->pxp_fd >= 0) && imx_pxp_allocator->pxp_fd_is_internal)
	{
		close(imx_pxp_allocator->pxp_fd);
		imx_pxp_allocator->pxp_fd = -1;
	}

	free(imx_pxp_allocator);
}


static ImxDmaBuffer* imx_dma_buffer_pxp_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error)
{
	size_t actual_size;
	ImxDmaBufferPxpBuffer *imx_pxp_buffer;
	ImxDmaBufferPxpAllocator *imx_pxp_allocator = (ImxDmaBufferPxpAllocator *)allocator;

	assert(imx_pxp_allocator != NULL);
	assert(imx_pxp_allocator->pxp_fd >= 0);

	/* The PXP allocator does not have a parameter for alignment, so we resort to a trick.
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
	imx_pxp_buffer = (ImxDmaBufferPxpBuffer *)malloc(sizeof(ImxDmaBufferPxpBuffer));
	imx_pxp_buffer->parent.allocator = allocator;
	imx_pxp_buffer->actual_size = actual_size;
	imx_pxp_buffer->size = size;
	imx_pxp_buffer->mapped_virtual_address = NULL;
	imx_pxp_buffer->mapping_refcount = 0;

	/* Perform the actual allocation. */
	imx_pxp_buffer->mem_desc.size = size;
	imx_pxp_buffer->mem_desc.mtype = MEMORY_TYPE_WC; /* TODO: Use MEMORY_TYPE_UNCACHED instead? */
	if (ioctl(imx_pxp_allocator->pxp_fd, PXP_IOC_GET_PHYMEM, &(imx_pxp_buffer->mem_desc)) != 0)
	{
		if (error != NULL)
			*error = errno;
		goto cleanup;
	}
	imx_pxp_buffer->physical_address = (imx_physical_address_t)((imx_pxp_buffer->mem_desc.phys_addr));

	/* Align the physical address. */
	imx_pxp_buffer->aligned_physical_address = (imx_physical_address_t)IMX_DMA_BUFFER_ALIGN_VAL_TO(imx_pxp_buffer->physical_address, alignment);

finish:
	return (ImxDmaBuffer *)imx_pxp_buffer;

cleanup:
	free(imx_pxp_buffer);
	imx_pxp_buffer = NULL;
	goto finish;
}


static void imx_dma_buffer_pxp_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferPxpBuffer *imx_pxp_buffer = (ImxDmaBufferPxpBuffer *)buffer;
	ImxDmaBufferPxpAllocator *imx_pxp_allocator = (ImxDmaBufferPxpAllocator *)allocator;

	assert(imx_pxp_allocator != NULL);
	assert(imx_pxp_allocator->pxp_fd >= 0);
	assert(imx_pxp_buffer != NULL);
	assert(imx_pxp_buffer->physical_address != 0);

	if (imx_pxp_buffer->mapped_virtual_address != NULL)
	{
		/* Set mapping_refcount to 1 to force an
		* imx_dma_buffer_pxp_allocator_unmap() to actually unmap the buffer. */
		imx_pxp_buffer->mapping_refcount = 1;
		imx_dma_buffer_pxp_allocator_unmap(allocator, buffer);
	}

	ioctl(imx_pxp_allocator->pxp_fd, PXP_IOC_PUT_PHYMEM, &(imx_pxp_buffer->mem_desc));

	free(imx_pxp_buffer);
}


static uint8_t* imx_dma_buffer_pxp_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error)
{
	ImxDmaBufferPxpBuffer *imx_pxp_buffer = (ImxDmaBufferPxpBuffer *)buffer;
	ImxDmaBufferPxpAllocator *imx_pxp_allocator = (ImxDmaBufferPxpAllocator *)allocator;

	assert(imx_pxp_allocator != NULL);
	assert(imx_pxp_allocator->pxp_fd >= 0);
	assert(imx_pxp_buffer != NULL);
	assert(imx_pxp_buffer->physical_address != 0);

	if (imx_pxp_buffer->mapped_virtual_address != NULL)
	{
		assert((imx_pxp_buffer->map_flags & flags & IMX_DMA_BUFFER_MAPPING_READWRITE_FLAG_MASK) == (flags & IMX_DMA_BUFFER_MAPPING_READWRITE_FLAG_MASK));

		/* Buffer is already mapped. Just increment the
		 * refcount and otherwise do nothing. */
		imx_pxp_buffer->mapping_refcount++;
	}
	else
	{
		/* Buffer is not mapped yet. Call mmap() to perform
		 * the memory mapping. */

		int mmap_prot = 0;
		int mmap_flags = MAP_SHARED;
		void *virtual_address;

		mmap_prot |= (flags & IMX_DMA_BUFFER_MAPPING_FLAG_READ) ? PROT_READ : 0;
		mmap_prot |= (flags & IMX_DMA_BUFFER_MAPPING_FLAG_WRITE) ? PROT_WRITE : 0;

		imx_pxp_buffer->map_flags = flags;

		virtual_address = mmap(0, imx_pxp_buffer->size, mmap_prot, mmap_flags, imx_pxp_allocator->pxp_fd, imx_pxp_buffer->physical_address);
		if (virtual_address == MAP_FAILED)
		{
			if (error != NULL)
				*error = errno;
		}
		else
		{
			imx_pxp_buffer->mapping_refcount = 1;
			imx_pxp_buffer->mapped_virtual_address = virtual_address;
		}
	}

	return imx_pxp_buffer->mapped_virtual_address;
}


static void imx_dma_buffer_pxp_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferPxpBuffer *imx_pxp_buffer = (ImxDmaBufferPxpBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);

	assert(imx_pxp_buffer != NULL);
	assert(imx_pxp_buffer->physical_address != 0);

	if (imx_pxp_buffer->mapped_virtual_address == NULL)
		return;

	imx_pxp_buffer->mapping_refcount--;
	if (imx_pxp_buffer->mapping_refcount != 0)
		return;

	munmap((void *)(imx_pxp_buffer->mapped_virtual_address), imx_pxp_buffer->size);
	imx_pxp_buffer->mapped_virtual_address = NULL;
}


static imx_physical_address_t imx_dma_buffer_pxp_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferPxpBuffer *imx_pxp_buffer = (ImxDmaBufferPxpBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_pxp_buffer != NULL);
	return imx_pxp_buffer->aligned_physical_address;
}


static int imx_dma_buffer_pxp_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	IMX_DMA_BUFFER_UNUSED_PARAM(buffer);
	return -1;
}


static size_t imx_dma_buffer_pxp_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferPxpBuffer *imx_pxp_buffer = (ImxDmaBufferPxpBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_pxp_buffer != NULL);
	return imx_pxp_buffer->size;
}


ImxDmaBufferAllocator* imx_dma_buffer_pxp_allocator_new(int pxp_fd, int *error)
{
	ImxDmaBufferPxpAllocator *imx_pxp_allocator = (ImxDmaBufferPxpAllocator *)malloc(sizeof(ImxDmaBufferPxpAllocator));
	imx_pxp_allocator->parent.destroy = imx_dma_buffer_pxp_allocator_destroy;
	imx_pxp_allocator->parent.allocate = imx_dma_buffer_pxp_allocator_allocate;
	imx_pxp_allocator->parent.deallocate = imx_dma_buffer_pxp_allocator_deallocate;
	imx_pxp_allocator->parent.map = imx_dma_buffer_pxp_allocator_map;
	imx_pxp_allocator->parent.unmap = imx_dma_buffer_pxp_allocator_unmap;
	imx_pxp_allocator->parent.start_sync_session = imx_dma_buffer_noop_start_sync_session_func;
	imx_pxp_allocator->parent.stop_sync_session = imx_dma_buffer_noop_stop_sync_session_func;
	imx_pxp_allocator->parent.get_physical_address = imx_dma_buffer_pxp_allocator_get_physical_address;
	imx_pxp_allocator->parent.get_fd = imx_dma_buffer_pxp_allocator_get_fd;
	imx_pxp_allocator->parent.get_size = imx_dma_buffer_pxp_allocator_get_size;
	imx_pxp_allocator->pxp_fd = pxp_fd;
	imx_pxp_allocator->pxp_fd_is_internal = (pxp_fd < 0);

	if (pxp_fd < 0)
	{
		imx_pxp_allocator->pxp_fd = open("/dev/pxp_device", O_RDWR, 0);
		if (imx_pxp_allocator->pxp_fd < 0)
		{
			if (error != NULL)
				*error = errno;
			free(imx_pxp_allocator);
			return NULL;
		}
	}

	return (ImxDmaBufferAllocator*)imx_pxp_allocator;
}


