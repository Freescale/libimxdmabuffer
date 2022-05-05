#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

#include <imxdmabuffer_config.h>
#include "imxdmabuffer.h"
#include "imxdmabuffer_priv.h"
#include "imxdmabuffer_dma_heap_allocator.h"


/* XXX: Currently (2022-04-28), DMA-BUF heaps do not synchrnize properly in
 * NXP BSPs. Not even the DMA_BUF sync ioctl helps, and according to this commit:
 * https://source.codeaurora.org/external/imx/gst-plugins-base/commit/?h=MM_04.06.04_2112_L5.15.y&id=6dd8dc7566a76354e3283dc4e2dcecae817bf20e
 * it even cause issues (which are not documented, unfortunately).
 *
 * A workaround is to issue a DMA_BUF_IOCTL_PHYS ioctl, as seen here:
 * https://source.codeaurora.org/external/imx/gst-plugins-base/commit/?h=MM_04.06.04_2112_L5.15.y&id=d6ad337837085f8dc1ef29eae6844edbf3a3915f
 * This seems to sync CPU caches with DRAM. It is unclear if this will ever change,
 * so this workaround, along with the DMA_BUF sync, are kept in code blocks that
 * can be enabled/disabled with these #defines. */
//#define USE_DMA_BUF_SYNC_IOCTL
#define USE_DMA_BUF_PHYS_SYNC_WORKAROUND


typedef struct
{
	ImxDmaBuffer parent;

	int dmabuf_fd;
	imx_physical_address_t physical_address;
	size_t size;
	uint8_t* mapped_virtual_address;
	unsigned int map_flags;

	int mapping_refcount;
	int sync_started;
}
ImxDmaBufferDmaHeapBuffer;


typedef struct
{
	ImxDmaBufferAllocator parent;
	int dma_heap_fd;
	int dma_heap_fd_is_internal;
	unsigned int heap_flags;
	unsigned int fd_flags;
}
ImxDmaBufferDmaHeapAllocator;


static void imx_dma_buffer_dma_heap_allocator_destroy(ImxDmaBufferAllocator *allocator);
static ImxDmaBuffer* imx_dma_buffer_dma_heap_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error);
static void imx_dma_buffer_dma_heap_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static uint8_t* imx_dma_buffer_dma_heap_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error);
static void imx_dma_buffer_dma_heap_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static void imx_dma_buffer_dma_heap_allocator_start_sync_session(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static void imx_dma_buffer_dma_heap_allocator_start_sync_session_impl(ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer);
static void imx_dma_buffer_dma_heap_allocator_stop_sync_session(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static void imx_dma_buffer_dma_heap_allocator_stop_sync_session_impl(ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer);
static imx_physical_address_t imx_dma_buffer_dma_heap_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static int imx_dma_buffer_dma_heap_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static size_t imx_dma_buffer_dma_heap_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);


static void imx_dma_buffer_dma_heap_allocator_destroy(ImxDmaBufferAllocator *allocator)
{
	ImxDmaBufferDmaHeapAllocator *imx_dma_heap_allocator = (ImxDmaBufferDmaHeapAllocator *)allocator;

	assert(imx_dma_heap_allocator != NULL);

	if ((imx_dma_heap_allocator->dma_heap_fd > 0) && imx_dma_heap_allocator->dma_heap_fd_is_internal)
	{
		close(imx_dma_heap_allocator->dma_heap_fd);
		imx_dma_heap_allocator->dma_heap_fd = -1;
	}

	free(imx_dma_heap_allocator);
}


static ImxDmaBuffer* imx_dma_buffer_dma_heap_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error)
{
	int dmabuf_fd = -1;
	imx_physical_address_t physical_address;
	ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer;
	ImxDmaBufferDmaHeapAllocator *imx_dma_heap_allocator = (ImxDmaBufferDmaHeapAllocator *)allocator;

	IMX_DMA_BUFFER_UNUSED_PARAM(alignment);

	assert(imx_dma_heap_allocator != NULL);
	assert(imx_dma_heap_allocator->dma_heap_fd > 0);

	/* Perform the actual allocation. */
	dmabuf_fd = imx_dma_buffer_dma_heap_allocate_dmabuf(
		imx_dma_heap_allocator->dma_heap_fd,
		size,
		imx_dma_heap_allocator->heap_flags,
		imx_dma_heap_allocator->fd_flags,
		error
	);
	if (dmabuf_fd < 0)
		return NULL;

	/* Now that we've got the buffer, retrieve its physical address. */
	physical_address = imx_dma_buffer_dma_heap_get_physical_address_from_dmabuf_fd(dmabuf_fd, error);
	if (physical_address == 0)
	{
		close(dmabuf_fd);
		return NULL;
	}

	/* Allocate system memory for the DMA buffer structure, and initialize its fields. */
	imx_dma_heap_buffer = (ImxDmaBufferDmaHeapBuffer *)malloc(sizeof(ImxDmaBufferDmaHeapBuffer));
	imx_dma_heap_buffer->parent.allocator = allocator;
	imx_dma_heap_buffer->dmabuf_fd = dmabuf_fd;
	imx_dma_heap_buffer->physical_address = physical_address;
	imx_dma_heap_buffer->size = size;
	imx_dma_heap_buffer->mapped_virtual_address = NULL;
	imx_dma_heap_buffer->mapping_refcount = 0;
	imx_dma_heap_buffer->sync_started = 0;

	return (ImxDmaBuffer *)imx_dma_heap_buffer;
}


