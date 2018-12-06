#ifndef IMXDMABUFFER_IPU_PRIV_H
#define IMXDMABUFFER_IPU_PRIV_H

#include <stddef.h>
#include "imxdmabuffer_physaddr.h"


#ifdef __cplusplus
extern "C" {
#endif


imx_physical_address_t imx_dma_buffer_ipu_allocate(int ipu_fd, size_t size, int *error);
void imx_dma_buffer_ipu_deallocate(int ipu_fd, imx_physical_address_t physical_address);


#ifdef __cplusplus
}
#endif


#endif /* IMXDMABUFFER_IPU_PRIV_H */
