
#ifndef __TCPI_DEPS_H
#define __TCPI_DEPS_H

#include <stdint.h>
#include <stddef.h> /* needed for offsetof macro below */
#include "mx_tcpm.h" /* needed for i2c callbacks */

int i2c_write_byte(const uint8_t reg_addr, const uint8_t reg_val);
int reg_read_byte(const uint8_t reg_addr);

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct regmap {
	uint8_t device_addr;
	uint8_t max_port_power;
};

// from err.h
static inline long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}
static inline void *ERR_PTR(long error)
{
	return (void *) error;
}

static inline int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
  mx_reg_read_bytes(map->device_addr, reg, val, 1);
  return 0;
}
static inline int regmap_raw_read(struct regmap *map, unsigned int reg, void *val, size_t val_len)
{
  mx_reg_read_bytes(map->device_addr, reg, (uint8_t*)(val), val_len);
  return 0;
}
static inline int regmap_write(struct regmap *map, unsigned int reg, unsigned int val)
{
  return mx_reg_write_bytes(map->device_addr, reg, &val, 1);
}
static inline int regmap_raw_write(struct regmap *map, unsigned int reg, const void *val, size_t val_len)
{
  mx_reg_write_bytes(map->device_addr, reg, (uint8_t*)val, val_len);
  return 0;
}

#endif
