#include <errno.h>
#include <sys/ioctl.h>

/* This is necessary to turn off these warning that originate in ipu.h :
 *   "ISO C99 doesn’t support unnamed structs/unions"    */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include <linux/ipu.h>

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "imxdmabuffer_ipu_priv.h"


/* These functions are isolated in this separate .c file to avoid
 * conflicts between uint*_t definitions from linux/ipu.h and definitions
 * from stdint.h. */


imx_physical_address_t imx_dma_buffer_ipu_allocate(int ipu_fd, size_t size, int *error)
{
	dma_addr_t m = (dma_addr_t)size;
	if (ioctl(ipu_fd, IPU_ALLOC, &m) < 0)
	{
		if (error != NULL)
			*error = errno;
		return 0;
	}
	else
		return (imx_physical_address_t)m;
}


void imx_dma_buffer_ipu_deallocate(int ipu_fd, imx_physical_address_t physical_address)
{
	dma_addr_t m = physical_address;
	ioctl(ipu_fd, IPU_FREE, &m);
}