static void imx_dma_buffer_dma_heap_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer = (ImxDmaBufferDmaHeapBuffer *)buffer;

	assert(imx_dma_heap_buffer != NULL);
	assert(imx_dma_heap_buffer->dmabuf_fd > 0);

	if (imx_dma_heap_buffer->mapped_virtual_address != NULL)
	{
		imx_dma_buffer_dma_heap_allocator_stop_sync_session(allocator, buffer);

		/* Set mapping_refcount to 1 to force an
		* imx_dma_buffer_dma_heap_allocator_unmap() to actually unmap the buffer. */
		imx_dma_heap_buffer->mapping_refcount = 1;
		imx_dma_buffer_dma_heap_allocator_unmap(allocator, buffer);
	}

	close(imx_dma_heap_buffer->dmabuf_fd);
	free(imx_dma_heap_buffer);
}


static uint8_t* imx_dma_buffer_dma_heap_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error)
{
	ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer = (ImxDmaBufferDmaHeapBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);

	assert(imx_dma_heap_buffer != NULL);
	assert(imx_dma_heap_buffer->dmabuf_fd > 0);

	if ((flags & IMX_DMA_BUFFER_MAPPING_READWRITE_FLAG_MASK) == 0)
		flags |= IMX_DMA_BUFFER_MAPPING_FLAG_READ | IMX_DMA_BUFFER_MAPPING_FLAG_WRITE;

	if (imx_dma_heap_buffer->mapped_virtual_address != NULL)
	{
		assert((imx_dma_heap_buffer->map_flags & flags & IMX_DMA_BUFFER_MAPPING_READWRITE_FLAG_MASK) == (flags & IMX_DMA_BUFFER_MAPPING_READWRITE_FLAG_MASK));

		/* Buffer is already mapped. Just increment the
		 * refcount and otherwise do nothing. */
		imx_dma_heap_buffer->mapping_refcount++;
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

		imx_dma_heap_buffer->map_flags = flags;

		virtual_address = mmap(0, imx_dma_heap_buffer->size, mmap_prot, mmap_flags, imx_dma_heap_buffer->dmabuf_fd, 0);
		if (virtual_address == MAP_FAILED)
		{
			if (error != NULL)
				*error = errno;
		}
		else
		{
			imx_dma_heap_buffer->mapping_refcount = 1;
			imx_dma_heap_buffer->mapped_virtual_address = virtual_address;
		}

		if (!(flags & IMX_DMA_BUFFER_MAPPING_FLAG_MANUAL_SYNC))
			imx_dma_buffer_dma_heap_allocator_start_sync_session_impl(imx_dma_heap_buffer);
	}

	return imx_dma_heap_buffer->mapped_virtual_address;
}


static void imx_dma_buffer_dma_heap_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer = (ImxDmaBufferDmaHeapBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);

	assert(imx_dma_heap_buffer != NULL);
	assert(imx_dma_heap_buffer->dmabuf_fd > 0);

	if (imx_dma_heap_buffer->mapped_virtual_address == NULL)
		return;

	imx_dma_heap_buffer->mapping_refcount--;
	if (imx_dma_heap_buffer->mapping_refcount != 0)
		return;

	if (!(imx_dma_heap_buffer->map_flags & IMX_DMA_BUFFER_MAPPING_FLAG_MANUAL_SYNC))
		imx_dma_buffer_dma_heap_allocator_stop_sync_session_impl(imx_dma_heap_buffer);

	munmap((void *)(imx_dma_heap_buffer->mapped_virtual_address), imx_dma_heap_buffer->size);
	imx_dma_heap_buffer->mapped_virtual_address = NULL;
}


static void imx_dma_buffer_dma_heap_allocator_start_sync_session(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer = (ImxDmaBufferDmaHeapBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);

	if (imx_dma_heap_buffer->sync_started)
		return;
	if (!(imx_dma_heap_buffer->map_flags & IMX_DMA_BUFFER_MAPPING_FLAG_MANUAL_SYNC))
		return;

	imx_dma_buffer_dma_heap_allocator_start_sync_session_impl(imx_dma_heap_buffer);
}


