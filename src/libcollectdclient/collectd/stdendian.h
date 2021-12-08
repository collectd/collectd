/*
 *   stdendian.h
 *
 *   This header defines the following endian macros as defined here:
 *   http://austingroupbugs.net/view.php?id=162
 *
 *     BYTE_ORDER         this macro shall have a value equal to one
 *                        of the *_ENDIAN macros in this header.
 *     LITTLE_ENDIAN      if BYTE_ORDER == LITTLE_ENDIAN, the host
 *                        byte order is from least significant to
 *                        most significant.
 *     BIG_ENDIAN         if BYTE_ORDER == BIG_ENDIAN, the host byte
 *                        order is from most significant to least
 *                        significant.
 *
 *   The following are defined as macros:
 *
 *     uint16_t bswap16(uint16_t x);
 *     uint32_t bswap32(uint32_t x);
 *     uint64_t bswap64(uint64_t x);

 *     uint16_t htobe16(uint16_t x);
 *     uint16_t htole16(uint16_t x);
 *     uint16_t be16toh(uint16_t x);
 *     uint16_t le16toh(uint16_t x);
 *
 *     uint32_t htobe32(uint32_t x);
 *     uint32_t htole32(uint32_t x);
 *     uint32_t be32toh(uint32_t x);
 *     uint32_t le32toh(uint32_t x);
 *
 *     uint64_t htobe64(uint64_t x);
 *     uint64_t htole64(uint64_t x);
 *     uint64_t be64toh(uint64_t x);
 *     uint64_t le64toh(uint64_t x);
 *
 *   The header defines the following macro for OpenCL compatibility
 *
 https://www.khronos.org/registry/cl/sdk/2.0/docs/man/xhtml/preprocessorDirectives.html
 *
 *     __ENDIAN_LITTLE__  if BYTE_ORDER == LITTLE_ENDIAN then this
 *                        macro is present for OpenCL compatibility
 *
 *   The implementation provides a uniform interface to endian macros using only
 *   system headers on recent Linux, Darwin, FreeBSD, Solaris and Windows
 systems.
 *
 *   This approach is intended to avoid the need for preflight configure
 scripts.
 *   An alternative approach would be to test compiler CPU architecture marcros.
 *
 *   This header has had *limited* testing on recent C11/C++11 compilers and is
 *   based on the austin bug tracker interface, manpages, and headers present in
 *   Linux, FreeBSD, Windows, Solaris and Darwin.
 *
 *   The header uses __builtin_bswapXX intrinsic with GCC/Clang (__GNUC__) on
 *   platforms that do not provide bswap16, bswap32, bswap64 (Darwin)
 *
 *   Public Domain.
 */

#if HAVE_STDINT_H
#include <stdint.h>
#endif

/* Linux / GLIBC */
#if defined(__linux__) || defined(__GLIBC__)
#include <byteswap.h>
#include <endian.h>
#define __ENDIAN_DEFINED 1
#define __BSWAP_DEFINED 1
#define __HOSTSWAP_DEFINED 1
#define _BYTE_ORDER __BYTE_ORDER
#define _LITTLE_ENDIAN __LITTLE_ENDIAN
#define _BIG_ENDIAN __BIG_ENDIAN
#define bswap16(x) bswap_16(x)
#define bswap32(x) bswap_32(x)
#define bswap64(x) bswap_64(x)
#endif /* __linux__ || __GLIBC__ */

/* BSD */
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__) ||   \
    defined(__OpenBSD__)
#include <sys/endian.h>
#define __ENDIAN_DEFINED 1
#define __BSWAP_DEFINED 1
#define __HOSTSWAP_DEFINED 1
#endif /* BSD */

/* Solaris */
#if defined(sun)
#include <sys/byteorder.h>
#include <sys/isa_defs.h>
#define bswap16(x) BSWAP_16(x)
#define bswap32(x) BSWAP_32(x)
#define bswap64(x) BSWAP_64(x)
/* sun headers don't set a value for _LITTLE_ENDIAN or _BIG_ENDIAN */
#if defined(_LITTLE_ENDIAN)
#undef _LITTLE_ENDIAN
#define _LITTLE_ENDIAN 1234
#define _BIG_ENDIAN 4321
#define _BYTE_ORDER _LITTLE_ENDIAN
#elif defined(_BIG_ENDIAN)
#undef _BIG_ENDIAN
#define _LITTLE_ENDIAN 1234
#define _BIG_ENDIAN 4321
#define _BYTE_ORDER _BIG_ENDIAN
#endif
#define __ENDIAN_DEFINED 1
#endif /* sun */

/* AIX */
#if defined(_AIX)
#include <sys/machine.h>
#if BYTE_ORDER == LITTLE_ENDIAN
#define _LITTLE_ENDIAN 1234
#define _BIG_ENDIAN 4321
#define _BYTE_ORDER _LITTLE_ENDIAN
#elif BYTE_ORDER == BIG_ENDIAN
#define _LITTLE_ENDIAN 1234
#define _BIG_ENDIAN 4321
#define _BYTE_ORDER _BIG_ENDIAN
#else
#error Could not determine CPU byte order for AIX
#endif
#define __ENDIAN_DEFINED 1
#endif /* AIX */

