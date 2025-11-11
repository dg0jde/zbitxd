void setup();
void loop();
void display();
void redraw();
void key_pressed(char c);
int field_set(const char *label, const char *new_value);
int get_field_value(char *id, char *value);
int get_field_value_by_label(char *label, char *value);
extern int spectrum_plot[];
void remote_execute(char *command);
int remote_update_field(int i, char *text);
void web_get_spectrum(char *buff);
int web_get_console(char *buff, int max);
int remote_audio_output(int16_t *samples);
const char *field_str(char *label);
int field_int(char *label);
int is_in_tx();
void abort_tx();
void enter_qso();
extern int display_freq;

// A mixed bag of named styles used in various places in various UIs.
// TODO maybe separate the ones that mean something from those that are mere GTK-UI-specific styles
// e.g. MYCALL and GRID are what I'd prefer to call semantics, rather than styles
typedef enum {
    STYLE_FIELD_LABEL,
    STYLE_FIELD_VALUE,
    STYLE_LARGE_FIELD,
    STYLE_LARGE_VALUE,
    STYLE_SMALL,
    STYLE_LOG,
    STYLE_FT8_RX,
    STYLE_FT8_TX,
    STYLE_SMALL_FIELD_VALUE,
    STYLE_CW_RX,
    STYLE_CW_TX,
    STYLE_FLDIGI_RX,
    STYLE_FLDIGI_TX,
    STYLE_TELNET,
    STYLE_FT8_QUEUED,
    STYLE_FT8_REPLY,
    STYLE_MYCALL,
    STYLE_CALLER,
    STYLE_GRID,
    STYLE_BLACK
}  sbitx_style;

void enter_qso();
void call_wipe();
void write_console(int style, char *text);
int macro_load(char *filename, char *output);
int macro_exec(int key, char *dest);
void macro_label(int fn_key, char *label);
void macro_list(char *output);
void macro_get_keys(char *output);
void update_log_ed();
void write_call_log();

void tlog(char * id, char * text, int p);
//void tlogf(char * format, ...);

// 006 : Click fix by Beckman
// 007 : FT8 webui fix and coloring
// 008 : Callsign lookup, and grid visited coloring
// 009 : FT8 EXCH field safeguard
// 010 : FT8 callsign and grid decoration
// 011 : FT8 CQ click error fixed
// 012 : FT8 click non CQ fixed
// 013 : FT8 web_q overflow fixed
// 014 : Web GridMap v1, and cur selected Band/Store shown
// 015 : Web Robinson zoom GridMap, mouseover grid info, Logged Seen btn
// 016 : Cleanout and prepare for github sbitx-oz7bx v3.02.0016
#include "version.h"