static void imx_dma_buffer_dma_heap_allocator_start_sync_session_impl(ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer)
{
#ifdef USE_DMA_BUF_SYNC_IOCTL
	{
		struct dma_buf_sync dmabuf_sync;
		memset(&dmabuf_sync, 0, sizeof(dmabuf_sync));
		dmabuf_sync.flags = DMA_BUF_SYNC_START;
		dmabuf_sync.flags |= (imx_dma_heap_buffer->map_flags & IMX_DMA_BUFFER_MAPPING_FLAG_READ) ? DMA_BUF_SYNC_READ : 0;
		dmabuf_sync.flags |= (imx_dma_heap_buffer->map_flags & IMX_DMA_BUFFER_MAPPING_FLAG_WRITE) ? DMA_BUF_SYNC_WRITE : 0;

		ioctl(imx_dma_heap_buffer->dmabuf_fd, DMA_BUF_IOCTL_SYNC, &dmabuf_sync);
	}
#endif

#ifdef USE_DMA_BUF_PHYS_SYNC_WORKAROUND
	/* Use the DMA_BUF_IOCTL_PHYS here to force the CPU cache to
	 * be repopulated with the contents of the actual memory block.
	 * Otherwise, CPU read operations might use stale cached data. */
	if (imx_dma_heap_buffer->map_flags & IMX_DMA_BUFFER_MAPPING_FLAG_READ)
	{
		struct dma_buf_phys dma_phys;
		ioctl(imx_dma_heap_buffer->dmabuf_fd, DMA_BUF_IOCTL_PHYS, &dma_phys);
	}
#endif

	imx_dma_heap_buffer->sync_started = 1;
}


static void imx_dma_buffer_dma_heap_allocator_stop_sync_session(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer = (ImxDmaBufferDmaHeapBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);

	assert(imx_dma_heap_buffer->mapped_virtual_address != 0);

	if (!imx_dma_heap_buffer->sync_started)
		return;
	if (!(imx_dma_heap_buffer->map_flags & IMX_DMA_BUFFER_MAPPING_FLAG_MANUAL_SYNC))
		return;

	imx_dma_buffer_dma_heap_allocator_stop_sync_session_impl(imx_dma_heap_buffer);
}


static void imx_dma_buffer_dma_heap_allocator_stop_sync_session_impl(ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer)
{
#ifdef USE_DMA_BUF_SYNC_IOCTL
	{
		struct dma_buf_sync dmabuf_sync;
		memset(&dmabuf_sync, 0, sizeof(dmabuf_sync));
		dmabuf_sync.flags = DMA_BUF_SYNC_END;
		dmabuf_sync.flags |= (imx_dma_heap_buffer->map_flags & IMX_DMA_BUFFER_MAPPING_FLAG_READ) ? DMA_BUF_SYNC_READ : 0;
		dmabuf_sync.flags |= (imx_dma_heap_buffer->map_flags & IMX_DMA_BUFFER_MAPPING_FLAG_WRITE) ? DMA_BUF_SYNC_WRITE : 0;

		ioctl(imx_dma_heap_buffer->dmabuf_fd, DMA_BUF_IOCTL_SYNC, &dmabuf_sync);
	}
#endif

#ifdef USE_DMA_BUF_PHYS_SYNC_WORKAROUND
	/* Use the DMA_BUF_IOCTL_PHYS here to force the CPU cache to be
	 * written to the actual memory block. Otherwise, device DMA
	 * access to memory may not use the data the CPU just wrote. */
	if (imx_dma_heap_buffer->map_flags & IMX_DMA_BUFFER_MAPPING_FLAG_WRITE)
	{
		struct dma_buf_phys dma_phys;
		ioctl(imx_dma_heap_buffer->dmabuf_fd, DMA_BUF_IOCTL_PHYS, &dma_phys);
	}
#endif

	imx_dma_heap_buffer->sync_started = 0;
}


static imx_physical_address_t imx_dma_buffer_dma_heap_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer = (ImxDmaBufferDmaHeapBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_dma_heap_buffer != NULL);
	return imx_dma_heap_buffer->physical_address;
}


static int imx_dma_buffer_dma_heap_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer = (ImxDmaBufferDmaHeapBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_dma_heap_buffer != NULL);
	return imx_dma_heap_buffer->dmabuf_fd;
}


static size_t imx_dma_buffer_dma_heap_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDmaHeapBuffer *imx_dma_heap_buffer = (ImxDmaBufferDmaHeapBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_dma_heap_buffer != NULL);
	return imx_dma_heap_buffer->size;
}


