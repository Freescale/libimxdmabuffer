#ifndef IMXDMABUFFER_PRIV_H
#define IMXDMABUFFER_PRIV_H

#include <stdint.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif


#define IMX_DMA_BUFFER_UNUSED_PARAM(x) ((void)(x))
#define IMX_DMA_BUFFER_ALIGN_VAL_TO(LENGTH, ALIGN_SIZE)  ( ((uintptr_t)(((uint8_t*)(LENGTH)) + (ALIGN_SIZE) - 1) / (ALIGN_SIZE)) * (ALIGN_SIZE) )


#ifdef __cplusplus
}
#endif


#endif /* IMXDMABUFFER_PRIV_H */
