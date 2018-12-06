#!/usr/bin/env python


from waflib.Build import BuildContext, CleanContext, InstallContext, UninstallContext, Logs

top = '.'
out = 'build'


# the code inside fragment deliberately does an unsafe implicit cast float->char to trigger a
# compiler warning; sometimes, gcc does not tell about an unsupported parameter *unless* the
# code being compiled causes a warning
c_cflag_check_code = """
int main()
{
	float f = 4.0;
	char c = f;
	return c - 4;
}
"""
def check_compiler_flag(conf, flag, lang):
	return conf.check(fragment = c_cflag_check_code, mandatory = 0, execute = 0, define_ret = 0, msg = 'Checking for compiler switch %s' % flag, cxxflags = conf.env[lang + 'FLAGS'] + [flag], okmsg = 'yes', errmsg = 'no')  
def check_compiler_flags_2(conf, cflags, ldflags, msg):
	Logs.pprint('NORMAL', msg)
	return conf.check(fragment = c_cflag_check_code, mandatory = 0, execute = 0, define_ret = 0, msg = 'Checking if building with these flags works', cxxflags = cflags, ldflags = ldflags, okmsg = 'yes', errmsg = 'no')


def add_compiler_flags(conf, env, flags, lang, compiler, uselib = ''):
	for flag in reversed(flags):
		if type(flag) == type(()):
			flag_candidate = flag[0]
			flag_alternative = flag[1]
		else:
			flag_candidate = flag
			flag_alternative = None

		if uselib:
			flags_pattern = lang + 'FLAGS_' + uselib
		else:
			flags_pattern = lang + 'FLAGS'

		if check_compiler_flag(conf, flag_candidate, compiler):
			env.prepend_value(flags_pattern, [flag_candidate])
		elif flag_alternative:
			if check_compiler_flag(conf, flag_alternative, compiler):
				env.prepend_value(flags_pattern, [flag_alternative])


def options(opt):
	opt.add_option('--enable-debug', action = 'store_true', default = False, help = 'enable debug build [default: disabled]')
	opt.add_option('--enable-static', action = 'store_true', default = False, help = 'build static library [default: build shared library]')
	opt.add_option('--imx-linux-headers-path', action='store', default='', help='path to i.MX linux headers (where linux/mxcfb.h etc. can be found)')
	opt.add_option('--with-ion-allocator', action='store', default = 'auto', help = 'build with ION allocator support (valid values: yes/no/auto)')
	opt.add_option('--with-dwl-allocator', action='store', default = 'auto', help = 'build with ION allocator support (valid values: yes/no/auto)')
	opt.add_option('--hantro-decoder-version', action='store', default = '', help = 'Hantro decoder version to use for DWL based allocations (valid values: G1 G2)')
	opt.add_option('--hantro-headers-path', action='store', default='', help='path to hantro headers (dwl.h codec.h are checked for)')
	opt.add_option('--with-ipu-allocator', action='store', default = 'auto', help = 'build with IPU allocator support (valid values: yes/no/auto)')
	opt.add_option('--with-g2d-allocator', action='store', default = 'auto', help = 'build with G2D allocator support (valid values: yes/no/auto)')
	opt.add_option('--g2d-includes', action = 'store', default = '', help = 'path to the directory where the g2d.h header is')
	opt.add_option('--g2d-libs', action = 'store', default = '', help = 'path to the directory where the g2d library is')
	opt.add_option('--with-pxp-allocator', action='store', default = 'auto', help = 'build with PxP allocator support (valid values: yes/no/auto)')
	opt.load('compiler_c')
	opt.load('gnu_dirs')


