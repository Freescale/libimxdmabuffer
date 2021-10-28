#ifndef IMXDMABUFFER_ION_ALLOCATOR_H
#define IMXDMABUFFER_ION_ALLOCATOR_H

#include "imxdmabuffer.h"


#ifdef __cplusplus
extern "C" {
#endif


#define IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_ION_FD (-1)
#define IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_ID_MASK (1 << 0)
#define IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_FLAGS (0)


/* Creates a new DMA buffer allocator that uses the modified i.MX Android ION allocator.
 *
 * The i.MX kernel contains a modified version of the ION allocator, which
 * got extra ioctl's added for handling physical addresses. Buffers are shared
 * via DMA-BUF file descriptors.
 *
 * One restriction of ION is that there can not be more more than one client per
 * user process. A client is represented by a file descriptor that corresponds to
 * the device node /dev/ion . If the process already opened that device node, then
 * an error would occur if this function is called, because it would try to open the
 * device node, which in turn would mean that there would be an attempt to get a
 * second /dev/ion file descriptor in the same process, and as mentioned before,
 * this is not permitted.
 *
 * The solution to this is the ion_fd argument. If set to a negative value, then
 * the allocator will open its own internal file descriptor to /dev/ion (and close
 * it when it gets destroyed). If however ion_fd is set to a valid file descriptor,
 * then the allocator uses it instead and does not try to create its own /dev/ion
 * file descriptor (and this external /dev/ion file descriptor is not closed when
 * the allocator is destroyed).
 *
 * @param ion_fd /dev/ion file descriptor to use, or a negative value if the allocator
 *        shall open and use its own file descriptor. The preprocessor macro
 *        IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_ION_FD can be used for the latter case.
 * @param ion_heap_id_mask Bitmask for selecting ION heaps during allocations. This is
 *        a bitwise OR combination of heap mask IDs. The IDs are combined by using their
 *        values as powers of 2. Example: mask = (1 << ID_1) | (1 << ID_2) .
 *        The IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_ID_MASK macro selects heap ID #0.
 *        Note however that starting with kernel 4.14.34, this argument is ignored,
 *        since the heap ID mask is autodetected (all heaps with type ION_HEAP_TYPE_DMA
 *        are selected).
 * @param ion_heap_flags Flags to pass to the ION heap during allocations. The
 *        preprocessor macro IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_FLAGS can be
 *        used as a default value (= no flags selected).
 * @param error If this pointer is non-NULL, and if an error occurs, then the integer
 *        the pointer refers to is set to an error code from errno.h. If creating
 *        the allocator succeeds, the integer is not modified.
 * @return Pointer to the newly created ION DMA allocator, or NULL in case of an error.
 */
ImxDmaBufferAllocator* imx_dma_buffer_ion_allocator_new(int ion_fd, unsigned int ion_heap_id_mask, unsigned int ion_heap_flags, int *error);


/* Allocates a DMA buffer via ION and returns the file descriptor representing the buffer.
 *
 * This function is useful for assembling a custom allocator that uses ION. This may
 * be necessary in frameworks that have their own memory allocation infrastructure
 * and already have code in place for mapping/unmapping file descriptors for example.
 * Usually it is better to just use the predefined ION imxdmabuffer allocator instead.
 * Use imx_dma_buffer_ion_allocator_new() to create one instance.
 *
 * NOTE: Currently, the alignment argument is not actually doing anything. This is
 * because there is no clear way to enforce a minimum physical address alignment over
 * ION. In the ION ImxDmaBufferAllocator implementation, it would be possible to use
 * this value by allocating a bit more memory than requested & aligning the physical
 * and mapped virtual addresses manually. But, this only works if the caller only ever
 * accesses the memory block over the imx_dma_buffer_* functions. With ION allocation
 * though it is _also_ possible to access it by memory-mapping its DMA-BUF fd, and
 * any virtual address that gets memory-mapped that way will _not_ be aligned in the
 * same way. This means that there are no options left for enforcing specific memory
 * alignment with ION at this stage. Fortunately, allocated pages typically are aligned
 * at a page level, meaning an alignment to 4096 bytes. This alignment typically
 * fulfills requirement of all practical use cases (since the requirements usually
 * are just something like "align to 8 bytes", "align to 16 bytes" etc.) Still, leaving
 * this argument in place, in case future ION revisions allow for specifying alignment.
 *
 * @param ion_fd /dev/ion file descriptor to use. Must not be negative.
 * @param size Size of the DMA buffer to allocate, in bytes. Must be greater than 0.
 * @param alignment Memory alignment for the newly allocated DMA buffer.
 * @param ion_heap_id_mask Bitmask for ION heaps during allocations. For more details,
 *        see the imx_dma_buffer_ion_allocator_new() ion_heap_id_mask reference.
 * @param ion_heap_flags Flags to pass to the ION heap during allocations.
 * @param error If this pointer is non-NULL, and if an error occurs, then the
 *        integer the pointer refers to is set to an error code from errno.h. If
 *        allocation succeeds, the integer is not modified.
 * @return DMA-BUF file descriptor for the allocated DMA buffer, or a negative value
 *         if allocation failed.
 */
int imx_dma_buffer_ion_allocate_dmabuf(int ion_fd, size_t size, size_t alignment, unsigned int ion_heap_id_mask, unsigned int ion_heap_flags, int *error);

/* Retrieves a physical address for the DMA buffer with the given DMA-BUF FD.
 *
 * @param ion_fd /dev/ion file descriptor to use. Must not be negative.
 * @param dmabuf_fd DMA-BUF file descriptor to retrieve a physical address for.
 * @param error If this pointer is non-NULL, and if an error occurs, then the
 *        integer the pointer refers to is set to an error code from errno.h. If
 *        retrieving the physical address succeeds, the integer is not modified.
 * @return Physical address to the DMA buffer represented by the DMA-BUF FD, or
 *         0 if retrieving the address failed.
 */
imx_physical_address_t imx_dma_buffer_ion_get_physical_address_from_dmabuf_fd(int ion_fd, int dmabuf_fd, int *error);


#ifdef __cplusplus
}
#endif


#endif /* IMXDMABUFFER_ION_ALLOCATOR_H */
