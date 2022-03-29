#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <imxdmabuffer_config.h>
#include "imxdmabuffer.h"
#include "imxdmabuffer_priv.h"
#include "imxdmabuffer_ipu_allocator.h"
#include "imxdmabuffer_ipu_priv.h"


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
}
ImxDmaBufferIpuBuffer;


typedef struct
{
	ImxDmaBufferAllocator parent;
	int ipu_fd;
	int ipu_fd_is_internal;
}
ImxDmaBufferIpuAllocator;


static void imx_dma_buffer_ipu_allocator_destroy(ImxDmaBufferAllocator *allocator);
static ImxDmaBuffer* imx_dma_buffer_ipu_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error);
static void imx_dma_buffer_ipu_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static uint8_t* imx_dma_buffer_ipu_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error);
static void imx_dma_buffer_ipu_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static imx_physical_address_t imx_dma_buffer_ipu_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static int imx_dma_buffer_ipu_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static size_t imx_dma_buffer_ipu_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);


static void imx_dma_buffer_ipu_allocator_destroy(ImxDmaBufferAllocator *allocator)
{
	ImxDmaBufferIpuAllocator *imx_ipu_allocator = (ImxDmaBufferIpuAllocator *)allocator;

	assert(imx_ipu_allocator != NULL);

	if ((imx_ipu_allocator->ipu_fd >= 0) && imx_ipu_allocator->ipu_fd_is_internal)
	{
		close(imx_ipu_allocator->ipu_fd);
		imx_ipu_allocator->ipu_fd = -1;
	}

	free(imx_ipu_allocator);
}


static ImxDmaBuffer* imx_dma_buffer_ipu_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error)
{
	size_t actual_size;
	imx_physical_address_t physical_address;
	ImxDmaBufferIpuBuffer *imx_ipu_buffer;
	ImxDmaBufferIpuAllocator *imx_ipu_allocator = (ImxDmaBufferIpuAllocator *)allocator;

	assert(imx_ipu_allocator != NULL);
	assert(imx_ipu_allocator->ipu_fd >= 0);

	/* The IPU allocator does not have a parameter for alignment, so we resort to a trick.
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
	imx_ipu_buffer = (ImxDmaBufferIpuBuffer *)malloc(sizeof(ImxDmaBufferIpuBuffer));
	imx_ipu_buffer->parent.allocator = allocator;
	imx_ipu_buffer->actual_size = actual_size;
	imx_ipu_buffer->size = size;
	imx_ipu_buffer->mapped_virtual_address = NULL;
	imx_ipu_buffer->mapping_refcount = 0;

	/* Perform the actual allocation. */
	if ((physical_address = imx_dma_buffer_ipu_allocate(imx_ipu_allocator->ipu_fd, actual_size, error)) == 0)
		goto cleanup;
	imx_ipu_buffer->physical_address = physical_address;

	/* Align the physical address. */
	imx_ipu_buffer->aligned_physical_address = (imx_physical_address_t)IMX_DMA_BUFFER_ALIGN_VAL_TO(physical_address, alignment);

finish:
	return (ImxDmaBuffer *)imx_ipu_buffer;

cleanup:
	free(imx_ipu_buffer);
	imx_ipu_buffer = NULL;
	goto finish;
}


static void imx_dma_buffer_ipu_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIpuBuffer *imx_ipu_buffer = (ImxDmaBufferIpuBuffer *)buffer;
	ImxDmaBufferIpuAllocator *imx_ipu_allocator = (ImxDmaBufferIpuAllocator *)allocator;

	assert(imx_ipu_allocator != NULL);
	assert(imx_ipu_allocator->ipu_fd >= 0);
	assert(imx_ipu_buffer != NULL);
	assert(imx_ipu_buffer->physical_address != 0);

	if (imx_ipu_buffer->mapped_virtual_address != NULL)
	{
		/* Set mapping_refcount to 1 to force an
		* imx_dma_buffer_ipu_allocator_unmap() to actually unmap the buffer. */
		imx_ipu_buffer->mapping_refcount = 1;
		imx_dma_buffer_ipu_allocator_unmap(allocator, buffer);
	}

	imx_dma_buffer_ipu_deallocate(imx_ipu_allocator->ipu_fd, imx_ipu_buffer->physical_address);

	free(imx_ipu_buffer);
}


