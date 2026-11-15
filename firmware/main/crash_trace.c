/* Trace du dernier plantage : voir crash_trace.h.
 *
 * On intercepte esp_panic_handler (ESP-IDF, components/esp_system/panic.c) par l'editeur de liens
 * (-Wl,--wrap, dans CMakeLists.txt) : il est appele depuis un autre fichier (port/panic_handler.c),
 * donc l'appel passe par __wrap_esp_panic_handler, qui note la trace puis rend la main au vrai.
 * Code de panique : en IRAM, rien que des ecritures en memoire RTC, aucun appel qui pourrait
 * dependre du cache ou d'un verrou. Le parcours de pile reprend celui d'ESP-IDF
 * (panic_print_backtrace, esp_backtrace_print_from_frame). */
#include "crash_trace.h"
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_private/panic_internal.h"

#if CONFIG_IDF_TARGET_ARCH_XTENSA
#include "esp_debug_helpers.h"
#include "esp_cpu_utils.h"
#include "xtensa_context.h"
#endif

#define CRASH_MAGIC 0x50414E43u   /* « PANC » : la memoire RTC est aleatoire a la mise sous tension */

typedef struct {
    uint32_t magic;
    crash_trace_t t;
    uint32_t sum;   /* somme de controle : un enregistrement a moitie ecrit est ecarte */
} crash_rec_t;

static RTC_NOINIT_ATTR crash_rec_t s_rec;

static uint32_t IRAM_ATTR rec_sum(const crash_trace_t *t) {
    const uint32_t *w = (const uint32_t *)t;
    uint32_t s = 0x9E3779B9u;
    for (size_t i = 0; i < sizeof(*t) / sizeof(uint32_t); i++) s = (s ^ w[i]) * 16777619u;
    return s;
}

void __real_esp_panic_handler(panic_info_t *info);

void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info) {
#if CONFIG_IDF_TARGET_ARCH_XTENSA
    const XtExcFrame *f = (const XtExcFrame *)info->frame;
    s_rec.magic = 0;   /* invalide pendant l'ecriture */
    memset(&s_rec.t, 0, sizeof(s_rec.t));
    s_rec.t.pc = (uint32_t)info->addr;
    if (f) {
        s_rec.t.exccause = info->pseudo_excause ? 0 : f->exccause;
        esp_backtrace_frame_t fr = { .pc = f->pc, .sp = f->a1, .next_pc = f->a0, .exc_frame = f };
        uint32_t n = 0;
        s_rec.t.bt[n++] = esp_cpu_process_stack_pc(fr.pc);
        while (n < CRASH_TRACE_DEPTH && fr.next_pc != 0 && esp_backtrace_get_next_frame(&fr))
            s_rec.t.bt[n++] = esp_cpu_process_stack_pc(fr.pc);
        s_rec.t.depth = n;
    }
    s_rec.sum = rec_sum(&s_rec.t);
    s_rec.magic = CRASH_MAGIC;
#endif
    __real_esp_panic_handler(info);
}

bool crash_trace_last(crash_trace_t *out) {
    if (s_rec.magic != CRASH_MAGIC || s_rec.sum != rec_sum(&s_rec.t) || s_rec.t.depth > CRASH_TRACE_DEPTH) return false;
    if (out) *out = s_rec.t;
    return true;
}

void crash_trace_json(char *buf, size_t len) {
    crash_trace_t t;
    if (!len) return;
    if (!crash_trace_last(&t)) { snprintf(buf, len, "null"); return; }
    int n = snprintf(buf, len, "{\"pc\":\"0x%08lx\",\"cause\":%lu,\"bt\":\"", (unsigned long)t.pc, (unsigned long)t.exccause);
    for (uint32_t i = 0; i < t.depth && n > 0 && (size_t)n < len; i++)
        n += snprintf(buf + n, len - (size_t)n, "%s0x%08lx", i ? " " : "", (unsigned long)t.bt[i]);
    if (n > 0 && (size_t)n < len) snprintf(buf + n, len - (size_t)n, "\"}");
}
