libimxdmabuffer - library for allocating and managing physically contiguous memory ("DMA memory" or "DMA buffers") on i.MX devices
==================================================================================================================================

The purpose of this library is to provide an API for allocating memory blocks
that are physically contiguous. Typically, a process allocates *virtual*
memory blocks, which are contiguous, but only in the virtual address space
the process got assigned by the operating system. Underneath the virtual
address space layer, the *actual* memory might be fragmented. The machine's
MMU is what typically takes care of mapping parts of the physical memory
into one contiguous virtual address space.

However, DMA (Direct Memory Access) channels usually cannot work with virtually
contiguous but physically fragmented memory blocks, because they are not
linked to the MMU. Therefore, for such DMA transfers, it is necessary to make
sure that the memory blocks that are to be transferred are *physically*
contiguous. This library provides APIs for allocators that can produce memory
blocks which are physicall contiguous. These memory blocks are referred to as
"contiguous memory", "physical memory", "DMA memory", or "DMA buffers".

This library is designed specifically for DMA buffer allocations on i.MX
machines. One can use a specific allocator, or use the default one. What the
default allocator is gets decided by the libimxdmabuffer build configuration.
On different i.MX variant, different allocators are available, which is an
important reason for why this library was written: To provide one consistent
interface for DMA buffer allocation in various i.MX variants, even though the
underlying allocator might differ from i.MX variant to i.MX variant.


License
-------

This library is licensed under the LGPL v2.1.


Available allocators
--------------------

* DWL: Uses the Hantro DWL API for allocation. Only works on machines
  with a Hantro VPU decoder.
* G2D: Uses the Vivante G2D API for allocation. Only works on machines
  with the G2D API enabled. i.MX6 machines with the Vivante GPU drivers
  have this API.
* ION: Uses the ION allocator that has been originally ported from
  Android to Linux, and has further been enabled and modified in the
  linux-imx kernel. Not available on i.MX6, though this seems to be
  purely a kernel configuration issue.
* IPU: Uses IPU ioctls for allocation. Available on machines with an
  IPU, which includes most i.MX6 variants, but no i.MX7 or i.MX8 ones.
* PxP: Uses PxP ioctls for allocation. Available on machines with a
  PxP, which includes the i.MX7, and some i.MX6 variants.


Building and installing
-----------------------

This project uses the [waf meta build system](https://code.google.com/p/waf/).
To configure , first set the following environment variables to whatever is
necessary for cross compilation for your platform:

* `CC`
* `CFLAGS`
* `LDFLAGS`
* `PKG_CONFIG_PATH`
* `PKG_CONFIG_SYSROOT_DIR`

Then, run:

    ./waf configure --prefix=PREFIX

(The aforementioned environment variables are only necessary for this
configure call.)

PREFIX defines the installation prefix, that is, where the built binaries
will be installed.

Once configuration is complete, run:

    ./waf

This builds the library.
Finally, to install, run:

    ./waf install

This will install the headers in `$PREFIX/include/imxdmabuffer/` , the
libraries in `$PREFIX/lib/` , and generate a pkg-config .pc file, which is
placed in `$PREFIX/lib/pkgconfig/` .


Configuring the default allocator
---------------------------------

By default, this is the order by which allocators are tried:

ION -> DWL -> IPU -> G2D -> PxP

The first one that is available will be used. Individual allocators can be
enabled or disabled by using the `--with-<allocname>-allocator=<value>`
configuration switches, where `<allocname`> is the lowercase name of the
allocator, and `<value>` is either `yes`, `no`, or `auto`. `yes` means that
the build configuration fails if the allocator is available. `no` disables
the allocator. `auto` is similar to `yes`, except that build configuration
won't fail if the allocator is not available, it's just that allocator that
will be turned off in the build.


API documentation
-----------------

The API is documented in this header:

* `imxdmabuffer/imxdmabuffer.h` : main allocation API
