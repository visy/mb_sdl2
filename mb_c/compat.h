#ifndef COMPAT_H
#define COMPAT_H

#ifdef _WIN32
#define STRICMP _stricmp
#define STRNICMP _strnicmp
#define PATH_SEP '\\'
#else
#include <strings.h>
#define STRICMP strcasecmp
#define STRNICMP strncasecmp
#define PATH_SEP '/'
#endif

#endif
