/* Pure path classification, shared by fs.c and config.c (B-1160). It touches
 * no filesystem state, so focused test targets link it as it is. */
#include <ctype.h>
#include <PR/ultratypes.h>
#include "fs.h"

s32 fsPathIsAbsolute(const char *path)
{
	return (path[0] == '/' || (isalpha((unsigned char)path[0]) && path[1] == ':'));
}

s32 fsPathIsCwdRelative(const char *path)
{
	// ., .., ./, ../
	return (path[0] == '.' && (path[1] == '.' || path[1] == '/' || path[1] == '\\' || path[1] == '\0'));
}
