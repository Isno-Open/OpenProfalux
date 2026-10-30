/* io_trace : adaptateur mince sur capture_journal (le journal commun io + raw-ook). Conserve l'API
 * historique appelee par radio.c / io_pair.cpp / web_ui.c ; tout ecrit desormais dans le journal
 * commun, pour que l'assistance lise io ET raw-ook au meme endroit. Voir capture_journal.h. */
#include "io_trace.h"
#include "capture_journal.h"

#define IO_RADIO 0   /* index de la radio io (868) dans la declaration de carte */

void io_trace_reset(void)            { capture_journal_reset(); }
void io_trace_persist(void)          { capture_journal_persist(); }
void io_trace_mark(const char *label){ capture_journal_mark(label); }

void io_trace_add(char dir, int8_t rssi, const uint8_t *bytes, int n) {
    capture_journal_add_io(dir, IO_RADIO, rssi, (n > 8 ? bytes[8] : -1), bytes, n);
}

int io_trace_count(void)             { return capture_journal_count(); }
int io_trace_dump(char *out, size_t cap) { return capture_journal_dump(out, cap); }
