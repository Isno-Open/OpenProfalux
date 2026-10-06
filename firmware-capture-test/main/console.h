#ifndef CONSOLE_H
#define CONSOLE_H

/* Self-test KeeLoq au boot (verifie la clef index 15 contre une trame DEVMEL reelle). */
void console_selftest(void);

/* Demarre le REPL serie du banc d'enrolement (commandes : newid/learn/settime/set/emit/
 * enroll/up/stop/down/status). Cree sa propre tache. */
void console_start(void);

/* Pilotage par bouton G39 (sans clavier) : */
void banc_capture_guided(void);   /* 1 appui : capture guidee UP/STOP/DOWN */
void banc_enroll(void);           /* 2 appuis : identite fraiche + enrolement R6 complet */
void banc_test_cmd(void);         /* 3 appuis : test commande native 0x067 */

#endif
