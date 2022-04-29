#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/ion.h>
#include <linux/dma-buf.h>
#include <linux/version.h>

#include "imxdmabuffer.h"
#include "imxdmabuffer_priv.h"
#include "imxdmabuffer_ion_allocator.h"


typedef struct
{
	ImxDmaBuffer parent;

	int dmabuf_fd;
	imx_physical_address_t physical_address;
	size_t size;
	uint8_t* mapped_virtual_address;
	unsigned int map_flags;

	int mapping_refcount;
}
ImxDmaBufferIonBuffer;


typedef struct
{
	ImxDmaBufferAllocator parent;
	int ion_fd;
	int ion_fd_is_internal;
	unsigned int ion_heap_id_mask;
	unsigned int ion_heap_flags;
}
ImxDmaBufferIonAllocator;


static void imx_dma_buffer_ion_allocator_destroy(ImxDmaBufferAllocator *allocator);
static ImxDmaBuffer* imx_dma_buffer_ion_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error);
static void imx_dma_buffer_ion_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static uint8_t* imx_dma_buffer_ion_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error);
static void imx_dma_buffer_ion_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static imx_physical_address_t imx_dma_buffer_ion_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static int imx_dma_buffer_ion_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static size_t imx_dma_buffer_ion_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);


static void imx_dma_buffer_ion_allocator_destroy(ImxDmaBufferAllocator *allocator)
{
	ImxDmaBufferIonAllocator *imx_ion_allocator = (ImxDmaBufferIonAllocator *)allocator;

	assert(imx_ion_allocator != NULL);

	if ((imx_ion_allocator->ion_fd >= 0) && imx_ion_allocator->ion_fd_is_internal)
	{
		close(imx_ion_allocator->ion_fd);
		imx_ion_allocator->ion_fd = -1;
	}

	free(imx_ion_allocator);
}


static ImxDmaBuffer* imx_dma_buffer_ion_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error)
{
	int dmabuf_fd = -1;
	imx_physical_address_t physical_address;
	ImxDmaBufferIonBuffer *imx_ion_buffer;
	ImxDmaBufferIonAllocator *imx_ion_allocator = (ImxDmaBufferIonAllocator *)allocator;

	assert(imx_ion_allocator != NULL);
	assert(imx_ion_allocator->ion_fd >= 0);

	/* Perform the actual allocation. */
	dmabuf_fd = imx_dma_buffer_ion_allocate_dmabuf(imx_ion_allocator->ion_fd, size, alignment, imx_ion_allocator->ion_heap_id_mask, imx_ion_allocator->ion_heap_flags, error);
	if (dmabuf_fd < 0)
		return NULL;

	/* Now that we've got the buffer, retrieve its physical address. */
	physical_address = imx_dma_buffer_ion_get_physical_address_from_dmabuf_fd(imx_ion_allocator->ion_fd, dmabuf_fd, error);
	if (physical_address == 0)
	{
		close(dmabuf_fd);
		return NULL;
	}

	/* Allocate system memory for the DMA buffer structure, and initialize its fields. */
	imx_ion_buffer = (ImxDmaBufferIonBuffer *)malloc(sizeof(ImxDmaBufferIonBuffer));
	imx_ion_buffer->parent.allocator = allocator;
	imx_ion_buffer->dmabuf_fd = dmabuf_fd;
	imx_ion_buffer->physical_address = physical_address;
	imx_ion_buffer->size = size;
	imx_ion_buffer->mapped_virtual_address = NULL;
	imx_ion_buffer->mapping_refcount = 0;

	return (ImxDmaBuffer *)imx_ion_buffer;
}


static void imx_dma_buffer_ion_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;

	assert(imx_ion_buffer != NULL);
	assert(imx_ion_buffer->dmabuf_fd >= 0);

	if (imx_ion_buffer->mapped_virtual_address != NULL)
	{
		/* Set mapping_refcount to 1 to force an
		* imx_dma_buffer_ion_allocator_unmap() to actually unmap the buffer. */
		imx_ion_buffer->mapping_refcount = 1;
		imx_dma_buffer_ion_allocator_unmap(allocator, buffer);
	}

	close(imx_ion_buffer->dmabuf_fd);
	free(imx_ion_buffer);
}


