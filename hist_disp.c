#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "sdr_ui.h"
#include "logbook.h"
#include "hist_disp.h"
#include "configure.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

bool isLetter(char c) {
    return c >= 'A' && c <= 'Z';
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isValidGridId(char* gridId) {
	return strlen(gridId) == 4 && 
		isLetter(gridId[0]) && isLetter(gridId[1]) &&
        isDigit(gridId[2]) && isDigit(gridId[3]);
}

static FILE* onfFout;

void addGridToFile(char * gridId, int cnt) {
    if (isValidGridId(gridId)) {
		if (onfFout != NULL) {
				fwrite(gridId,1,4,onfFout);
        }
    }
}

void hd_createGridList() {
	 onfFout = fopen(STATEDIR "/grids.txt", "wb");

		logbook_open();
		logbook_get_grids(addGridToFile);

        fwrite("\0\0", 1, 2, onfFout);
        if (onfFout != NULL) fclose(onfFout);
}

struct hd_message_struct {
	char signal_info[32];
	char m1[32], m2[32], m3[32], m4[32];
};

int hd_next_token(const char* src, int start, char* tok, int tok_max, char * sep) {
	tok[0] = 0;
	if (src == NULL || src[start] == 0) 
		return -1;
	int len = strlen(src);
	if (start >= len)
		return -2;
	const char * p_sep;
	int n = 0, p = 0;
	if (len > 0 && src[len-1] == '\n') {
		len--; // strip trailing newline
	}
	do {
		p_sep = strstr(src + start, sep);
		if (p_sep == NULL) {
			p_sep = src+len;
		}
		n = p_sep - (src + start);
		p = start;
		start = start + n + strlen(sep);
	} while (n == 0 && start < len);
	if (n > tok_max) return -2;
	memcpy(tok, src + p, n);
	tok[n] = 0;
	return p + n + strlen(sep);
}

int hd_message_parse(struct hd_message_struct* p_message, const char* raw_message) {
	memset(p_message, 0, sizeof(struct hd_message_struct));
	int r = hd_next_token(raw_message, 0, p_message->signal_info, 32, "~ ");
	if (r < 0 ) return r;
	r = hd_next_token(raw_message, r, p_message->m1, 32, " ");
	if (r < 0) return r;
	r = hd_next_token(raw_message, r, p_message->m2, 32, " ");
	if (r < -1) return r;
	if (r < 0) return 0;
	r = hd_next_token(raw_message, r, p_message->m3, 32, " ");
	if (r < -1) return r;
	if (r < 0) return 0;
	r = hd_next_token(raw_message, r, p_message->m4, 32, " ");
	if (r < -1) return r;
	return 0;
}

int ff_lookup_style(char* id, int style, int style_default) {
	switch (style)
	{
	case STYLE_CALLER:
		return logbook_caller_exists(id) ? style_default : style;
		// return style; // test skipping log lookup
		break;

	case STYLE_GRID: {
		bool id_ok =
			(strlen(id) == 4 && strcmp(id,"RR73") &&
			isLetter(id[0]) && isLetter(id[1]) &&
        	isDigit(id[2]) && isDigit(id[3]));
			
			return (!id_ok || logbook_grid_exists(id)) ? style_default : style;
			//return (!id_ok) ? style_default : style; // test skipping log lookup
		}
		break;

	default:
		break;
	}
	return style;
}

char ff_char(int style) {
    /* used to be 'A' + style, where style came from these:
    #define FONT_FIELD_LABEL 0
    #define FONT_FIELD_VALUE 1
    #define FONT_LARGE_FIELD 2
    #define FONT_LARGE_VALUE 3
    #define FONT_SMALL 4
    #define FONT_LOG 5
    #define FONT_FT8_RX 6
    #define FONT_FT8_TX 7
    #define FONT_SMALL_FIELD_VALUE 8
    #define FONT_CW_RX 9
    #define FONT_CW_TX 10
    #define FONT_FLDIGI_RX 11
    #define FONT_FLDIGI_TX 12
    #define FONT_TELNET 13
    #define FONT_FT8_QUEUED 14
    #define FONT_FT8_REPLY 15
    #define FF_MYCALL 16
    #define FF_CALLER 17
    #define FF_GRID 18
    #define FONT_BLACK 19
    */
    switch (style) {
	// console styles
	case STYLE_LOG:
	    return 'A' + 5;
	case STYLE_MYCALL:
	    return 'A' + 16;
	case STYLE_CALLER:
	    return 'A' + 17;
	case STYLE_CALLEE:
	    return 'A' + 5;
	case STYLE_GRID:
	    return 'A' + 18;
	case STYLE_TIME:
	case STYLE_FREQ:
	case STYLE_FT8_RX:
	    return 'A' + 6;
	case STYLE_SNR:
	case STYLE_FT8_TX:
	    return 'A' + 7;
	case STYLE_FT8_QUEUED:
	    return 'A' + 14;
	case STYLE_FT8_REPLY:
	    return 'A' + 15;
	case STYLE_CW_RX:
	    return 'A' + 9;
	case STYLE_CW_TX:
	    return 'A' + 10;
	case STYLE_FLDIGI_RX:
	    return 'A' + 11;
	case STYLE_FLDIGI_TX:
	    return 'A' + 12;
	case STYLE_TELNET:
	    return 'A' + 13;

	// field styles
	case STYLE_FIELD_LABEL:
	    return 'A' + 0;
	case STYLE_FIELD_VALUE:
	    return 'A' + 1;
	case STYLE_LARGE_FIELD:
	    return 'A' + 2;
	case STYLE_LARGE_VALUE:
	    return 'A' + 3;
	case STYLE_SMALL:
	    return 'A' + 4;
	case STYLE_SMALL_FIELD_VALUE:
	    return 'A' + 8;
	case STYLE_BLACK:
	    return 'A' + 19;
	default:
	    printf("warning: unhandled style %d treated as \"log\"\n", style);
	    return 'A' + 5;
    }
}

char *ff_cs(char * markup, int style) {
    markup[0] = HD_MARKUP_CHAR;
    markup[1] = ff_char(style);
    markup[2] = 0;
    return markup;
}

// convert from semantic style to old numeric line style font (as used on zbitx i2c display)
int old_style_font(int style) {
    switch (style) {
    case STYLE_FT8_RX:
	return 6; // FONT_FT8_RX
    case STYLE_FT8_TX:
	return 7; // FONT_FT8_TX
    case STYLE_FT8_QUEUED:
	return 14; // FONT_FT8_QUEUED
    case STYLE_FT8_REPLY:
	return 15; // FONT_FT8_REPLY
    case STYLE_CW_RX:
	return 9; // FONT_CW_RX
    case STYLE_CW_TX:
	return 10; // FONT_CW_TX
    case STYLE_FLDIGI_RX:
	return 11; // FONT_FLDIGI_RX
    case STYLE_FLDIGI_TX:
	return 12; // FONT_FLDIGI_TX
    case STYLE_TELNET:
	return 13; // FONT_TELNET
    // The rest of the FONT_ defines are not meant for the console, so we don't need them
    default: // or STYLE_LOG
	return 5; // FONT_LOG
    }
}

char* ff_style(char* decorated, struct hd_message_struct *pms, int style_default, int style1, int style2, int style3, int style4) {
	char markup[3];
	*decorated = 0;
	
	strcat(decorated, ff_cs(markup, style_default));
	strcat(decorated, pms->signal_info);
	strcat(decorated, "~ ");

	strcat(decorated, ff_cs(markup, ff_lookup_style(pms->m1, style1, style_default)));
	strcat(decorated, pms->m1);
	strcat(decorated, " ");

	strcat(decorated, ff_cs(markup, ff_lookup_style(pms->m2, style2, style_default)));
	strcat(decorated, pms->m2);
	strcat(decorated, " ");

	strcat(decorated, ff_cs(markup, ff_lookup_style(pms->m3, style3, style_default)));
	strcat(decorated, pms->m3);

	if (style4) {
		strcat(decorated, " ");
		strcat(decorated, ff_cs(markup, ff_lookup_style(pms->m4, style4, style_default)));
		strcat(decorated, pms->m4);
	}
	strcat(decorated, "\n");

}

int hd_length_no_decoration( char * decorated) {
	int len = 0;
	while(*decorated) 
		if (*decorated++ == HD_MARKUP_CHAR) 
			len--;  
		else
			len++;
	return len < 0 ? 0 : len;
}


void hd_strip_decoration(char * ft8_message, char * decorated) {
	while(*decorated) {
		if (*decorated == HD_MARKUP_CHAR && *(decorated+1) != 0) {
			decorated += 2;
		} else if (*decorated == '<' || *decorated == '>') {
			decorated += 1;
		} else {
			*ft8_message++ = *decorated++;
		}
	}
	*ft8_message = 0;
}

// TODO rewrite like ftx_zbitx_decorate but keep the whole message,
// and use text_span_semantics instead of trying to re-parse the message.
int hd_decorate(int style, const char * message, char * decorated) {
	switch (style) {
	case STYLE_FT8_RX:
	case STYLE_FT8_TX:
	case STYLE_FT8_QUEUED:
	case STYLE_FT8_REPLY: 
		{
		decorated[0] = 0;
			struct hd_message_struct fms;
			const char* my_callsign = field_str("MYCALLSIGN");
			int res = hd_message_parse(&fms, message);
			if (res == 0) {
				if (!strcmp(fms.m1, "CQ")) { 
					if (fms.m4[0] == 0) { // CQ caller grid
						ff_style(decorated, &fms, style, STYLE_LOG, STYLE_CALLER, STYLE_GRID, 0);
					}
					else { // CQ DX caller grid
						ff_style(decorated, &fms, style, STYLE_LOG, STYLE_LOG, STYLE_CALLER, STYLE_GRID);
					}
				} else if (!strcmp(fms.m1, my_callsign)) 
				{ // mycall caller grid|report
					ff_style(decorated, &fms, style, STYLE_MYCALL, STYLE_CALLER, STYLE_GRID, 0);
				} else if (!strcmp(fms.m2, my_callsign)) 
				{ // caller mycall grid|report
					ff_style(decorated, &fms, style, STYLE_CALLER, STYLE_MYCALL, STYLE_GRID, 0);
				} else 
				{ // other caller grid|report
					ff_style(decorated, &fms, style, style, STYLE_CALLER, STYLE_GRID, 0);
				}
			}
			return res;
		}
		break;
	default:
		strcpy(decorated, message);
	}
	return 0;
}

/*!
    "Decorates" an FT8/FT4 \a message according to semantics \a sem,
    writing to the given \a out buffer up to \a maxlen characters.
    This variant (as opposed to hd_decorate) tries to make it compact for the
    zbitx front panel, by condensing timestamps and omitting the tilde separator (if any).
    Returns the number of bytes written to \a out.
    But alas, this doesn't work because ft8_update() in ft8.cpp for the front panel
    is trying to assume it knows what the fields are, by position, and skipping some.
    It ends up skipping everything and dispaying nothing.  Needs work...
*/
/*
int ftx_zbitx_decorate(const char *message, int len, const text_span_semantic *sems, int sem_count, char *out, int maxlen) {
    //~ printf("ftx_zbitx_decorate %s sems %d\n", message, sem_count);
    char *outp = out;
    char default_style = ff_char(STYLE_LOG);
    int written = 0;
    //~ if (sem_count > 0)
	//~ default_style = ff_char(sems[0].semantic);
    char last_style = default_style;
    *out = 0;
    for (int si = 1; si < sem_count; ++si) {
	const int start_col = sems[si].start_column;
	const int sem = sems[si].semantic;
	// what comes after time and before SNR is "candidate score": omit it to save space
	if (sem == STYLE_SNR)
	    written = start_col;
	else if (start_col > written) {
printf("output text (style %c vs last %c) from col %d before sem %d:%d	at col %d after '%s'\n", default_style, last_style, written, si, sem, start_col, out);
	    if (default_style != last_style && start_col - written > 1) {
		*outp++ = HD_MARKUP_CHAR; *outp++ = default_style;
		last_style = default_style;
	    }
	    if (*(message + written + 1) == '~') // in case of " ~ ", one space is enough
		*outp++ = ' ';
	    else
		outp = stpncpy(outp, message + written, MIN(maxlen - written, start_col - written));
	    *outp = 0;
	    written = start_col;
	}
printf("sems %d: %c; output text (vs last style %c) from col %d with len %d after '%s'\n", si, ff_char(sem), last_style, start_col, sems[si].length, out);
	if (ff_char(sem) != last_style) {
	    last_style = ff_char(sem);
	    *outp++ = HD_MARKUP_CHAR; *outp++ = last_style;
	}
	// special case: shorten the time representation (omit hours and tenths-of-seconds, so only 4 digits and a space)
	if (sem == STYLE_TIME) {
	    outp = stpncpy(outp, message + written + 2, MIN(maxlen - written, 4));
	    *outp++ = ' ';
	} else {
	    outp = stpncpy(outp, message + written, MIN(maxlen - written, sems[si].length));
	}
	*outp = 0;
	written += sems[si].length;
    }
    if (len < written) {
	if (default_style != last_style) {
	    last_style = default_style;
	    *outp++ = HD_MARKUP_CHAR; *outp++ = last_style;
	}
	outp = stpncpy(outp, message + written, maxlen - written);
	*outp = 0;
    }
    *outp++ = '\n';
    *outp = 0;
    return outp - out;
}
*/