char const * IMX_DMA_BUFFER_DMA_HEAP_ALLOCATOR_DEFAULT_DMA_HEAP_NODE = "/dev/dma_heap/linux,cma";
unsigned int const IMX_DMA_BUFFER_DMA_HEAP_ALLOCATOR_DEFAULT_HEAP_FLAGS = DMA_HEAP_VALID_HEAP_FLAGS;
unsigned int const IMX_DMA_BUFFER_DMA_HEAP_ALLOCATOR_DEFAULT_FD_FLAGS = (O_RDWR | O_CLOEXEC);


ImxDmaBufferAllocator* imx_dma_buffer_dma_heap_allocator_new(
	int dma_heap_fd,
	unsigned int heap_flags,
	unsigned int fd_flags,
	int *error
)
{
	ImxDmaBufferDmaHeapAllocator *imx_dma_heap_allocator;

	imx_dma_heap_allocator = (ImxDmaBufferDmaHeapAllocator *)malloc(sizeof(ImxDmaBufferDmaHeapAllocator));
	imx_dma_heap_allocator->parent.destroy = imx_dma_buffer_dma_heap_allocator_destroy;
	imx_dma_heap_allocator->parent.allocate = imx_dma_buffer_dma_heap_allocator_allocate;
	imx_dma_heap_allocator->parent.deallocate = imx_dma_buffer_dma_heap_allocator_deallocate;
	imx_dma_heap_allocator->parent.map = imx_dma_buffer_dma_heap_allocator_map;
	imx_dma_heap_allocator->parent.unmap = imx_dma_buffer_dma_heap_allocator_unmap;
	imx_dma_heap_allocator->parent.start_sync_session = imx_dma_buffer_dma_heap_allocator_start_sync_session;
	imx_dma_heap_allocator->parent.stop_sync_session = imx_dma_buffer_dma_heap_allocator_stop_sync_session;
	imx_dma_heap_allocator->parent.get_physical_address = imx_dma_buffer_dma_heap_allocator_get_physical_address;
	imx_dma_heap_allocator->parent.get_fd = imx_dma_buffer_dma_heap_allocator_get_fd;
	imx_dma_heap_allocator->parent.get_size = imx_dma_buffer_dma_heap_allocator_get_size;
	imx_dma_heap_allocator->dma_heap_fd = dma_heap_fd;
	imx_dma_heap_allocator->dma_heap_fd_is_internal = (dma_heap_fd < 0);
	imx_dma_heap_allocator->heap_flags = heap_flags;
	imx_dma_heap_allocator->fd_flags = fd_flags;

	if (dma_heap_fd < 0)
	{
		imx_dma_heap_allocator->dma_heap_fd = open(IMXDMABUFFER_DMA_HEAP_DEVICE_NODE_PATH, O_RDWR);
		if (imx_dma_heap_allocator->dma_heap_fd < 0)
		{
			if (error != NULL)
				*error = errno;
			free(imx_dma_heap_allocator);
			return NULL;
		}
	}

	return (ImxDmaBufferAllocator*)imx_dma_heap_allocator;
}


int imx_dma_buffer_dma_heap_allocator_get_dma_heap_fd(ImxDmaBufferAllocator *allocator)
{
	ImxDmaBufferDmaHeapAllocator *imx_dma_heap_allocator = (ImxDmaBufferDmaHeapAllocator *)allocator;
	return imx_dma_heap_allocator->dma_heap_fd;
}


int imx_dma_buffer_dma_heap_allocate_dmabuf(
	int dma_heap_fd,
	size_t size,
	unsigned int heap_flags,
	unsigned int fd_flags,
	int *error
)
{
	int dmabuf_fd = -1;
	struct dma_heap_allocation_data heap_alloc_data;

	assert(dma_heap_fd > 0);
	assert(size > 0);

	memset(&heap_alloc_data, 0, sizeof(heap_alloc_data));

	heap_alloc_data.len = size;
	heap_alloc_data.heap_flags = heap_flags;
	heap_alloc_data.fd_flags = fd_flags;

	if (ioctl(dma_heap_fd, DMA_HEAP_IOCTL_ALLOC, &heap_alloc_data) < 0)
	{
		if (error != NULL)
			*error = errno;
		goto finish;
	}

	dmabuf_fd = heap_alloc_data.fd;

finish:
	return dmabuf_fd;
}


imx_physical_address_t imx_dma_buffer_dma_heap_get_physical_address_from_dmabuf_fd(int dmabuf_fd, int *error)
{
	struct dma_buf_phys dma_phys;

	assert(dmabuf_fd > 0);

	if (ioctl(dmabuf_fd, DMA_BUF_IOCTL_PHYS, &dma_phys) < 0)
	{
		if (error != NULL)
			*error = errno;
		return 0;
	}

	return (imx_physical_address_t)(dma_phys.phys);
}
