#include<stdio.h>
#include<stdlib.h>
#include<dirent.h>
#include<string.h>
#include<time.h>

double which_comes_first(struct tm* time1, long ms1, struct tm* time2, long ms2) {
	double dt = -1;

	if(ms1 < 0)
		ms1 = 0;
	if(ms2 < 0)
		ms2 = 0;

	if((dt = difftime(mktime(time1), mktime(time2))) < 0)
		return 1;
	else if(dt > 0)
		return 2;
	else
		return ms1 < ms2 ? 1 : 2;

}


int parse_log_name_to_date(char* filename, struct tm *time) {

	int return_value;

	char* peer = malloc(8*sizeof(char));

	long ms;

	if(sscanf(filename, "%d-%d-%d_%d:%d:%d.%ld_gbn_core_log_%s.log",
				&time->tm_year,
				&time->tm_mon,
				&time->tm_mday,
				&time->tm_hour,
				&time->tm_min,
				&time->tm_sec,
				&ms,
				peer) != 0) {
		return_value = 0;
		time->tm_isdst = 1;
		time->tm_year -= 1900;

		if(strcmp(peer, "client.log") == 0)
			return_value = 1;
		else if(strcmp(peer, "server.log") == 0)
			return_value = 2;
		else
			return_value = 0;

	} else return_value = -1;

	return return_value;
}

int parse_line(char* line, struct tm* time, long* ms) {

	int return_value;
	char* ln = malloc(150*sizeof(char));

	if(sscanf(line, "[%d-%d-%d_%d:%d:%d.%ld] %s\n",
								&time->tm_year,
								&time->tm_mon,
								&time->tm_mday,
								&time->tm_hour,
								&time->tm_min,
								&time->tm_sec,
								ms,
								ln) != 0) {
		return_value = 0;
		time->tm_year -= 1900;
	} else
		return_value = -1;

	return return_value;
}

int mergelogs(struct dirent *log_c_ent, struct dirent *log_s_ent) {

	int return_value, server_over = 0, client_over = 0, s_line_parsed, c_line_parsed, break_next;

	int n; /////DEBUG

	FILE* mergefile;
	char* mergefilename = malloc(80*sizeof(char));

	struct tm* tm_c = malloc(sizeof(struct tm));
	struct tm* tm_s = malloc(sizeof(struct tm));
	struct tm* tm_max = malloc(sizeof(struct tm));

	long ms_c = -1;
	long ms_s = -1;

	size_t maxln = 150;
	char* ln_c = malloc(maxln*sizeof(char));
	char* ln_s = malloc(maxln*sizeof(char));

	FILE* log_c = malloc(sizeof(FILE));
	FILE* log_s = malloc(sizeof(FILE));

	char* cpath = malloc(80*sizeof(char));
	char* spath = malloc(80*sizeof(char));

	parse_log_name_to_date(log_c_ent->d_name, tm_c);
	parse_log_name_to_date(log_s_ent->d_name, tm_s);


	sprintf(cpath, "log/%s", log_c_ent->d_name);
	printf("Client log file: \"%s\"\n", cpath);
	if((log_c = fopen(cpath, "r")) == NULL)
		printf("Fatal error\n");

	sprintf(spath, "log/%s", log_s_ent->d_name);
	printf("Server log file: \"%s\"\n", spath);
	if((log_s = fopen(spath, "r")) == NULL)
		perror("Fatal error\n");

	if(difftime(mktime(tm_c), mktime(tm_s)) > 0) {
		tm_max = tm_s;

		sprintf(mergefilename, "log/%d-%d-%d_%d:%d:%d_log_merge.log",
								tm_s->tm_year + 1900,
								tm_s->tm_mon,
								tm_s->tm_mday,
								tm_s->tm_hour,
								tm_s->tm_min,
								tm_s->tm_sec);

		mergefile = fopen(mergefilename, "w");

		if(mergefile != NULL)
			printf("Merge file created.\n");
		else
			printf("Error creating the merge file.\n");

		if((n = getline(&ln_c, &maxln, log_c)) < 0)
			printf("Client log empty!\n");
		else {
			parse_line(ln_c, tm_c, &ms_c);
			fclose(log_c);
			if((log_c = fopen(cpath, "r")) == NULL)
				printf("Fatal error\n");
		}

		fprintf(mergefile, "\t\t\t\t\t\t\t\tServer side\n");
		fprintf(mergefile, "Client side\n");


		fprintf(mergefile, "\t\t\t\t\t\t\t\t%d:%d:%d\n",
				tm_s->tm_hour,
				tm_s->tm_min,
				tm_s->tm_sec);

		fprintf(mergefile, "%d:%d:%d\n",
				tm_c->tm_hour,
				tm_c->tm_min,
				tm_c->tm_sec);

		fprintf(mergefile, "\n");


		if((n = getline(&ln_c, &maxln, log_c)) < 0)
			client_over = 1;
		if(parse_line(ln_c, tm_c, &ms_c) < 0)
			printf("Could not parse line:\nln_c:%s\n", ln_c);

		if((n = getline(&ln_s, &maxln, log_s)) < 0)
			server_over = 1;
		if(parse_line(ln_s, tm_s, &ms_s) < 0)
			printf("Could not parse line:\nln_c:%s\n", ln_s);
		
		do {
			while(which_comes_first(tm_c, ms_c, tm_s, ms_s) == 1 && client_over != 1) {
				fprintf(mergefile, "%s", ln_c);

				if((n = getline(&ln_c, &maxln, log_c)) < 0)
					client_over = 1;
				if(parse_line(ln_c, tm_c, &ms_c) < 0)
					sprintf(ln_c, "\n");//"Could not parse line:\nln_c:%s\n", ln_c);
			}

			fprintf(mergefile, "\n\n\n");

			while(which_comes_first(tm_c, ms_c, tm_s, ms_s) == 2 && server_over != 1) {
				fprintf(mergefile, "\t\t\t\t\t\t\t\t%s", ln_s);

				if((n = getline(&ln_s, &maxln, log_s)) < 0)
					server_over = 1;
				if(parse_line(ln_s, tm_s, &ms_s) < 0)
					sprintf(ln_s, "\n");//Could not parse line:\nln_s:%s\n", ln_s);
			}

			fprintf(mergefile, "\n\n\n");

		} while(server_over != 1 && client_over != 1);

		printf("closing files.\n");

		fclose(log_c);
		fclose(log_s);
		fclose(mergefile);

		printf("%s created.\n", mergefilename);
	} else
		return_value = -1;

	return return_value;
}

