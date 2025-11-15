#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h> 
#include <math.h>
#include <complex.h>
#include <fftw3.h>
#include <unistd.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include <linux/types.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <ctype.h>
#include <arpa/inet.h>
#include "sdr.h"
#include "sdr_ui.h"
#include "logbook.h"
#include "configure.h"

#include <sqlite3.h>

static int rc;
static sqlite3 *db=NULL;

void logbook_open();
int logbook_fill(int from_id, int count, char *query);

/* writes the output to /tmp/zbitx_result_rows.txt
	if the from_id is negative, it returns the later 50 records (higher id)
	if the from_id is positive, it returns the prior 50 records (lower id) */

int logbook_query(char *query, int from_id, char *result_file){
	sqlite3_stmt *stmt;
	char statement[200], param[2000];

	if (db == NULL)
		logbook_open();

	//add to the bottom of the logbook
	if (from_id > 0){
		if (query)
			sprintf(statement, "select * from logbook "
				"where (callsign_recv LIKE '%s%%' AND id < %d) ",
				query, from_id);
		else
			sprintf(statement, "select * from logbook where id < %d ", from_id);
	}
	//last 50 QSOs
	else if (from_id == 0){
		if (query)
			sprintf(statement, "select * from logbook "
				"where callsign_recv LIKE '%s%%' ", query);
		else
			strcpy(statement, "select * from logbook ");
	}
	//latest QSOs after from_id (top of the log)
	else {
		if (query)
			sprintf(statement, "select * from logbook "
				"where (callsign_recv LIKE '%s%%' AND id > %d) ",
				query, -from_id);
		else 
			sprintf(statement, "select * from logbook where id > %d ", -from_id); 
	}
	strcat(statement, "ORDER BY id DESC LIMIT 50;");

	//printf("[%s]\n", statement);
	sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);

	const char *output_path = "/tmp/zbitx_result_rows.txt";
	strcpy(result_file, output_path);
	
	FILE *pf = fopen(output_path, "w");
	if (!pf)
		return -1;

	int rec = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		int i;
		int num_cols = sqlite3_column_count(stmt);
		for (i = 0; i < num_cols; i++){
			switch (sqlite3_column_type(stmt, i))
			{
			case (SQLITE3_TEXT):
				strcpy(param, sqlite3_column_text(stmt, i));
				break;
			case (SQLITE_INTEGER):
				sprintf(param, "%d", sqlite3_column_int(stmt, i));
				break;
			case (SQLITE_FLOAT):
				sprintf(param, "%g", sqlite3_column_double(stmt, i));
				break;
			case (SQLITE_NULL):
				break;
			default:
				sprintf(param, "%d", sqlite3_column_type(stmt, i));
				break;
			}
			//printf("%s|", param);
			fprintf(pf, "%s|", param);
		}
		//printf("\n");
		fprintf(pf, "\n");
	}
	sqlite3_finalize(stmt);
	fclose(pf);
	return rec;
}

int logbook_count_dup(const char *callsign, int last_seconds){
	char date_str[100], time_str[100], statement[1000];
	sqlite3_stmt *stmt;

	time_t log_time = time(NULL) - last_seconds;
	struct tm *tmp = gmtime(&log_time);
	sprintf(date_str, "%04d-%02d-%02d", tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday);
	sprintf(time_str, "%02d%02d", tmp->tm_hour, tmp->tm_min);
	
	sprintf(statement, "select * from logbook where "
		"callsign_recv=\"%s\" AND qso_date >= \"%s\" AND qso_time >= \"%s\"",
		callsign, date_str, time_str);

	sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	int rec = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		rec++;
	}
	sqlite3_finalize(stmt);
	return rec;
}

int logbook_get_grids(void (*f)(char *,int)) {
	sqlite3_stmt *stmt;

	char *statement = "SELECT exch_recv, COUNT(*) AS n FROM logbook "
		"GROUP BY exch_recv order by exch_recv";

	int res = sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	//printf("%s : %d\n", statement, res);
	int cnt = 0;
	char grid[10];
	int n = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		int num_cols = sqlite3_column_count(stmt);
		for (int i = 0; i < num_cols; i++){
			char const *col_name = sqlite3_column_name(stmt, i);
			if (!strcmp(col_name, "exch_recv")) { 
				strcpy(grid, sqlite3_column_text(stmt, i));
			} else
			if (!strcmp(col_name, "n")) { 
				n = sqlite3_column_int(stmt, i);
			}
		}
		f(grid,n);
		cnt++;
	}
	sqlite3_finalize(stmt);
	return cnt;
}

