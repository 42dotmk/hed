#ifndef HED_PATH_LIMITS_H
#define HED_PATH_LIMITS_H

/* Portable PATH_MAX shim.
 *
 * - Linux (glibc, with _GNU_SOURCE): <limits.h> exposes PATH_MAX (4096)
 *   by indirectly pulling <linux/limits.h>.
 * - macOS / BSD: <limits.h> pulls <sys/syslimits.h>, which defines
 *   PATH_MAX (1024).
 * - Windows / anywhere else PATH_MAX is missing: fall back to 4096
 *   (matching the Linux value), which is plenty for our use cases. */

#include <limits.h>

#ifndef PATH_MAX
#  ifdef MAX_PATH
#    define PATH_MAX MAX_PATH
#  else
#    define PATH_MAX 4096
#  endif
#endif

#endif /* HED_PATH_LIMITS_H */