static uint8_t* imx_dma_buffer_ion_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);

	assert(imx_ion_buffer != NULL);
	assert(imx_ion_buffer->dmabuf_fd >= 0);

	if (flags == 0)
		flags = IMX_DMA_BUFFER_MAPPING_FLAG_READ | IMX_DMA_BUFFER_MAPPING_FLAG_WRITE;

	if (imx_ion_buffer->mapped_virtual_address != NULL)
	{
		assert((imx_ion_buffer->map_flags & flags) == flags);

		/* Buffer is already mapped. Just increment the
		 * refcount and otherwise do nothing. */
		imx_ion_buffer->mapping_refcount++;
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

		imx_ion_buffer->map_flags = flags;

		virtual_address = mmap(0, imx_ion_buffer->size, mmap_prot, mmap_flags, imx_ion_buffer->dmabuf_fd, 0);
		if (virtual_address == MAP_FAILED)
		{
			if (error != NULL)
				*error = errno;
		}
		else
		{
			imx_ion_buffer->mapping_refcount = 1;
			imx_ion_buffer->mapped_virtual_address = virtual_address;
		}
	}

	return imx_ion_buffer->mapped_virtual_address;
}


static void imx_dma_buffer_ion_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);

	assert(imx_ion_buffer != NULL);
	assert(imx_ion_buffer->dmabuf_fd >= 0);

	if (imx_ion_buffer->mapped_virtual_address == NULL)
		return;

	imx_ion_buffer->mapping_refcount--;
	if (imx_ion_buffer->mapping_refcount != 0)
		return;

	munmap((void *)(imx_ion_buffer->mapped_virtual_address), imx_ion_buffer->size);
	imx_ion_buffer->mapped_virtual_address = NULL;
}


static imx_physical_address_t imx_dma_buffer_ion_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_ion_buffer != NULL);
	return imx_ion_buffer->physical_address;
}


static int imx_dma_buffer_ion_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_ion_buffer != NULL);
	return imx_ion_buffer->dmabuf_fd;
}


static size_t imx_dma_buffer_ion_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_ion_buffer != NULL);
	return imx_ion_buffer->size;
}


ImxDmaBufferAllocator* imx_dma_buffer_ion_allocator_new(int ion_fd, unsigned int ion_heap_id_mask, unsigned int ion_heap_flags, int *error)
{
	ImxDmaBufferIonAllocator *imx_ion_allocator = (ImxDmaBufferIonAllocator *)malloc(sizeof(ImxDmaBufferIonAllocator));
	imx_ion_allocator->parent.destroy = imx_dma_buffer_ion_allocator_destroy;
	imx_ion_allocator->parent.allocate = imx_dma_buffer_ion_allocator_allocate;
	imx_ion_allocator->parent.deallocate = imx_dma_buffer_ion_allocator_deallocate;
	imx_ion_allocator->parent.map = imx_dma_buffer_ion_allocator_map;
	imx_ion_allocator->parent.unmap = imx_dma_buffer_ion_allocator_unmap;
	imx_ion_allocator->parent.get_physical_address = imx_dma_buffer_ion_allocator_get_physical_address;
	imx_ion_allocator->parent.get_fd = imx_dma_buffer_ion_allocator_get_fd;
	imx_ion_allocator->parent.get_size = imx_dma_buffer_ion_allocator_get_size;
	imx_ion_allocator->ion_fd = ion_fd;
	imx_ion_allocator->ion_fd_is_internal = (ion_fd < 0);
	imx_ion_allocator->ion_heap_id_mask = ion_heap_id_mask;
	imx_ion_allocator->ion_heap_flags = ion_heap_flags;

	if (ion_fd < 0)
	{
		imx_ion_allocator->ion_fd = open("/dev/ion", O_RDONLY);
		if (imx_ion_allocator->ion_fd < 0)
		{
			if (error != NULL)
				*error = errno;
			free(imx_ion_allocator);
			return NULL;
		}
	}

	return (ImxDmaBufferAllocator*)imx_ion_allocator;
}




#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)

