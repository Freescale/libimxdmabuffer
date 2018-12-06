#ifndef IMXDMABUFFER_H
#define IMXDMABUFFER_H

#include <stddef.h>
#include <stdint.h>
#include "imxdmabuffer_physaddr.h"


#ifdef __cplusplus
extern "C" {
#endif


/* ImxDmaBufferMappingFlags: Flags for the ImxVpuDMABufferAllocator's
 * map vfuncs. These flags can be bitwise-OR combined. */
typedef enum
{
	/* Map memory for CPU write access. */
	IMX_DMA_BUFFER_MAPPING_FLAG_WRITE       = (1UL << 0),
	/* Map memory for CPU read access. */
	IMX_DMA_BUFFER_MAPPING_FLAG_READ        = (1UL << 1),
	/* Map memory privately (default: shared mapping). Writing to
	 * a memory mapping that was mapped with this flag will not
	 * change the underlying DMA memory. Instead, it uses a
	 * copy-on-write scheme. */
	IMX_DMA_BUFFER_MAPPING_FLAG_MAP_PRIVATE = (1UL << 2)
}
ImxDmaBufferMappingFlags;


typedef struct _ImxDmaBuffer ImxDmaBuffer;
typedef struct _ImxDmaBufferAllocator ImxDmaBufferAllocator;
typedef struct _ImxWrappedDmaBuffer ImxWrappedDmaBuffer;


#define IMX_DMA_BUFFER_PADDING 8


/* ImxDmaBuffer:
 *
 * Opaque object containing a DMA buffer (a physically contiguous
 * memory block that can be used for transmissions through DMA channels).
 * Its structure is defined by the allocator which created the object.
 */
struct _ImxDmaBuffer
{
	ImxDmaBufferAllocator *allocator;
};


/* ImxDmaBufferAllocator:
 *
 * This structure contains function pointers (referred to as "vfuncs") which define an allocator
 * for ImxDmaBuffer instances. It is possible to define a custom allocator, which is useful for
 * tracing memory allocations, and for hooking up any existing allocation mechanisms.
 *
 * The vfuncs typically are not called directly from the outside, but by using the corresponding
 * imx_dma_buffer_* functions() instead. See the documentation of these functions for more details
 * about what the vfuncs do. 
 */
struct _ImxDmaBufferAllocator
{
	void (*destroy)(ImxDmaBufferAllocator *allocator);

	ImxDmaBuffer* (*allocate)(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error);
	void (*deallocate)(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);

	uint8_t* (*map)(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error);
	void (*unmap)(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);

	imx_physical_address_t (*get_physical_address)(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
	int (*get_fd)(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);

	size_t (*get_size)(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);

	void* _reserved[IMX_DMA_BUFFER_PADDING];
};


/* Creates a new DMA buffer allocator.
 *
 * This uses one of the several available i.MX DMA allocators internally. Which
 * one is used is determined by the build configuration of libimxdmabuffer.
 *
 * @param error If this pointer is non-NULL, and if an error occurs, then the integer
 *        the pointer refers to is set to an error code from errno.h. If creating
 *        the allocator succeeds, the integer is not modified.
 * @return Pointer to the newly created DMA allocator, or NULL in case of an error.
 */
ImxDmaBufferAllocator* imx_dma_buffer_allocator_new(int *error);

/* Destroys a previously created DMA buffer allocator.
 *
 * After this call, the allocator is fully destroyed, and must not be used anymore.
 * Also, any existing DMA buffers that have been allocated by this allocator will be
 * deallocated.
 */
void imx_dma_buffer_allocator_destroy(ImxDmaBufferAllocator *allocator);

/* Allocates a DMA buffer.
 *
 * For deallocating DMA buffers, use imx_dma_buffer_deallocate().
 *
 * Allocated buffers can have their physical addresses aligned. The alignment is
 * in bytes. An alignment of 1 or 0 means that no alignment is required. The
 * alignment is only required for the buffer's physical address, not for mapped
 * virtual addresses. Alignment does not reduce the accessible size of the buffer.
 * If for example the required alignment is 32 bytes, and the underlying allocation
 * mechanism does not accept an alignment parameter, then the allocated buffer will
 * internally have a size that is buffer than the one specified here, and it will
 * increase the value of the physical address if necessary to make it align to 32.
 *
 * @param allocator Allocator to use.
 * @param size Size of the buffer to allocate, in bytes. Must be at least 1.
 * @param alignment Physical address alignment, in bytes.
 * @param error If this pointer is non-NULL, and if an error occurs, then the integer
 *        the pointer refers to is set to an error code from errno.h. If allocation
 *        succeeds, the integer is not modified.
 * @return Pointer to the newly allocated DMA buffer if allocation succeeded,
 *         or NULL in case of an error.
 */
ImxDmaBuffer* imx_dma_buffer_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error);

/* Deallocates a DMA buffer.
 *
 * After this call, the buffer is fully deallocated, and must not be accessed anymore.
 */
void imx_dma_buffer_deallocate(ImxDmaBuffer *buffer);

/* Maps a DMA buffer to the local address space, and returns the virtual address to this space.
 *
 * Trying to map an already mapped buffer does not re-map. Instead, it increments an
 * internal reference counter, and returns the same mapped virtual address as before.
 * This means that imx_dma_buffer_unmap() must be called exactly the same number of times
 * imx_dma_buffer_map() was called on the same DMA buffer in order to be actually unmapped.
 *
 * @param flags Bitwise OR combination of flags (or 0 if no flags are used, in which case it
 *        will map in regular read/write mode). See ImxDmaBufferMappingFlags for a list of
 *        valid flags.
 * @param error If this pointer is non-NULL, and an error occurs, then the integer
 *        the pointer refers to is set to an error code from errno.h. If mapping
 *        succeeds, the integer is not modified.
 * @return Pointer with the address to the mapped region in the virtual address space where
 *         the data from the DMA buffer can be accessed, or NULL in case of an error.
 */
uint8_t* imx_dma_buffer_map(ImxDmaBuffer *buffer, unsigned int flags, int *error);

/* Unmaps a DMA buffer.
 *
 * If the buffer isn't currently mapped, this function does nothing. As explained in
 * imx_dma_buffer_map(), the buffer isn't actually unmapped until the internal reference
 * counter reaches zero.
 */
void imx_dma_buffer_unmap(ImxDmaBuffer *buffer);

/* Gets the physical address associated with the DMA buffer.
 *
 * This address points to the start of the buffer in the physical address space. The
 * physical address will be aligned to the value that was specified by the alignment
 * argument in the imx_dma_buffer_allocate() function that allocated this DMA buffer.
 *
 * This function can also be called while the DMA buffer is memory-mapped.
 */
imx_physical_address_t imx_dma_buffer_get_physical_address(ImxDmaBuffer *buffer);

/* Returns a file descriptor associated with the DMA buffer (if one exists).
 *
 * If the underlying DMA memory allocator uses file descriptors, then this function
 * returns the file descriptor associated with the DMA buffer. If no such file
 * descriptor exists, -1 is returned.
 *
 * This function can also be called while the DMA buffer is memory-mapped.
 */
int imx_dma_buffer_get_fd(ImxDmaBuffer *buffer);

/* Returns the size of the buffer, in bytes.
 *
 * This function can also be called while the DMA buffer is memory-mapped.
 */
size_t imx_dma_buffer_get_size(ImxDmaBuffer *buffer);


/* ImxWrappedDmaBuffer:
 *
 * Structure for wrapping existing DMA buffers. This is useful for interfacing with
 * existing buffers that were not allocated by libimxdmabuffer.
 *
 * First, initialize the structure with imx_dma_buffer_init_wrapped_buffer().
 * Then fill the fd, physical_address, and size values.
 *
 * map_func / unmap_func are used in the imx_dma_buffer_map() / imx_dma_buffer_unmap()
 * calls. If these function pointers are NULL, no mapping will be done.
 * NOTE: imx_dma_buffer_map() will return a NULL pointer in this case.
 */
struct _ImxWrappedDmaBuffer
{
	ImxDmaBuffer parent;

	uint8_t* (*map)(ImxWrappedDmaBuffer *wrapped_dma_buffer, unsigned int flags, int *error);
	void (*unmap)(ImxWrappedDmaBuffer *wrapped_dma_buffer);

	int fd;
	imx_physical_address_t physical_address;
	size_t size;
};

/* Call for initializing wrapped DMA buffer structures.
 * Always call this before further using such a structure. */
void imx_dma_buffer_init_wrapped_buffer(ImxWrappedDmaBuffer *buffer);


#ifdef __cplusplus
}
#endif


#endif /* IMXDMABUFFER_H */
