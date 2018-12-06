#include <stdio.h>
#include <string.h>

#include "imxdmabuffer_config.h"
#include "imxdmabuffer/imxdmabuffer.h"
#include "imxdmabuffer/imxdmabuffer_priv.h"

#ifdef IMXDMABUFFER_ION_ALLOCATOR_ENABLED
#include "imxdmabuffer/imxdmabuffer_ion_allocator.h"
#endif

#ifdef IMXDMABUFFER_DWL_ALLOCATOR_ENABLED
#include "imxdmabuffer/imxdmabuffer_dwl_allocator.h"
#endif

#ifdef IMXDMABUFFER_IPU_ALLOCATOR_ENABLED
#include "imxdmabuffer/imxdmabuffer_ipu_allocator.h"
#endif

#ifdef IMXDMABUFFER_G2D_ALLOCATOR_ENABLED
#include "imxdmabuffer/imxdmabuffer_g2d_allocator.h"
#endif

#ifdef IMXDMABUFFER_PXP_ALLOCATOR_ENABLED
#include "imxdmabuffer/imxdmabuffer_pxp_allocator.h"
#endif


int check_allocation(ImxDmaBufferAllocator *allocator, char const *name)
{
	static size_t const expected_buffer_size = 4096;
	static size_t const expected_alignment = 16;
	size_t actual_buffer_size;
	int retval = 0;
	int err;
	void *mapped_virtual_address = NULL;
	imx_physical_address_t physical_address;
	ImxDmaBuffer *dma_buffer = NULL;

	if (allocator == NULL)
	{
		fprintf(stderr, "Could not create %s allocator\n", name);
		goto finish;
	}

	dma_buffer = imx_dma_buffer_allocate(allocator, expected_buffer_size, expected_alignment, &err);
	if (dma_buffer == NULL)
	{
		fprintf(stderr, "Could not allocate DMA buffer with %s allocator: %s (%d)\n", name, strerror(err), err);
		goto finish;
	}

	actual_buffer_size = imx_dma_buffer_get_size(dma_buffer);
	if (actual_buffer_size != expected_buffer_size)
	{
		fprintf(stderr, "DMA buffer allocated with %s allocator has incorrect size: expected %zu got %zu\n", name, expected_buffer_size, actual_buffer_size);
		goto finish;
	}

	mapped_virtual_address = imx_dma_buffer_map(dma_buffer, 0, &err);
	if (mapped_virtual_address == NULL)
	{
		fprintf(stderr, "Could not mapped DMA buffer allocated with %s allocator: %s (%d)\n", name, strerror(err), err);
		goto finish;
	}

	physical_address = imx_dma_buffer_get_physical_address(dma_buffer);
	if (physical_address == 0)
	{
		fprintf(stderr, "Could not get physical address for DMA buffer allocated %s allocator\n", name);
		goto finish;
	}
	if ((physical_address & expected_alignment) != 0)
	{
		fprintf(stderr, "Physical address %" IMX_PHYSICAL_ADDRESS_FORMAT " for DMA buffer allocated %s allocator is not aligned to %zu-byte boundaries\n", physical_address, name, expected_alignment);
		goto finish;
	}

	fprintf(stderr, "%s allocator works correctly\n", name);
	retval = 1;

finish:
	if (mapped_virtual_address != NULL)
		imx_dma_buffer_unmap(dma_buffer);
	if (dma_buffer != NULL)
		imx_dma_buffer_deallocate(dma_buffer);
	if (allocator != NULL)
		imx_dma_buffer_allocator_destroy(allocator);

	return retval;
}


int main()
{
	int err;
	ImxDmaBufferAllocator *allocator;
	int retval = 0;

#ifdef IMXDMABUFFER_ION_ALLOCATOR_ENABLED
	allocator = imx_dma_buffer_ion_allocator_new(-1, IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_ID_MASK, IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_FLAGS, &err);
	if (allocator == NULL)
	{
		fprintf(stderr, "Could not create ION allocator: %s (%d)\n", strerror(err), err);
		retval = -1;
	}
	else if (check_allocation(allocator, "ION") == 0)
		retval = -1;
#endif

#ifdef IMXDMABUFFER_DWL_ALLOCATOR_ENABLED
	allocator = imx_dma_buffer_dwl_allocator_new(&err);
	if (allocator == NULL)
	{
		fprintf(stderr, "Could not create DWL allocator: %s (%d)\n", strerror(err), err);
		retval = -1;
	}
	else if (check_allocation(allocator, "DWL") == 0)
		retval = -1;
#endif

#ifdef IMXDMABUFFER_IPU_ALLOCATOR_ENABLED
	allocator = imx_dma_buffer_ipu_allocator_new(-1, &err);
	if (allocator == NULL)
	{
		fprintf(stderr, "Could not create IPU allocator: %s (%d)\n", strerror(err), err);
		retval = -1;
	}
	else if (check_allocation(allocator, "IPU") == 0)
		retval = -1;
#endif

#ifdef IMXDMABUFFER_G2D_ALLOCATOR_ENABLED
	allocator = imx_dma_buffer_g2d_allocator_new();
	if (check_allocation(allocator, "G2D") == 0)
		retval = -1;
#endif

#ifdef IMXDMABUFFER_PXP_ALLOCATOR_ENABLED
	allocator = imx_dma_buffer_pxp_allocator_new(-1, &err);
	if (allocator == NULL)
	{
		fprintf(stderr, "Could not create PxP allocator: %s (%d)\n", strerror(err), err);
		retval = -1;
	}
	else if (check_allocation(allocator, "PxP") == 0)
		retval = -1;
#endif
	
	return retval;
}