bool logbook_caller_exists(char * id) {
	sqlite3_stmt *stmt;
	char * statement = "SELECT EXISTS(SELECT 1 FROM logbook WHERE callsign_recv=?)";
	int res = sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	if (res != SQLITE_OK) return false;
	bool exists = false;
	res = sqlite3_bind_text(stmt, 1, id, strlen(id), SQLITE_STATIC);
	if (res == SQLITE_OK) {
		res = sqlite3_step(stmt);
		int i = sqlite3_column_int(stmt, 0);
		exists = ( res == SQLITE_ROW && i != 0);
	}
	sqlite3_finalize(stmt);
	return exists;
}

bool logbook_grid_exists(char *id) {
	sqlite3_stmt *stmt;
	char * statement = "SELECT EXISTS(SELECT 1 FROM logbook WHERE exch_recv=?)";
	int res = sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	if (res != SQLITE_OK) return false;
	bool exists = false;
	res = sqlite3_bind_text(stmt, 1, id, strlen(id), SQLITE_STATIC);
	if (res == SQLITE_OK) {
		res = sqlite3_step(stmt);
		int i = sqlite3_column_int(stmt, 0);
		exists = ( res == SQLITE_ROW && i != 0);
	}
	sqlite3_finalize(stmt);
	return exists;
}

int logbook_prev_log(const char *callsign, char *result){
	char statement[1000], param[2000];
	sqlite3_stmt *stmt;

	sprintf(statement, "select * from logbook where "
		"callsign_recv=\"%s\" ORDER BY id DESC",
		callsign);
	strcpy(result, callsign);
	strcat(result, ": ");
	int res = sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	//printf("%s : %d\n", statement, res);
	int rec = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		int i;
		int num_cols = sqlite3_column_count(stmt);
		if (rec == 0) {

			for (i = 0; i < num_cols; i++){
				char const *col_name = sqlite3_column_name(stmt, i);
			    if (!strcmp(col_name, "id")) { continue; }
				if (!strcmp(col_name, "callsign_recv")) { continue; }
				switch (sqlite3_column_type(stmt, i))
				{
				case (SQLITE3_TEXT):
					strcpy(param, sqlite3_column_text(stmt, i));
					break;
				case (SQLITE_INTEGER):
					sprintf(param, "%d", sqlite3_column_int(stmt, i));
					break;
				case (SQLITE_FLOAT):
					sprintf(param, "%g", sqlite3_column_double(stmt, i));
					break;
				case (SQLITE_NULL):
					break;
				default:
					sprintf(param, "%d", sqlite3_column_type(stmt, i));
					break;
				}
				//printf("%s : %s\n", col_name, param);
				strcat(result, param);
				if (!strcmp(col_name, "qso_date")) strcat(result, "_");
				else strcat(result, " ");
			}
		}
		rec++;
	}
	sqlite3_finalize(stmt);
	sprintf(param, ": %d", rec);
	strcat(result, param);
	/*if (rec > 1) {
		sprintf(param, "\nand %d more.", rec-1);
		strcat(result, param);
	} else
	if (rec == 0) {
		sprintf(result, "%s not logged.", callsign);
	}*/
	return rec;
}

void logbook_open(){
	const char *db_path = STATEDIR "/sbitx.db";

	rc = sqlite3_open(db_path, &db);
}

