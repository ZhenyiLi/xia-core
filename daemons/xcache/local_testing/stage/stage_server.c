#include "stage_utils.h"

using namespace std;

char myAD[MAX_XID_SIZE];
char myHID[MAX_XID_SIZE];
char my4ID[MAX_XID_SIZE];

int stageServerSock;

pthread_mutex_t profileLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t bufLock = PTHREAD_MUTEX_INITIALIZER;
//pthread_mutex_t timeLock = PTHREAD_MUTEX_INITIALIZER;

struct chunkProfile {
	int fetchState;
	long fetchStartTimestamp;
	long fetchFinishTimestamp;
	sockaddr_x oldDag;
	sockaddr_x newDag;
};

// Used by different service to communicate with each other.
map<string, map<string, chunkProfile> > SIDToProfile;	// stage state
map<string, vector<sockaddr_x> > SIDToBuf; // chunk buffer to stage
//map<string, long> SIDToTime; // timestamp last seen

// TODO: non-blocking fasion -- stage manager can send another stage request before get a ready response
// put the CIDs into the stage buffer, and "ready" msg one by one
void stageControl(int sock, char *cmd)
{
	//pthread_mutex_lock(&timeLock);
	//SIDToTime[remoteSID] = now_msec();
	//pthread_mutex_unlock(&timeLock);

	char remoteAD[MAX_XID_SIZE];
	char remoteHID[MAX_XID_SIZE];
	char remoteSID[MAX_XID_SIZE];
	
	XgetRemoteAddr(sock, remoteAD, remoteHID, remoteSID); // get stage manager's SID

	vector<string> CIDs = strVector(cmd);

	for(auto CID: CIDs)
		cerr << "test !!!!!!!" << CID << endl;
	pthread_mutex_lock(&profileLock);
	for (auto CID : CIDs) {
		SIDToProfile[remoteSID][CID].fetchState = BLANK;
		url_to_dag(&SIDToProfile[remoteSID][CID].oldDag, (char*)CID.c_str(), CID.size());		
		SIDToProfile[remoteSID][CID].fetchStartTimestamp = 0;
		SIDToProfile[remoteSID][CID].fetchFinishTimestamp = 0;
	}
	pthread_mutex_unlock(&profileLock);

	// put the CIDs into the buffer to be staged
	pthread_mutex_lock(&bufLock);
	for (unsigned int i = 0; i < CIDs.size(); i++)
		SIDToBuf[remoteSID].push_back(SIDToProfile[remoteSID][CIDs[i]].oldDag);
	pthread_mutex_unlock(&bufLock);

	// send the chunk ready msg one by one
	char url[256];
	char oldUrl[256];
	for (int i = 0; i < CIDs.size();++i) {
		dag_to_url(oldUrl, 256, &SIDToProfile[remoteSID][CIDs[i]].oldDag);
		while (1) {
say("In stage control, getting chunk: %s\n", oldUrl);

            //add the lock and unlock action    --Lwy   1.16					
			if (SIDToProfile[remoteSID][oldUrl].fetchState == READY) {
				char reply[XIA_MAX_BUF] = {0};
				dag_to_url(url, 256, &SIDToProfile[remoteSID][oldUrl].newDag);
								
				sprintf(reply, "ready %s %s", oldUrl, url);
				hearHello(sock);

				///////////////////// 
				/////////////////////
				/////////////////////
				/////////////////////
				/////////////////////
				/////////////////////

				// Send chunk ready message to state manager.
				sendStreamCmd(sock, reply);
				say("xsend return ----- xsend return ---- xsend return ----  %s", CIDs[i].c_str());
				//pthread_mutex_unlock(&profileLock);				
				break;
			}
			// Determine the intervals to check the state of current chunk.
			usleep(SCAN_DELAY_MSEC*1000); // chenren: check timing issue
		}
	}

	pthread_mutex_lock(&profileLock);
	SIDToProfile.erase(remoteSID);
	pthread_mutex_unlock(&profileLock);
	pthread_mutex_lock(&bufLock);
	SIDToBuf.erase(remoteSID);
	pthread_mutex_unlock(&bufLock);
	//pthread_mutex_lock(&timeLock);
	//SIDToTime.erase(remoteSID);
	//pthread_mutex_unlock(&timeLock);

	return;
}