static uint8_t* imx_dma_buffer_ipu_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error)
{
	ImxDmaBufferIpuBuffer *imx_ipu_buffer = (ImxDmaBufferIpuBuffer *)buffer;
	ImxDmaBufferIpuAllocator *imx_ipu_allocator = (ImxDmaBufferIpuAllocator *)allocator;

	assert(imx_ipu_allocator != NULL);
	assert(imx_ipu_allocator->ipu_fd >= 0);
	assert(imx_ipu_buffer != NULL);
	assert(imx_ipu_buffer->physical_address != 0);

	if (imx_ipu_buffer->mapped_virtual_address != NULL)
	{
		assert(imx_ipu_buffer->map_flags == flags);

		/* Buffer is already mapped. Just increment the
		 * refcount and otherwise do nothing. */
		imx_ipu_buffer->mapping_refcount++;
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

		imx_ipu_buffer->map_flags = flags;

		virtual_address = mmap(0, imx_ipu_buffer->size, mmap_prot, mmap_flags, imx_ipu_allocator->ipu_fd, imx_ipu_buffer->physical_address);
		if (virtual_address == MAP_FAILED)
		{
			if (error != NULL)
				*error = errno;
		}
		else
		{
			imx_ipu_buffer->mapping_refcount = 1;
			imx_ipu_buffer->mapped_virtual_address = virtual_address;
		}
	}

	return imx_ipu_buffer->mapped_virtual_address;
}


static void imx_dma_buffer_ipu_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIpuBuffer *imx_ipu_buffer = (ImxDmaBufferIpuBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);

	assert(imx_ipu_buffer != NULL);
	assert(imx_ipu_buffer->physical_address != 0);

	if (imx_ipu_buffer->mapped_virtual_address == NULL)
		return;

	imx_ipu_buffer->mapping_refcount--;
	if (imx_ipu_buffer->mapping_refcount != 0)
		return;

	munmap((void *)(imx_ipu_buffer->mapped_virtual_address), imx_ipu_buffer->size);
	imx_ipu_buffer->mapped_virtual_address = NULL;
}


static imx_physical_address_t imx_dma_buffer_ipu_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIpuBuffer *imx_ipu_buffer = (ImxDmaBufferIpuBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_ipu_buffer != NULL);
	return imx_ipu_buffer->aligned_physical_address;
}


static int imx_dma_buffer_ipu_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	IMX_DMA_BUFFER_UNUSED_PARAM(buffer);
	return -1;
}


static size_t imx_dma_buffer_ipu_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIpuBuffer *imx_ipu_buffer = (ImxDmaBufferIpuBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_ipu_buffer != NULL);
	return imx_ipu_buffer->size;
}


ImxDmaBufferAllocator* imx_dma_buffer_ipu_allocator_new(int ipu_fd, int *error)
{
	ImxDmaBufferIpuAllocator *imx_ipu_allocator = (ImxDmaBufferIpuAllocator *)malloc(sizeof(ImxDmaBufferIpuAllocator));
	imx_ipu_allocator->parent.destroy = imx_dma_buffer_ipu_allocator_destroy;
	imx_ipu_allocator->parent.allocate = imx_dma_buffer_ipu_allocator_allocate;
	imx_ipu_allocator->parent.deallocate = imx_dma_buffer_ipu_allocator_deallocate;
	imx_ipu_allocator->parent.map = imx_dma_buffer_ipu_allocator_map;
	imx_ipu_allocator->parent.unmap = imx_dma_buffer_ipu_allocator_unmap;
	imx_ipu_allocator->parent.get_physical_address = imx_dma_buffer_ipu_allocator_get_physical_address;
	imx_ipu_allocator->parent.get_fd = imx_dma_buffer_ipu_allocator_get_fd;
	imx_ipu_allocator->parent.get_size = imx_dma_buffer_ipu_allocator_get_size;
	imx_ipu_allocator->ipu_fd = ipu_fd;
	imx_ipu_allocator->ipu_fd_is_internal = (ipu_fd < 0);

	if (ipu_fd < 0)
	{
		imx_ipu_allocator->ipu_fd = open("/dev/mxc_ipu", O_RDWR, 0);
		if (imx_ipu_allocator->ipu_fd < 0)
		{
			if (error != NULL)
				*error = errno;
			free(imx_ipu_allocator);
			return NULL;
		}
	}

	return (ImxDmaBufferAllocator*)imx_ipu_allocator;
}