/* Windows */
#if defined(_WIN32) || defined(_MSC_VER)
/* assumes all Microsoft targets are little endian */
#define _LITTLE_ENDIAN 1234
#define _BIG_ENDIAN 4321
#define _BYTE_ORDER _LITTLE_ENDIAN
#define __ENDIAN_DEFINED 1
#endif /* _MSC_VER */

/* OS X */
#if defined(__APPLE__)
#include <machine/endian.h>
#define _BYTE_ORDER BYTE_ORDER
#define _LITTLE_ENDIAN LITTLE_ENDIAN
#define _BIG_ENDIAN BIG_ENDIAN
#define __ENDIAN_DEFINED 1
#endif /* __APPLE__ */

/* OpenCL */
#if defined(__OPENCL_VERSION__)
#define _LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#if defined(__ENDIAN_LITTLE__)
#define _BYTE_ORDER _LITTLE_ENDIAN
#else
#define _BYTE_ORDER _BIG_ENDIAN
#endif
#define bswap16(x) as_ushort(as_uchar2(ushort(x)).s1s0)
#define bswap32(x) as_uint(as_uchar4(uint(x)).s3s2s1s0)
#define bswap64(x) as_ulong(as_uchar8(ulong(x)).s7s6s5s4s3s2s1s0)
#define __ENDIAN_DEFINED 1
#define __BSWAP_DEFINED 1
#endif

/* Unknown */
#if !__ENDIAN_DEFINED
#error Could not determine CPU byte order
#endif

/* POSIX - http://austingroupbugs.net/view.php?id=162 */
#ifndef BYTE_ORDER
#define BYTE_ORDER _BYTE_ORDER
#endif
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN _LITTLE_ENDIAN
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN _BIG_ENDIAN
#endif

/* OpenCL compatibility - define __ENDIAN_LITTLE__ on little endian systems */
#if _BYTE_ORDER == _LITTLE_ENDIAN
#if !defined(__ENDIAN_LITTLE__)
#define __ENDIAN_LITTLE__ 1
#endif
#endif

/* Byte swap macros */
#if !__BSWAP_DEFINED

#ifndef bswap16
/* handle missing __builtin_bswap16
 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52624 */
#if defined __GNUC__
#define bswap16(x) __builtin_bswap16(x)
#else
#define bswap16(x)                                                             \
  ((uint16_t)((((uint16_t)(x)&0xff00) >> 8) | (((uint16_t)(x)&0x00ff) << 8)))
#endif
#endif

#ifndef bswap32
#if defined __GNUC__
#define bswap32(x) __builtin_bswap32(x)
#else
#define bswap32(x)                                                             \
  ((uint32_t)(                                                                 \
      (((uint32_t)(x)&0xff000000) >> 24) | (((uint32_t)(x)&0x00ff0000) >> 8) | \
      (((uint32_t)(x)&0x0000ff00) << 8) | (((uint32_t)(x)&0x000000ff) << 24)))
#endif
#endif

#ifndef bswap64
#if defined __GNUC__
#define bswap64(x) __builtin_bswap64(x)
#else
#define bswap64(x)                                                             \
  ((uint64_t)((((uint64_t)(x)&0xff00000000000000ull) >> 56) |                  \
              (((uint64_t)(x)&0x00ff000000000000ull) >> 40) |                  \
              (((uint64_t)(x)&0x0000ff0000000000ull) >> 24) |                  \
              (((uint64_t)(x)&0x000000ff00000000ull) >> 8) |                   \
              (((uint64_t)(x)&0x00000000ff000000ull) << 8) |                   \
              (((uint64_t)(x)&0x0000000000ff0000ull) << 24) |                  \
              (((uint64_t)(x)&0x000000000000ff00ull) << 40) |                  \
              (((uint64_t)(x)&0x00000000000000ffull) << 56)))
#endif
#endif

#endif

/* Host swap macros */
#ifndef __HOSTSWAP_DEFINED
#if _BYTE_ORDER == _LITTLE_ENDIAN
#define htobe16(x) bswap16((x))
#define htole16(x) ((uint16_t)(x))
#define be16toh(x) bswap16((x))
#define le16toh(x) ((uint16_t)(x))

#define htobe32(x) bswap32((x))
#define htole32(x) ((uint32_t)(x))
#define be32toh(x) bswap32((x))
#define le32toh(x) ((uint32_t)(x))

#define htobe64(x) bswap64((x))
#define htole64(x) ((uint64_t)(x))
#define be64toh(x) bswap64((x))
#define le64toh(x) ((uint64_t)(x))
#elif _BYTE_ORDER == _BIG_ENDIAN
#define htobe16(x) ((uint16_t)(x))
#define htole16(x) bswap16((x))
#define be16toh(x) ((uint16_t)(x))
#define le16toh(x) bswap16((x))

#define htobe32(x) ((uint32_t)(x))
#define htole32(x) bswap32((x))
#define be32toh(x) ((uint32_t)(x))
#define le64toh(x) bswap64((x))

#define htobe64(x) ((uint64_t)(x))
#define htole64(x) bswap64((x))
#define be64toh(x) ((uint64_t)(x))
#define le32toh(x) bswap32((x))
#endif
#endif