static unsigned int get_heap_id_mask(int ion_fd, int *error)
{
	unsigned int heap_id_mask = 0;

	/* Starting with kernel 4.14.34, we can iterate over the
	 * ION heaps and find those with type ION_HEAP_TYPE_DMA. */

	int i;
	int heap_count;
	struct ion_heap_query query = { 0 };
	struct ion_heap_data *heap_data = NULL;

	if ((ioctl(ion_fd, ION_IOC_HEAP_QUERY, &query) < 0) || (query.cnt == 0))
	{
		if (error != NULL)
			*error = errno;
		return 0;
	}

	heap_count = query.cnt;

	heap_data = calloc(heap_count, sizeof(struct ion_heap_data));
	query.cnt = heap_count;
	query.heaps = (__u64)((uintptr_t)heap_data);
	if (ioctl(ion_fd, ION_IOC_HEAP_QUERY, &query) < 0)
	{
		if (error != NULL)
			*error = errno;
		free(heap_data);
		return 0;
	}

	for (i = 0; i < heap_count; ++i)
	{
		int is_dma_heap = (heap_data[i].type == ION_HEAP_TYPE_DMA);
		if (is_dma_heap)
			heap_id_mask |= 1u << heap_data[i].heap_id;
	}

	free(heap_data);

	return heap_id_mask;
}

#endif


int imx_dma_buffer_ion_allocate_dmabuf(int ion_fd, size_t size, size_t alignment, unsigned int ion_heap_id_mask, unsigned int ion_heap_flags, int *error)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	/* alignment value is unused in newer kernels. See function
	 * documentation for more about this. */
	IMX_DMA_BUFFER_UNUSED_PARAM(alignment);
#endif

	/* Prior to kernel 4.14.34, we cannot get the FD from the
	 * allocation data directly, and have to resort to an extra
	 * ION_IOC_MAP ioctl, which requires the user_handle. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
	ion_user_handle_t user_handle;
	int user_handle_set = 0;
#endif
	int dmabuf_fd = -1;

	assert(ion_fd >= 0);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	/* Starting with kernel 4.14.34, we do not need the ion_heap_id_mask
	 * argument anymore, since we can autodetect the mask, so we ignore
	 * the argument's value. */
	ion_heap_id_mask = get_heap_id_mask(ion_fd, error);
	if (ion_heap_id_mask == 0)
		goto finish;
#endif

	{
		struct ion_allocation_data allocation_data =
		{
			.len = size,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
			.align = alignment,
#endif
			.heap_id_mask = ion_heap_id_mask,
			.flags = ion_heap_flags
		};

		if (ioctl(ion_fd, ION_IOC_ALLOC, &allocation_data) < 0)
		{
			if (error != NULL)
				*error = errno;
			goto finish;
		}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
		{
			user_handle = allocation_data.handle;
			user_handle_set = 1;

			struct ion_fd_data fd_data =
			{
				.handle = user_handle
			};

			if ((ioctl(ion_fd, ION_IOC_MAP, &fd_data) < 0) || (fd_data.fd < 0))
			{
				if (error != NULL)
					*error = errno;
				goto finish;
			}

			dmabuf_fd = fd_data.fd;
		}
#else
		dmabuf_fd = allocation_data.fd;
#endif
	}


finish:

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
	if (user_handle_set)
	{
		struct ion_handle_data handle_data =
		{
			.handle = user_handle
		};

		ioctl(ion_fd, ION_IOC_FREE, &handle_data);
	}
#endif

	return dmabuf_fd;
}


imx_physical_address_t imx_dma_buffer_ion_get_physical_address_from_dmabuf_fd(int ion_fd, int dmabuf_fd, int *error)
{
	imx_physical_address_t physical_address = 0;

	/* The DMA_BUF_IOCTL_PHYS ioctl is not available
	 * until kernel version 4.14.34. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)

	struct ion_phys_dma_data phys_dma_data =
	{
		.phys = 0,
		.size = 0,
		.dmafd = dmabuf_fd
	};
	struct ion_custom_data custom_data =
	{
		.cmd = ION_IOC_PHYS_DMA,
		.arg = (unsigned long)(&phys_dma_data)
	};

	assert(ion_fd >= 0);

	if (ioctl(ion_fd, ION_IOC_CUSTOM, &custom_data) < 0)
	{
		if (error != NULL)
			*error = errno;
		return 0;
	}

	physical_address = (imx_physical_address_t)(phys_dma_data.phys);

#else

	struct dma_buf_phys dma_phys;

	assert(dmabuf_fd >= 0);

	IMX_DMA_BUFFER_UNUSED_PARAM(ion_fd);

	if (ioctl(dmabuf_fd, DMA_BUF_IOCTL_PHYS, &dma_phys) < 0)
	{
		if (error != NULL)
			*error = errno;
		return 0;
	}
	physical_address = (imx_physical_address_t)(dma_phys.phys);

#endif

	return physical_address;
}
