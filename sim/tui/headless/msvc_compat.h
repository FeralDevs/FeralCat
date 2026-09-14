/* Vendored GCC-only annotations occur in unused touch and image codec headers.
 * Native sprite primitives and fonts already have their own MSVC support.
 * This renderer does not decode PNG/QOI images or use touch hardware. */
#pragma once
#ifdef _MSC_VER
#define __attribute__(x)
#endif
