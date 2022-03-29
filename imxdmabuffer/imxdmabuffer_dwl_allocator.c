#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "dwl.h"

#include <imxdmabuffer_config.h>
#include "imxdmabuffer.h"
#include "imxdmabuffer_priv.h"
#include "imxdmabuffer_dwl_allocator.h"


typedef struct
{
	ImxDmaBuffer parent;

	struct DWLLinearMem dwl_linear_mem;

	size_t actual_size;
	size_t size;
	uint8_t* aligned_virtual_address;
	imx_physical_address_t aligned_physical_address;

	/* These are kept around to catch invalid redundant mapping attempts.
	 * It is good practice to check for those even if the underlying
	 * allocator (DWL in this case) does not actually need any mapping
	 * or mapping flags. */
	unsigned int map_flags;
	int mapping_refcount;
}
ImxDmaBufferDwlBuffer;


typedef struct
{
	ImxDmaBufferAllocator parent;
	struct DWLInitParam dwl_init_param;
	void const *dwl_instance;
}
ImxDmaBufferDwlAllocator;


static void imx_dma_buffer_dwl_allocator_destroy(ImxDmaBufferAllocator *allocator);
static ImxDmaBuffer* imx_dma_buffer_dwl_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error);
static void imx_dma_buffer_dwl_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static uint8_t* imx_dma_buffer_dwl_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error);
static void imx_dma_buffer_dwl_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static imx_physical_address_t imx_dma_buffer_dwl_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static int imx_dma_buffer_dwl_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static size_t imx_dma_buffer_dwl_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);


static void imx_dma_buffer_dwl_allocator_destroy(ImxDmaBufferAllocator *allocator)
{
	ImxDmaBufferDwlAllocator *imx_dwl_allocator = (ImxDmaBufferDwlAllocator *)allocator;

	assert(imx_dwl_allocator != NULL);
	assert(imx_dwl_allocator->dwl_instance != NULL);

	DWLRelease(imx_dwl_allocator->dwl_instance);

	free(imx_dwl_allocator);
}


static ImxDmaBuffer* imx_dma_buffer_dwl_allocator_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error)
{
	size_t actual_size;
	ImxDmaBufferDwlBuffer *imx_dwl_buffer;
	ImxDmaBufferDwlAllocator *imx_dwl_allocator = (ImxDmaBufferDwlAllocator *)allocator;

	assert(imx_dwl_allocator != NULL);
	assert(imx_dwl_allocator->dwl_instance != NULL);

	/* The DWL allocator does not have a parameter for alignment, so we resort to a trick.
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
	imx_dwl_buffer = (ImxDmaBufferDwlBuffer *)malloc(sizeof(ImxDmaBufferDwlBuffer));
	imx_dwl_buffer->parent.allocator = allocator;
	imx_dwl_buffer->actual_size = actual_size;
	imx_dwl_buffer->size = size;
	imx_dwl_buffer->mapping_refcount = 0;

	/* Initialize the DWL linear memory structure for allocation. DWL_MEM_TYPE_CPU is
	 * physically contiguous memory that can be accessed with the CPU.
	 * TODO: There is another type called "secure memory". It is selected by using the
	 * DWL_MEM_TYPE_SLICE type. Currently, it is unclear how to use it properly. */
	memset(&(imx_dwl_buffer->dwl_linear_mem), 0, sizeof(imx_dwl_buffer->dwl_linear_mem));
	imx_dwl_buffer->dwl_linear_mem.mem_type = DWL_MEM_TYPE_CPU;

	/* Perform the actual allocation. */
	if (DWLMallocLinear(imx_dwl_allocator->dwl_instance, actual_size, &(imx_dwl_buffer->dwl_linear_mem)) < 0)
	{
		if (error != NULL)
			*error = ENOMEM;
		goto cleanup;
	}

	/* Align the returned address. We also align the virtual address here, which isn't
	 * strictly necessary (alignment is only required for the physical address), but
	 * we do it regardless for sake of consistency. */
	imx_dwl_buffer->aligned_virtual_address = (uint8_t *)IMX_DMA_BUFFER_ALIGN_VAL_TO((uint8_t *)(imx_dwl_buffer->dwl_linear_mem.virtual_address), alignment);
	imx_dwl_buffer->aligned_physical_address = (imx_physical_address_t)IMX_DMA_BUFFER_ALIGN_VAL_TO((imx_physical_address_t)(imx_dwl_buffer->dwl_linear_mem.bus_address), alignment);

finish:
	return (ImxDmaBuffer *)imx_dwl_buffer;

cleanup:
	free(imx_dwl_buffer);
	imx_dwl_buffer = NULL;
	goto finish;
}

static void imx_dma_buffer_dwl_allocator_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDwlAllocator *imx_dwl_allocator = (ImxDmaBufferDwlAllocator *)allocator;
	ImxDmaBufferDwlBuffer *imx_dwl_buffer = (ImxDmaBufferDwlBuffer *)buffer;

	assert(imx_dwl_buffer != NULL);
	assert(imx_dwl_allocator != NULL);
	assert(imx_dwl_allocator->dwl_instance != NULL);

	DWLFreeLinear(imx_dwl_allocator->dwl_instance, &(imx_dwl_buffer->dwl_linear_mem));

	free(imx_dwl_buffer);
}

