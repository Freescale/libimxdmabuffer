#ifndef IMXDMABUFFER_PHYSADDR_H
#define IMXDMABUFFER_PHYSADDR_H


#ifdef __cplusplus
extern "C" {
#endif


/* Format and for printf-compatible format-strings.
 * Example use: printf("physical address: %" IMX_PHYSICAL_ADDRESS_FORMAT, phys_addr */
#define IMX_PHYSICAL_ADDRESS_FORMAT "#lx"


/* Typedef for physical addresses */
typedef unsigned long imx_physical_address_t;


#ifdef __cplusplus
}
#endif


#endif // IMXDMABUFFER_PHYSADDR_H
