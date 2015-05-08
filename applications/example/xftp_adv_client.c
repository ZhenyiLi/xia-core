/* ts=4 */
/*
** Copyright 2011 Carnegie Mellon University
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**    http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "Xsocket.h"
#include "dagaddr.hpp"
#include <assert.h>

#include <sys/time.h>

#include "prefetch_utils.h"

#define VERSION "v1.0"
#define TITLE "XIA Advanced FTP client"

// global configuration options
bool quick = false;

char src_ad[MAX_XID_SIZE];
char src_hid[MAX_XID_SIZE];

char dst_ad[MAX_XID_SIZE];
char dst_hid[MAX_XID_SIZE];

char *ftp_name = "www_s.ftp.advanced.aaa.xia";
char *prefetch_client_name = "www_s.client.prefetch.aaa.xia";
char *prefetch_profile_name = "www_s.profile.prefetch.aaa.xia";
char *prefetch_pred_name = "www_s.prediction.prefetch.aaa.xia";
char *prefetch_exec_name = "www_s.executer.prefetch.aaa.xia";

//char *prefetch_ctx_name = "www_s.prefetch_context_listener.aaa.xia";

int ftp_sock = -1;
int	prefetch_ctx_sock = -1;

int getFile(int sock, char *p_ad, char *p_hid, const char *fin, const char *fout) {
	int chunkSock;
	int offset;
	char cmd[512];
	char reply[512];
	int status = 0;
	char prefetch_cmd[512];

	// send the file request to the xftp server
	sprintf(cmd, "get %s", fin);
	sendCmd(sock, cmd);

	// get back number of chunks in the file
	if (getChunkCount(sock, reply, sizeof(reply)) < 1){
		warn("could not get chunk count. Aborting. \n");
		return -1;
	}

	// reply: OK: ***
	int count = atoi(&reply[4]);

	say("%d chunks in total\n", count);

	if ((chunkSock = Xsocket(AF_XIA, XSOCK_CHUNK, 0)) < 0)
		die(-1, "unable to create chunk socket\n");

	FILE *f = fopen(fout, "w");

	offset = 0;

	struct timeval tv;
	int start_msec, temp_start_msec, temp_end_msec;
	
	if (gettimeofday(&tv, NULL) == 0)
		start_msec = ((tv.tv_sec % 86400) * 1000 + tv.tv_usec / 1000);
			
	while (offset < count) {
		int num = NUM_CHUNKS;
		if (count - offset < num)
			num = count - offset;

		// tell the server we want a list of <num> cids starting at location <offset>
		printf("\nFetched chunks: %d/%d:%.1f%\n\n", offset, count, 100*(double)(offset)/count);

		sprintf(cmd, "block %d:%d", offset, num);
		
		if (gettimeofday(&tv, NULL) == 0)
			temp_start_msec = ((tv.tv_sec % 86400) * 1000 + tv.tv_usec / 1000);
					
		// send the requested CID range
		sendCmd(sock, cmd);

		// TODO: send file name and current chunks 
//		sprintf(prefetch_cmd, "get %s %d %d", fin, offset, offset + NUM_CHUNKS - 1);
//		int m = sendCmd(prefetch_ctx_sock, prefetch_cmd); 
/*
		char* hello = "Hello from context client";
		int m = sendCmd(prefetch_ctx_sock, hello); 
		printf("%d\n");
		say("Sent CID update\n");
*/
		if (getChunkCount(sock, reply, sizeof(reply)) < 1) {
			warn("could not get chunk count. Aborting. \n");
			return -1;
		}
		offset += NUM_CHUNKS;
		// &reply[4] are the requested CIDs  
		if (getListedChunks(chunkSock, f, &reply[4], p_ad, p_hid) < 0) {
			status= -1;
			break;
		}
		if (gettimeofday(&tv, NULL) == 0)
			temp_end_msec = ((tv.tv_sec % 86400) * 1000 + tv.tv_usec / 1000);
					
		printf("Time elapses: %.3f seconds\n", (double)(temp_end_msec - temp_start_msec)/1000);
	}
	printf("Time elapses: %.3f seconds in total.\n", (double)(temp_end_msec - start_msec)/1000);	
	fclose(f);

	if (status < 0) {
		unlink(fin);
	}

	say("Received file %s\n", fout);
	sendCmd(sock, "done");
	Xclose(chunkSock);
	return status;
}

int main(int argc, char **argv) {	
	const char *name;
	int sock = -1;
	char fin[512], fout[512];
	char cmd[512], reply[512];
	int params = -1;

	//prefetch_ctx_sock = initializeClient(prefetch_ctx_name, src_ad, src_hid, dst_ad, dst_hid);
/*
	// say hello to the connext server
	char* hi = "Hello from context client";		
	sendCmd(prefetch_ctx_sock, hi);
	say("Say hi to prefetch client.\n");
*/
	say ("\n%s (%s): started\n", TITLE, VERSION);
	
	if (argc == 1) {
		say ("No service name passed, using default: %s\nYou can also pass --quick to execute a couple of default commands for quick testing. Requires s.txt to exist. \n", ftp_name);
		sock = initializeClient(ftp_name, src_ad, src_hid, dst_ad, dst_hid);
		usage();
	} 
	else if (argc == 2) {
		if (strcmp(argv[1], "--quick") == 0) {
			quick = true;
			name = ftp_name;
		} 
		else {
			name = argv[1];
			usage();
		}
		say ("Connecting to: %s\n", name);
		sock = initializeClient(name, src_ad, src_hid, dst_ad, dst_hid);
	} 
	else if (argc == 3) {
		if (strcmp(argv[1], "--quick") == 0) {
			quick = true;
			name = argv[2];
			say ("Connecting to: %s\n", name, src_ad, src_hid);
			sock = initializeClient(name, src_ad, src_hid, dst_ad, dst_hid);
			usage();
		} 
		else {
			die(-1, "xftp [--quick] [SID]");
		}
	} 
	else {
		die(-1, "xftp [--quick] [SID]"); 
	}
	
	int i = 0;

	// This is for quick testing with a couple of commands
	while (i < NUM_PROMPTS) {
		say(">>");
		cmd[0] = '\n';
		fin[0] = '\n';
		fout[0] = '\n';
		params = -1;
		
		if (quick) {
			if (i == 0)
				strcpy(cmd, "put s.txt r.txt");
			else if (i == 1)
				strcpy(cmd, "get r.txt sr.txt\n");
			i++;
		} 
		else {
			fgets(cmd, 511, stdin);
		}
		// compare the first three characters
		if (strncmp(cmd, "get", 3) == 0) {
			// params is the number of arguments
			params = sscanf(cmd, "get %s %s", fin, fout);
			if (params != 2) {
				sprintf(reply, "FAIL: invalid command (%s)\n", cmd);
				warn(reply);
				usage();
				continue;
			}
			if (strcmp(fin, fout) == 0) {
				warn("Since both applications write to the same folder (local case) the names should be different.\n");
				continue;
			}
			getFile(sock, dst_ad, dst_hid, fin, fout);
		}
		else {
			sprintf(reply, "FAIL: invalid command (%s)\n", cmd);
			warn(reply);
			usage();
		}
	}
	
	return 1;
}