static uint8_t* imx_dma_buffer_dwl_allocator_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error)
{
	ImxDmaBufferDwlBuffer *imx_dwl_buffer = (ImxDmaBufferDwlBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	IMX_DMA_BUFFER_UNUSED_PARAM(error);

	assert(imx_dwl_buffer != NULL);

	if (flags == 0)
		flags = IMX_DMA_BUFFER_MAPPING_FLAG_READ | IMX_DMA_BUFFER_MAPPING_FLAG_WRITE;

	/* As mentioned above, we keep the refcount and flags around
	 * just to check correct API usage. Do this check here.
	 * (Other allocators perform more steps than this.) */
	if (imx_dwl_buffer->mapping_refcount > 0)
	{
		assert(imx_dwl_buffer->map_flags == flags);
		imx_dwl_buffer->mapping_refcount++;
	}
	else
	{
		imx_dwl_buffer->map_flags = flags;
		imx_dwl_buffer->mapping_refcount = 1;
	}

	/* DWL allocated memory is always mapped, so we just returned the aligned virtual
	 * address we stored in imx_dma_buffer_dwl_allocator_allocate(). */

	return imx_dwl_buffer->aligned_virtual_address;
}

static void imx_dma_buffer_dwl_allocator_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDwlBuffer *imx_dwl_buffer = (ImxDmaBufferDwlBuffer *)buffer;

	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);

	if (imx_dwl_buffer->mapping_refcount > 0)
		imx_dwl_buffer->mapping_refcount--;

	/* DWL allocated memory is always mapped, so we don't do anything here. */
}

static imx_physical_address_t imx_dma_buffer_dwl_allocator_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDwlBuffer *imx_dwl_buffer = (ImxDmaBufferDwlBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_dwl_buffer != NULL);
	return imx_dwl_buffer->aligned_physical_address;
}

static int imx_dma_buffer_dwl_allocator_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	IMX_DMA_BUFFER_UNUSED_PARAM(buffer);
	return -1;
}

static size_t imx_dma_buffer_dwl_allocator_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferDwlBuffer *imx_dwl_buffer = (ImxDmaBufferDwlBuffer *)buffer;
	IMX_DMA_BUFFER_UNUSED_PARAM(allocator);
	assert(imx_dwl_buffer != NULL);
	return imx_dwl_buffer->size;
}


ImxDmaBufferAllocator* imx_dma_buffer_dwl_allocator_new(int *error)
{
	ImxDmaBufferDwlAllocator *imx_dwl_allocator = (ImxDmaBufferDwlAllocator *)malloc(sizeof(ImxDmaBufferDwlAllocator));

	imx_dwl_allocator->parent.destroy = imx_dma_buffer_dwl_allocator_destroy;
	imx_dwl_allocator->parent.allocate = imx_dma_buffer_dwl_allocator_allocate;
	imx_dwl_allocator->parent.deallocate = imx_dma_buffer_dwl_allocator_deallocate;
	imx_dwl_allocator->parent.map = imx_dma_buffer_dwl_allocator_map;
	imx_dwl_allocator->parent.unmap = imx_dma_buffer_dwl_allocator_unmap;
	imx_dwl_allocator->parent.get_physical_address = imx_dma_buffer_dwl_allocator_get_physical_address;
	imx_dwl_allocator->parent.get_fd = imx_dma_buffer_dwl_allocator_get_fd;
	imx_dwl_allocator->parent.get_size = imx_dma_buffer_dwl_allocator_get_size;

	memset(&(imx_dwl_allocator->dwl_init_param), 0, sizeof(imx_dwl_allocator->dwl_init_param));

	/* Example code from the imx-vpu-hantro and imx-vpuwrap packages indicate that
	 * for a Hantro G2 decoder, the HEVC client type should be used here, and for
	 * a G1 decoder, we should use the H264 client type. The decoder version is
	 * currently selected in the libimxdmabuffer build configuration. */
#if defined(IMXDMABUFFER_DWL_USE_CLIENT_TYPE_HEVC)
	imx_dwl_allocator->dwl_init_param.client_type = DWL_CLIENT_TYPE_HEVC_DEC;
#elif defined(IMXDMABUFFER_DWL_USE_CLIENT_TYPE_H264)
	imx_dwl_allocator->dwl_init_param.client_type = DWL_CLIENT_TYPE_H264_DEC;
#else
#error Unknown client type
#endif
	imx_dwl_allocator->dwl_instance = DWLInit(&(imx_dwl_allocator->dwl_init_param));
	if (imx_dwl_allocator->dwl_instance == NULL)
	{
		if (error != NULL)
			*error = ENOMEM;
		goto cleanup;
	}

finish:
	return (ImxDmaBufferAllocator *)imx_dwl_allocator;

cleanup:
	free(imx_dwl_allocator);
	imx_dwl_allocator = NULL;
	goto finish;
}