void message_add(char *mode, unsigned int frequency, int outgoing, char *message){
	char date_str[10], time_str[10], freq_str[12], statement[1000], *err_msg;
	static int err_output = 1;

	/* get the frequency */
	get_field_value("r1:freq", freq_str);
	frequency = frequency + atoi(freq_str);

	/* get the time */
	time_t log_time = time(NULL);
	struct tm *tmp = gmtime(&log_time);

	int date_utc = ((tmp->tm_year + 1900)*10000) 
		+ ((tmp->tm_mon+1) * 100) + (tmp->tm_mday);
	int time_utc = (tmp->tm_hour * 10000) + (tmp->tm_min * 100) + tmp->tm_sec;

	sprintf(statement,
		"INSERT INTO messages (mode, freq, qso_date, qso_time, is_outgoing, data)"
		" VALUES('%s', '%d', '%d', '%d',  '%d','%s');",
			mode, frequency, date_utc, time_utc, outgoing, message);

	if (db == NULL)
		logbook_open();

	int res = sqlite3_exec(db, statement, 0,0, &err_msg);
	if (res != 0 && err_output) {
		printf("message_add: db err %d %s\n", res, err_msg);
		if (err_msg) sqlite3_free(err_msg);
		// only complain once, if the error is "no such table"
		// (it's quite alright to delete this table to avoid constant writing to the SSD)
		if (res == 1)
			err_output = 0;
	}
}

void logbook_add(char *contact_callsign, char *rst_sent, char *exchange_sent, 
	char *rst_recv, char *exchange_recv){
	char statement[1000], *err_msg, date_str[11], time_str[5];
	char freq[12], log_freq[12], mode[10], mycallsign[12];

	time_t log_time = time(NULL);
	struct tm *tmp = gmtime(&log_time);
	get_field_value("r1:freq", freq);
	get_field_value("r1:mode", mode);
	get_field_value("#mycallsign", mycallsign);

	sprintf(log_freq, "%d", atoi(freq)/1000);
	
	sprintf(date_str, "%04d-%02d-%02d", tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday);
	sprintf(time_str, "%02d%02d", tmp->tm_hour, tmp->tm_min);

	sprintf(statement,
		"INSERT INTO logbook (freq, mode, qso_date, qso_time, callsign_sent,"
		"rst_sent, exch_sent, callsign_recv, rst_recv, exch_recv) "
		"VALUES('%s', '%s', '%s', '%s',  '%s','%s','%s',  '%s','%s','%s');",
			log_freq, mode, date_str, time_str, mycallsign,
			 rst_sent, exchange_sent, contact_callsign, rst_recv, exchange_recv);

	if (db == NULL)
		logbook_open();

	int res = sqlite3_exec(db, statement, 0,0, &err_msg);
	if (res != 0) {
		printf("logbook_add db: %d err=%s", res, err_msg);
		if (err_msg) sqlite3_free(err_msg);
	}
}

// ADIF field headers, see note above
const static char *adif_names[]={"ID","MODE","FREQ","QSO_DATE","TIME_ON","OPERATOR","RST_SENT","STX_String","CALL","RST_RCVD","SRX_String","STX","COMMENTS"};

struct band_name {
	char *name;
	int from, to;
} bands[] = {
	{"160M", 1800, 2000},
	{"80M", 3500, 4000},
	{"60M", 5000, 5500},
	{"40M", 7000, 7300},
	{"30M", 10000, 10150},
	{"20M", 14000, 14350},
	{"17M", 18000, 18200},
	{"15M", 21000, 21450},
	{"12M", 24800, 25000},
	{"10M", 28000, 29700},
};

static void strip_chr(char *str, const char to_remove){
    int i, j, len;

    len = strlen(str);
    for(i=0; i<len; i++) {
        if(str[i] == to_remove) {
            for(j=i; j<len; j++)
                str[j] = str[j+1];
            len--;
            i--;
        }
    }
}

