// Desktop stand-in for avr-libc's <avr/pgmspace.h>.
//
// On the AVR, constant tables live in flash and must be read with special
// instructions. On a desktop CPU they are ordinary const data, so the PROGMEM
// attribute disappears and the pgm_read_* accessors become plain reads.

#ifndef AMBIKA_PLUGIN_SHIM_AVR_PGMSPACE_H_
#define AMBIKA_PLUGIN_SHIM_AVR_PGMSPACE_H_

#include <stdint.h>
#include <string.h>

#define PROGMEM

// MSVC has no GCC attribute syntax; the firmware only uses always_inline.
#if defined(_MSC_VER) && !defined(__clang__) && !defined(__attribute__)
#define __attribute__(x)
#endif

typedef char prog_char;
typedef int8_t prog_int8_t;
typedef uint8_t prog_uint8_t;
typedef int16_t prog_int16_t;
typedef uint16_t prog_uint16_t;
typedef int32_t prog_int32_t;
typedef uint32_t prog_uint32_t;

#define pgm_read_byte(address) (*reinterpret_cast<const uint8_t*>(address))
// avrlib also uses pgm_read_word to read pointers out of pointer tables, so
// this must return the pointee type rather than a fixed 16-bit value.
#define pgm_read_word(address) (*(address))
#define memcpy_P memcpy
#define strncpy_P strncpy

// voicecard/voicecard.h takes the address of the last EEPROM byte. The value
// is never dereferenced in the plug-in build.
#ifndef E2END
#define E2END 0x3ff
#endif

#endif  // AMBIKA_PLUGIN_SHIM_AVR_PGMSPACE_H_