// TODO: paralize getting chunks for each SID, i.e. fair scheduling
// read the CID from the stage buffer, execute staging, and update profile
void *stageData(void *)
{
	int chunkSock;

	XcacheHandle xcache;
	XcacheHandleInit(&xcache);
    // Create socket with server.
	if ((chunkSock = Xsocket(AF_XIA, XSOCK_CHUNK, 0)) < 0) {
		die(-1, "unable to create chunk socket\n");
	}

	while (1) {
		// pop out the chunks for staging
		pthread_mutex_lock(&bufLock);
		// For each stage manager.
		for (auto I : SIDToBuf) {
			if (I.second.size() > 0) {
				string SID = I.first;

                // For each Content.
                		//pthread_mutex_lock(&profileLock);
				//for (auto CID : I.second)
				//	SIDToProfile[SID][CID].fetchStartTimestamp = now_msec();
				//pthread_mutex_unlock(&profileLock);

				char buf[1024 * 1024];
				int ret;
say("Fetching chunks from server. The number of chunks is none, the first chunk is noe\n");
				for(auto addr : I.second){
					char CID[256];
					
					//sockaddr_x addr;
					//url_to_dag(&addr, (char*)CID.c_str(), CID.size());
					dag_to_url(CID, 256, &addr);					
					if ((ret = XfetchChunk(&xcache, buf, 1024 * 1024, XCF_BLOCK, &addr, sizeof(addr))) < 0) {
						say("unable to request chunks\n");
                        //add unlock function   --Lwy   1.16
                        pthread_mutex_unlock(&bufLock);
						pthread_exit(NULL);
					}

					pthread_mutex_lock(&profileLock);
					if(XputChunk(&xcache, (const char* )buf, ret, &SIDToProfile[SID][CID].newDag) < 0){
						say("unable to put chunks\n");
						pthread_mutex_unlock(&bufLock);
						pthread_mutex_unlock(&profileLock);
						pthread_exit(NULL);
					}
					if (SIDToProfile[SID][CID].fetchFinishTimestamp == 0) {
						SIDToProfile[SID][CID].fetchFinishTimestamp = now_msec();
					}
					SIDToProfile[SID][CID].fetchState = READY;
					pthread_mutex_unlock(&profileLock);

				}
				//I.second.clear(); // clear the buf
				// TODO: timeout the chunks by free(cs[i].cid); cs[j].cid = NULL; cs[j].cidLen = 0;
			}
		}
		pthread_mutex_unlock(&bufLock);
		usleep(SCAN_DELAY_MSEC*1000);
	}
	pthread_exit(NULL);
}

void *stageCmd(void *socketid)
{
	char cmd[XIA_MAXBUF];
	int sock = *((int*)socketid);
	int n = -1;

	while (1) {
say("In stageCmd.\n");
		memset(cmd, '\0', strlen(cmd));
		// Receive the stage command sent by stage_manager.
		if ((n = Xrecv(sock, cmd, sizeof(cmd), 0))  < 0) {
			warn("socket error while waiting for data, closing connection\n");
			break;
		}
say("Successfully receive stage command from stage manager.\n");
		if (strncmp(cmd, "stage", 5) == 0) {
			say("Receive a stage message: %s\n", cmd);
			stageControl(sock, cmd+6);
		}
	}

	Xclose(sock);
	say("Socket closed\n");
	pthread_exit(NULL);
}

int main()
{
	pthread_t thread_stage;
	pthread_create(&thread_stage, NULL, stageData, NULL); // dequeue, stage and update profile

	stageServerSock = registerStreamReceiver(getStageServiceName(), myAD, myHID, my4ID);
say("The current stageServerSock is %d\n", stageServerSock);
	blockListener((void *)&stageServerSock, stageCmd);

	return 0;
}