def configure(conf):
	conf.load('compiler_c')
	conf.load('gnu_dirs')


	# check and add compiler flags

	if conf.env['CFLAGS'] and conf.env['LINKFLAGS']:
		check_compiler_flags_2(conf, conf.env['CFLAGS'], conf.env['LINKFLAGS'], "Testing compiler flags %s and linker flags %s" % (' '.join(conf.env['CFLAGS']), ' '.join(conf.env['LINKFLAGS'])))
	elif conf.env['CFLAGS']:
		check_compiler_flags_2(conf, conf.env['CFLAGS'], '', "Testing compiler flags %s" % ' '.join(conf.env['CFLAGS']))
	elif conf.env['LINKFLAGS']:
		check_compiler_flags_2(conf, '', conf.env['LINKFLAGS'], "Testing linker flags %s" % ' '.join(conf.env['LINKFLAGS']))

	compiler_flags = ['-Wextra', '-Wall', '-std=gnu99', '-pedantic', '-fPIC', '-DPIC']
	if conf.options.enable_debug:
		compiler_flags += ['-O0', '-g3', '-ggdb']
	else:
		compiler_flags += ['-O2']

	add_compiler_flags(conf, conf.env, compiler_flags, 'C', 'C')


	# misc checks and flags
	conf.env['BUILD_STATIC'] = conf.options.enable_static
	conf.env['EXTRA_USELIBS'] = []
	conf.env['EXTRA_SOURCE_FILES'] = []


	# i.MX linux header checks and flags
	if not conf.options.imx_linux_headers_path:
		conf.fatal('--imx-linux-headers-path is not set')
	if not conf.check_cc(uselib_store = 'IMXHEADERS', define_name = '', mandatory = False, includes = [conf.options.imx_linux_headers_path], header_name = 'linux/mxcfb.h'):
		conf.fatal('Could not find linux/mxcfb.h in path "%s" specified by --imx-linux-headers-path' % conf.options.imx_linux_headers_path)
	Logs.pprint('NORMAL', 'i.MX linux headers path: %s' % conf.options.imx_linux_headers_path)


	# ION allocator checks and flags
	with_ion_alloc = conf.options.with_ion_allocator
	if with_ion_alloc != 'no':
		ion_header_found = conf.check_cc(
			fragment = '''
				#include <stddef.h>
				#include <linux/ion.h>
				int main() {
					return 0;
				}
			''',
			uselib = 'IMXHEADERS',
			mandatory = False,
			execute = False,
			msg = 'Checking for ION allocator support by testing the presence of linux/ion.h'
		)
		if ion_header_found:
			conf.define('IMXDMABUFFER_ION_ALLOCATOR_ENABLED', 1)
			conf.env['EXTRA_USELIBS'] += ['IMXHEADERS']
			conf.env['EXTRA_HEADER_FILES'] += ['imxdmabuffer/imxdmabuffer_ion_allocator.h']
			conf.env['EXTRA_SOURCE_FILES'] += ['imxdmabuffer/imxdmabuffer_ion_allocator.c']
		else:
			if with_ion_alloc == 'yes':
				conf.fatal('linux/ion.h was not found in i.MX linux headers path')
			else:
				Logs.pprint('NORMAL', 'linux/ion.h was not found in i.MX linux headers path; disabling ION allocator')


	# DWL allocator checks and flags
	with_dwl_alloc = conf.options.with_dwl_allocator
	if with_dwl_alloc != 'no':
		dwl_alloc_enabled = True
		auto_check = (with_dwl_alloc == 'auto')
		hantro_decoder_version = None

		if not conf.options.hantro_decoder_version:
			if auto_check:
				Logs.pprint('NORMAL', '--hantro-decoder-version is not set; disabling DWL allocator')
				dwl_alloc_enabled = False
			else:
				conf.fatal('--hantro-decoder-version is not set')
		if dwl_alloc_enabled:
			hantro_decoder_version = conf.options.hantro_decoder_version if conf.options.hantro_decoder_version in ['G1', 'G2'] else None
			if not hantro_decoder_version:
				conf.fatal('Invalid Hantro decoder version "%s" specified' % conf.options.hantro_decoder_version)
		Logs.pprint('NORMAL', 'Hantro decoder version: %s' % hantro_decoder_version)

		if dwl_alloc_enabled and not conf.options.hantro_headers_path:
			if auto_check:
				Logs.pprint('NORMAL', '--hantro-headers-path is not set; disabling DWL allocator')
				dwl_alloc_enabled = False
			else:
				conf.fatal('--hantro-headers-path is not set')
		Logs.pprint('NORMAL', 'Hantro headers path: %s' % conf.options.hantro_headers_path)

		if dwl_alloc_enabled:
			dwl_header_found = conf.check_cc(uselib_store = 'HANTRO', define_name = '', mandatory = False, includes = [conf.options.hantro_headers_path], header_name = 'dwl.h')
			if not dwl_header_found:
				if auto_check:
					Logs.pprint('NORMAL', 'Could not find dwl.h in path "%s" specified by --hantro-headers-path; disabling DWL allocator' % conf.options.hantro_headers_path)
					dwl_alloc_enabled = False
				else:
					conf.fatal('Could not find dwl.h in path "%s" specified by --hantro-headers-path' % conf.options.hantro_headers_path)

		if dwl_alloc_enabled and not conf.check_cc(uselib_store = 'RT', mandatory = True, lib = 'rt'):
			if auto_check:
				Logs.pprint('NORMAL', 'Could not find rt library; disabling DWL allocator')
				dwl_alloc_enabled = False
			else:
				conf.fatal('Could not find rt library')

		if dwl_alloc_enabled and not conf.check_cc(uselib_store = 'HANTRO', uselib = ['HANTRO', 'RT'], mandatory = True, lib = 'hantro'):
			if auto_check:
				Logs.pprint('NORMAL', 'Could not find hantro library; disabling DWL allocator')
				dwl_alloc_enabled = False
			else:
				conf.fatal('Could not find hantro library')

		if dwl_alloc_enabled:
			conf.define('IMXDMABUFFER_DWL_ALLOCATOR_ENABLED', 1)
			if hantro_decoder_version == 'G2':
				conf.define('IMXDMABUFFER_DWL_USE_CLIENT_TYPE_HEVC', 1)
			elif hantro_decoder_version == 'G1':
				conf.define('IMXDMABUFFER_DWL_USE_CLIENT_TYPE_H264', 1)
			else:
				conf.fatal('Internal configuration error - unknown Hantro decoder type')
			conf.env['EXTRA_USELIBS'] += ['HANTRO', 'RT']
			conf.env['EXTRA_HEADER_FILES'] += ['imxdmabuffer/imxdmabuffer_dwl_allocator.h']
			conf.env['EXTRA_SOURCE_FILES'] += ['imxdmabuffer/imxdmabuffer_dwl_allocator.c']


	# IPU allocator checks and flags
	with_ipu_alloc = conf.options.with_ipu_allocator
	if with_ipu_alloc != 'no':
		ipu_header_found = conf.check_cc(fragment = '''
			#include <time.h>
			#include <sys/types.h>
			#include <linux/fb.h>
			#include <linux/ipu.h>

			int main() { return 0; }
			''',
			uselib = 'IMXHEADERS',
			mandatory = False,
			execute = False,
			msg = 'checking for linux/fb.h and the IPU header linux/ipu.h'
		)
		if ipu_header_found:
			conf.define('IMXDMABUFFER_IPU_ALLOCATOR_ENABLED', 1)
			conf.env['EXTRA_USELIBS'] += ['IMXHEADERS']
			conf.env['EXTRA_HEADER_FILES'] += ['imxdmabuffer/imxdmabuffer_ipu_allocator.h']
			conf.env['EXTRA_SOURCE_FILES'] += ['imxdmabuffer/imxdmabuffer_ipu_allocator.c', 'imxdmabuffer/imxdmabuffer_ipu_priv.c']
		else:
			if with_ipu_alloc == 'yes':
				conf.fatal('linux/fb.h and/or linux/ipu.h were not found in i.MX linux headers path')
			else:
				Logs.pprint('NORMAL', 'linux/fb.h and/or linux/ipu.h were not found in i.MX linux headers path; disabling IPU allocator')


	# G2D allocator checks and flags
	with_g2d_alloc = conf.options.with_g2d_allocator
	if with_g2d_alloc != 'no':
		g2d_libpath = [conf.options.g2d_libs] if conf.options.g2d_libs else []
		g2d_includes = [conf.options.g2d_includes] if conf.options.g2d_includes else []
		g2d_lib_found = conf.check_cc(mandatory = 0,                   libpath = g2d_libpath  , lib = 'g2d'          , uselib_store = 'IMXG2D')
		g2d_inc_found = conf.check_cc(mandatory = 0, define_name = '', includes = g2d_includes, header_name = 'g2d.h', uselib_store = 'IMXG2D')
		if g2d_lib_found and g2d_inc_found:
			conf.define('IMXDMABUFFER_G2D_ALLOCATOR_ENABLED', 1)
			conf.env['EXTRA_USELIBS'] += ['IMXG2D']
			conf.env['EXTRA_HEADER_FILES'] += ['imxdmabuffer/imxdmabuffer_g2d_allocator.h']
			conf.env['EXTRA_SOURCE_FILES'] += ['imxdmabuffer/imxdmabuffer_g2d_allocator.c']
		else:
			if with_g2d_alloc == 'yes':
				conf.fatal('G2D not found (library found: %d header found: %d)' % (g2d_lib_found != None, g2d_inc_found != None))
			else:
				Logs.pprint('NORMAL', 'G2D not found (library found: %d header found: %d); disabling G2D allocator' % (g2d_lib_found != None, g2d_inc_found != None))


	# PxP allocator checks and flags
	with_pxp_alloc = conf.options.with_pxp_allocator
	if with_pxp_alloc != 'no':
		pxp_header_found = conf.check_cc(fragment = '''
			#include <linux/pxp_device.h>

			int main() { return 0; }
			''',
			uselib = 'IMXHEADERS',
			mandatory = False,
			execute = False,
			msg = 'checking for linux/pxp_device.h'
		)
		if pxp_header_found:
			conf.define('IMXDMABUFFER_PXP_ALLOCATOR_ENABLED', 1)
			conf.env['EXTRA_USELIBS'] += ['IMXHEADERS']
			conf.env['EXTRA_HEADER_FILES'] += ['imxdmabuffer/imxdmabuffer_pxp_allocator.h']
			conf.env['EXTRA_SOURCE_FILES'] += ['imxdmabuffer/imxdmabuffer_pxp_allocator.c']
		else:
			if with_pxp_alloc == 'yes':
				conf.fatal('linux/pxp_device.h was not found in i.MX linux headers path')
			else:
				Logs.pprint('NORMAL', 'linux/pxp_device.h was not found in i.MX linux headers path; disabling PxP allocator')


	# Process the library version number
	version_node = conf.srcnode.find_node('VERSION')
	if not version_node:
		conf.fatal('Could not open VERSION file')
	with open(version_node.abspath()) as x:
		version = x.readline().splitlines()[0]
	conf.env['IMXDMABUFFER_VERSION'] = version
	conf.define('IMXDMABUFFER_VERSION', version)
	Logs.pprint('NORMAL', 'libimxdmabuffer version %s' % version)


	# Write the config header
	conf.write_config_header('imxdmabuffer_config.h')


def build(bld):
	bld(
		features = ['c', 'cstlib' if bld.env['BUILD_STATIC'] else 'cshlib'],
		includes = ['.'],
		uselib = bld.env['EXTRA_USELIBS'],
		source = ['imxdmabuffer/imxdmabuffer.c'] + bld.env['EXTRA_SOURCE_FILES'],
		name = 'imxdmabuffer',
		target = 'imxdmabuffer',
		vnum = bld.env['IMXDMABUFFER_VERSION'],
		install_path = "${LIBDIR}"
	)

	bld.install_files('${PREFIX}/include/imxdmabuffer/', ['imxdmabuffer_config.h', 'imxdmabuffer/imxdmabuffer.h', 'imxdmabuffer/imxdmabuffer_physaddr.h'] + bld.env['EXTRA_HEADER_FILES'])

	bld(
		features = ['subst'],
		source = "libimxdmabuffer.pc.in",
		target = "libimxdmabuffer.pc",
		install_path = "${LIBDIR}/pkgconfig"
	)

	bld(
		features = ['c', 'cprogram'],
		includes = ['.'],
		use = 'imxdmabuffer',
		source = ['test/test-alloc.c'],
		target = 'test-alloc',
		install_path = None
	)