int export_adif(char *path, char *start_date, char *end_date){
	sqlite3_stmt *stmt;
	char statement[200], param[2000], qso_band[20];
	

	//add to the bottom of the logbook
	sprintf(statement, "select * from logbook where (qso_date >= '%s' AND  qso_date <= '%s')  ORDER BY id DESC;",
		start_date, end_date);

	FILE *pf = fopen(path, "w");
	sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	fprintf(pf, "/ADIF file\n");
	fprintf(pf, "generated from sBITX log db by Log2ADIF program\n");	
	fprintf(pf, "<adif version:5>3.1.4\n");	
	fprintf(pf, "<EOH>\n");	

	int rec = 0;

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		int i;
		int num_cols = sqlite3_column_count(stmt);
		for (i = 0; i < num_cols; i++){
			switch (sqlite3_column_type(stmt, i))
			{
			case (SQLITE3_TEXT):
				strcpy(param, sqlite3_column_text(stmt, i));
				break;
			case (SQLITE_INTEGER):
				sprintf(param, "%d", sqlite3_column_int(stmt, i));
				break;
			case (SQLITE_FLOAT):
				sprintf(param, "%g", sqlite3_column_double(stmt, i));
				break;
			case (SQLITE_NULL):
				break;
			default:
				sprintf(param, "%d", sqlite3_column_type(stmt, i));
				break;
			}
			if (i == 2){
				long f = atoi(param);
				float ffreq=atof(param)/1000.0;  // convert kHz to MHz
				sprintf(param, "%.3f",ffreq); // write out with 3 decimal digits
				for (int j = 0 ; j < sizeof(bands)/sizeof(struct band_name); j++)
					if (bands[j].from <= f && f <= bands[j].to){
						fprintf(pf, "<BAND:%d>%s", strlen(bands[j].name), bands[j].name); 
					}
			}
			else if (i == 3) //it is the date
				strip_chr(param, '-');
	   	fprintf(pf, "<%s:%d>%s", adif_names[i], strlen(param), param);
		}
		fprintf(pf, "<EOR>\n");
		//printf("\n");
	}
	sqlite3_finalize(stmt);
	fclose(pf);
}

int logbook_fill(int from_id, int count, char *query){
	sqlite3_stmt *stmt;
	char statement[200], param[2000];

	if (db == NULL)
		logbook_open();

	//add to the bottom of the logbook
	if (from_id > 0){
		if (query)
			sprintf(statement, "select * from logbook "
				"where (callsign_recv LIKE '%s%%' AND id < %d) ",
				query, from_id);
		else
			sprintf(statement, "select * from logbook where id < %d ", from_id);
	}
	//last 200 QSOs
	else if (from_id == 0){
		if (query)
			sprintf(statement, "select * from logbook "
				"where callsign_recv LIKE '%s%%' ", query);
		else
			strcpy(statement, "select * from logbook ");
	}
	//latest QSOs after from_id (top of the log)
	else {
		if (query)
			sprintf(statement, "select * from logbook "
				"where (callsign_recv LIKE '%s%%' AND id > %d) ",
				query, -from_id);
		else 
			sprintf(statement, "select * from logbook where id > %d ", -from_id); 
	}
	char stmt_count[100];
	sprintf(stmt_count, "ORDER BY id DESC LIMIT %d;", count);
	strcat(statement, stmt_count);
	//printf("[%s]\n", statement);
	sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);

	int rec = 0;

	char id[10], qso_time[20], qso_date[20], freq[20], mode[20], callsign[20],
	rst_recv[20], exchange_recv[20], rst_sent[20], exchange_sent[20], comments[1000];

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		int i;
		int num_cols = sqlite3_column_count(stmt);
		for (i = 0; i < num_cols; i++){

			char const *col_name = sqlite3_column_name(stmt, i);
			if (!strcmp(col_name, "id"))
				strcpy(id, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "qso_date"))
				strcpy(qso_date, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "qso_time"))
				strcpy(qso_time, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "qso_time"))
				strcpy(qso_time, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "freq"))
				strcpy(freq, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "mode"))
				strcpy(mode, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "callsign_recv"))
				strcpy(callsign, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "rst_sent"))
				strcpy(rst_sent, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "rst_recv"))
				strcpy(rst_recv, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "exch_sent"))
				strcpy(exchange_sent, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "exch_recv"))
				strcpy(exchange_recv, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "comments"))
				strcpy(comments, sqlite3_column_text(stmt, i));
		}
	}
	sqlite3_finalize(stmt);
}

void logbook_delete(int id){
	char statement[100], *err_msg;
	sprintf(statement, "DELETE FROM logbook WHERE id='%d';", id);
	sqlite3_exec(db, statement, 0,0, &err_msg);
}
