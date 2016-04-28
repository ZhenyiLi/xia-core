#include "stage_utils.h"
#include <time.h>


#define VERSION "v1.0"
#define TITLE "XIA Advanced FTP client"

using namespace std;

bool stage = true;

char fin[256], fout[256];

int ftpSock, stageManagerSock;

XcacheHandle h;
int getFile(int sock)
{
	FILE *fd = fopen(fout, "w");
	int n;
	int status = -1;
	char send_to_server_[XIA_MAX_BUF];
	memset(send_to_server_, '\0', strlen(send_to_server_));
	sprintf(send_to_server_, "finish receiveing the current CIDs.");

	int chunkSock;
	if ((chunkSock = Xsocket(AF_XIA, XSOCK_CHUNK, 0)) < 0) {
		die(-1, "unable to create chunk socket\n");
	}

	char cmd[XIA_MAX_BUF];
	char reply[XIA_MAX_BUF];

	vector<string> CIDs;

	// send the file request to the xftp server
	sprintf(cmd, "get %s", fin);
	sendStreamCmd(sock, cmd);

	// receive the CID list from xftp server
	while (1) {
		say("In getFile, receive CID list from xftp server.\n");
		memset(reply, '\0', XIA_MAX_BUF);
		if ((n = Xrecv(sock, reply, sizeof(reply), 0)) < 0) {
			Xclose(sock);
			die(-1, "Unable to communicate with the server\n");
		}
		if (strncmp(reply, "cont", 4) == 0) {
			say("Receiving partial chunk list\n");
			char reply_arr[strlen(reply)];
			//char reply_arr[strlen(reply+5)];
			strcpy(reply_arr, reply + 5);
			char *cid;
			CIDs.push_back(strtok(reply_arr, " "));
			// register CID
			while ((cid = strtok(NULL, " ")) != NULL) {
				CIDs.push_back(cid);
			}
		}

		else if (strncmp(reply, "done", 4) == 0) {
			say("Finish receiving all the CIDs\n");
			break;
		}
		sayHello(sock, send_to_server_);
	}

	// update CID list to the local staging service if exists.
	// Send CID list to stage manager
	if (stage) {
		if ((n = updateManifest(stageManagerSock, CIDs) < 0)) {
			close(stageManagerSock);
			die(-1, "Unable to communicate with the local prefetching service\n");
		}
	}

	XcacheHandleInit(&h);

	vector<unsigned int> chunkSize; // in bytes
	vector<long> latency;
	vector<long> chunkStartTime;
	vector<long> chunkFinishTime;

	long fetchTime = now_msec();
	long startTime = now_msec();
	unsigned int bytes = 0;

	// chunk fetching begins
	char data[1024 * 1024];
	for (unsigned int i = 0; i < CIDs.size(); i++) {
		clock_t start_time = clock();
say("The number of chunks is %d.\n", CIDs.size());
say("Fetching chunk %u / %lu\n", i, CIDs.size() - 1);
		int len;
		sockaddr_x addr;
		if(stage){
			cmd[0] = 0;
			sprintf(cmd, "fetch");
			sprintf(cmd, "%s %s", cmd, CIDs[i].c_str());
			if(send(stageManagerSock, cmd, strlen(cmd), 0) < 0){
die(-1, "send cmd fail! cmd is %s", cmd);
			}

			if((len = recv(stageManagerSock, cmd, XIA_MAX_BUF, 0)) < 0){
die(-1, "fail to recv from stageManager!");
			}	
			url_to_dag(&addr, cmd, len);
		}
		else{
			url_to_dag(&addr,(char*)CIDs[i].c_str(), CIDs[i].size());
		}
		if((len = XfetchChunk(&h, data, 1024 * 1024, XCF_BLOCK, &addr, sizeof(addr))) < 0) {
die(-1, "XcacheGetChunk Failed\n");
		}
		clock_t end_time = clock();
		cerr << "Running time is: "<< static_cast<double>(end_time - start_time) / CLOCKS_PER_SEC * 1000 << "ms" << endl;
say("writing %d bytes of chunk %s to disk\n", len, string2char(CIDs[i]));
		fwrite(data, 1, len, fd);
		bytes += len;
		chunkSize.push_back(len);
		latency.push_back(now_msec()-fetchTime);
		chunkStartTime.push_back(fetchTime);
		chunkFinishTime.push_back(now_msec());
	}
	fclose(fd);
	long finishTime = now_msec();
	say("Received file %s at %f MB/s (Time: %lu msec, Size: %d bytes)\n", fout, (1000 * (float)bytes / 1024) / (float)(finishTime - startTime) , finishTime - startTime, bytes);
	sendStreamCmd(sock, "done"); 	// chunk fetching ends, send command to server
	for (unsigned int i = 0; i < CIDs.size(); i++) {
		cout<<fout<<"\t"<<CIDs[i]<<"\t"<<chunkSize[i]<<" B\t"<<latency[i]<<"\t"<<chunkStartTime[i]<<"\t"<<chunkFinishTime[i]<<endl;
	}
	Xclose(chunkSock);
	Xclose(sock);
	return status;
}

int main(int argc, char **argv)
{
	while (1) {
		char myAD[MAX_XID_SIZE];
		char myHID[MAX_XID_SIZE];

		char ftpServAD[MAX_XID_SIZE];
		char ftpServHID[MAX_XID_SIZE];
		say("In main function of xftp_adv_client.\n");
		if (argc == 2) {
			say("\n%s (%s): started\n", TITLE, VERSION);
			strcpy(fin, argv[1]);
			sprintf(fout, "my%s", fin);

			ftpSock = initStreamClient(getXftpName(), myAD, myHID, ftpServAD, ftpServHID);
			//stageManagerSock is used to communicate with stage manager.
			//change it into Unix domain socket
			stageManagerSock = registerUnixStageManager(UNIXMANAGERSOCK);
			//stageManagerSock = registerStageManager(getStageManagerName());
say("The current stageManagerSock is %d\n", stageManagerSock);
			if (stageManagerSock == -1) {
				say("No local staging service running\n");
				stage = false;
			}
			getFile(ftpSock);
			close(stageManagerSock);
			return 0;
		}
		else {
			 die(-1, "Please input the filename as the second argument\n");
		}
	}

	return 0;
}