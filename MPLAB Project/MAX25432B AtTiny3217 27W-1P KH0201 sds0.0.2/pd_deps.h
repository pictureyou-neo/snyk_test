
#ifndef __PD_HACKS
#define __PD_HACKS

#include <stdint.h> /*mcm: needed for uintx_t types */
#include <stdbool.h>

#ifndef __packed
#define __packed __attribute__((__packed__)) /* mcm: added to deal with attribute __packed */
#endif

typedef uint8_t __le8;
typedef uint16_t __le16;
typedef uint32_t __le32;

// mcm: These do nothing if compiled little-endian 
inline uint16_t le16_to_cpu(uint16_t data)
{
	return data;
}

inline uint16_t cpu_to_le16(uint16_t data)
{
	return data;
}

inline uint32_t le32_to_cpu(uint32_t data)
{
	return data;
}

inline uint32_t cpu_to_le32(uint32_t data)
{
	return data;
}

#endif
