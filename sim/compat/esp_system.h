/* Shim de bureau : sous-ensemble d'esp_system.h utilisé par src/ui.
 * Seul esp_restart() est appelé (écran « système » → redémarrage). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void esp_restart(void);
static inline uint32_t esp_get_free_heap_size(void) { return 210u * 1024u; }
#ifdef __cplusplus
}
#endif