int main() {
	int return_value = 0;
	FILE* client_log, server_log;

	DIR* log_folder;
	struct dirent *ent;
	struct dirent *lastlog_c = NULL;
	struct dirent *lastlog_s = NULL;

	int i, c_i_max, s_i_max;
	struct tm s_tm_max, c_tm_max, tm_i;

	int peer_type;

	s_tm_max.tm_year = 0;
	c_tm_max.tm_year = 0;

	log_folder = opendir("log/");
	if (log_folder) {

		i=0;
		while((ent = readdir(log_folder)) != NULL) {
	    	if((peer_type = parse_log_name_to_date(ent->d_name, &tm_i)) > 0) {		//	If log file

	    		i++;

	    		if(peer_type == 1) {	//	If client log file

					if(c_tm_max.tm_year == 0) {
						lastlog_c = ent;
			    		c_tm_max = tm_i;
			    	} else if(which_comes_first(&tm_i, 0, &c_tm_max, 0) == 2) {
						lastlog_c = ent;
			    		c_tm_max = tm_i;
			    	}

	    		} else if(peer_type == 2) {				//	If server log file

	    			if(s_tm_max.tm_year == 0) {
						lastlog_s = ent;
			    		s_tm_max = tm_i;
			    	} else if(which_comes_first(&tm_i, 0, &s_tm_max, 0) == 2) {
						lastlog_s = ent;
			    		s_tm_max = tm_i;
			    	}

	    		} else
	    			printf("unexpected error\n");
	    	}
		}

		if(lastlog_s == NULL)
			printf("Error: no server logs are present in the log/ folder.\n");
		else if(lastlog_c == NULL)
			printf("Error: no client logs are present in the log/ folder.\n");
		else {
			mergelogs(lastlog_c, lastlog_s);
		}
	} else {
		printf("error opening log/ folder.\n");
		return_value = -1;
	}

	return return_value;
}
