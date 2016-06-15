#include "stage_utils.h"

using namespace std;
string lastSSID, currSSID;
//TODO: better to rename in case of mixing up with the chunkProfile struct in server   --Lwy   1.16
struct chunkProfile {
    int state;
    string dag;
    long stageStartTimestemp;
    long stageFinishTimestemp;

    chunkProfile(string _dag = "", int _state = BLANK): state(_state), dag(_dag) {}
};

int rttWifi = 0, rttInt = 0;
long timeWifi = 0, timeInt = 0;
int chunkToStage = 3;

map<int, vector<string> > SIDToDAGs;
map<int, map<string, chunkProfile> > SIDToProfile;
map<int, int> stageIndex;

bool netStageOn = true;
int thread_c = 0;

pthread_mutex_t profileLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t dagVecLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stageMutex = PTHREAD_MUTEX_INITIALIZER;
// TODO: this is not easy to manage in a centralized manner because we can have multiple threads for staging data due to mobility and we should have a pair of mutex and cond for each thread
pthread_mutex_t StageControl = PTHREAD_MUTEX_INITIALIZER;

//Update the stage window
void updateStageArg() {
    if (rttWifi && rttInt && timeWifi && timeInt) {
        int left = timeWifi + rttWifi;
        int right = timeInt + rttWifi + rttInt;
        chunkToStage = static_cast<double>(left) / right + 1;
    }
    else {
        chunkToStage = 3;
    }
}
// TODO: mem leak; window pred alg; int netMonSock;
void regHandler(int sock, char *cmd)
{
    say("In regHandler. CMD: %s\n", cmd);
    if (strncmp(cmd, "reg cont", 8) == 0) {
        say("Receiving partial chunk list\n");
        char cmd_arr[strlen(cmd + 9)];
        strcpy(cmd_arr, cmd + 9);
        char *dag, *saveptr;
        pthread_mutex_lock(&dagVecLock);
        pthread_mutex_lock(&profileLock);
        dag = strtok_r(cmd_arr, " ", &saveptr);

        SIDToDAGs[sock].push_back(dag);
        SIDToProfile[sock][dag] = chunkProfile(string(dag), BLANK);
        while ((dag = strtok_r(NULL, " ", &saveptr)) != NULL) {
            SIDToDAGs[sock].push_back(dag);
            SIDToProfile[sock][dag] = chunkProfile(dag, BLANK);
            say("DAG: %s\n", dag);
        }
        say("++++++++++++++++++++++++++++++++The remoteSID is %d\n", sock);
        pthread_mutex_unlock(&profileLock);
        pthread_mutex_unlock(&dagVecLock);
        return;
    }

    else if (strncmp(cmd, "reg done", 8) == 0) {
        pthread_mutex_lock(&stageMutex);
        stageIndex[sock] = 0;
        pthread_mutex_unlock(&stageMutex);
        pthread_mutex_unlock(&StageControl);
        say("Receive the reg done command\n");
        return;
    }
}

int delegationHandler(int sock, char *cmd)
{
    say("In delegationHandler.\nThe command is %s\n", cmd);

    string tmp;
    pthread_mutex_lock(&dagVecLock);
    auto iter = find(SIDToDAGs[sock].begin(), SIDToDAGs[sock].end(), cmd);
    if (!netStageOn || iter == SIDToDAGs[sock].end()) { // not register
        tmp = cmd;
        pthread_mutex_unlock(&dagVecLock);
    }
    else {
        auto dis = distance(SIDToDAGs[sock].begin(), iter);
        pthread_mutex_unlock(&dagVecLock);
        //SIDToDAGs[sock].erase();
        pthread_mutex_lock(&stageMutex);
        stageIndex[sock] = dis;
        pthread_mutex_unlock(&stageMutex);

        while (true) {
            pthread_mutex_lock(&profileLock);
            if (SIDToProfile[sock][cmd].state == READY) {
                //SIDToProfile[sock][cmd].state = IGNORE;
                break;
                say("DAG: %s change into IGNORE!\n", cmd);
            }
            pthread_mutex_unlock(&profileLock);
        }
        tmp = SIDToProfile[sock][cmd].dag;
        //SIDToProfile[sock].erase(cmd);
        pthread_mutex_unlock(&profileLock);
    }
    if (send(sock, tmp.c_str(), tmp.size(), 0) < 0) {
        warn("socket error while sending data, closing connection\n");
        return -1;
    }
    say("End of delegationHandler");
    return 0;
}

void *clientCmd(void *socketid)
{
    char cmd[XIA_MAXBUF];
    int sock = *((int*)socketid);
    int n;

    pthread_mutex_lock(&stageMutex);
    stageIndex[sock] = 0;
    pthread_mutex_unlock(&stageMutex);
    while (1) {
        memset(cmd, 0, sizeof(cmd));
        if ((n = recv(sock, cmd, sizeof(cmd), 0)) < 0) {
            warn("socket error while waiting for data, closing connection\n");
            break;
        }
        else {
            if (n == 0) {
                say("Unix Socket closed by client.");
                break;
            }
        }
        say("clientCmd````````````````````````````````Receive the command that: %s\n", cmd);
        // registration msg from xftp client: reg DAG1, DAG2, ... DAGn
        if (strncmp(cmd, "reg", 3) == 0) {
            say("Receive a chunk list registration message\n");
            regHandler(sock, cmd);
            //pthread_mutex_unlock(&StageControl);
        }
        else if (strncmp(cmd, "fetch", 5) == 0) {
            say("Receive a chunk request\n");
            if (delegationHandler(sock, cmd + 6) < 0) {
                break;
            }
            pthread_mutex_unlock(&StageControl);
        }
        else if (strncmp(cmd, "time", 4) == 0) {
            sscanf(cmd, "time %ld", &timeWifi);
            updateStageArg();
        }
        //usleep(SCAN_DELAY_MSEC*1000);
    }
    close(sock);
    say("Socket closed\n");
    pthread_mutex_lock(&profileLock);
    SIDToProfile.erase(sock);
    pthread_mutex_unlock(&profileLock);
    pthread_mutex_lock(&dagVecLock);
    SIDToDAGs.erase(sock);
    pthread_mutex_unlock(&dagVecLock);
    pthread_exit(NULL);
}

// WORKING version
// TODO: scheduling
// control plane: figure out the right DAGs to stage, change the stage state from BLANK to PENDING to READY
void *stageData(void *)
{
    thread_c++;
    int thread_id = thread_c;
    cerr << "Thread id " << thread_id << ": " << "Is launched\n";
    cerr << "Current " << getAD() << endl;

    char myAD[MAX_XID_SIZE];
    char myHID[MAX_XID_SIZE];
    char stageAD[MAX_XID_SIZE];
    char stageHID[MAX_XID_SIZE];

//netStageSock is used to communicate with stage server.
    getNewAD(myAD);
    //Connect to the Stage Server
    int netStageSock = registerStageService(getStageServiceName(), myAD, myHID, stageAD, stageHID);
    //Rtt of wireless
    rttWifi = getRTT(getStageServiceName());
    char rttCMD[100] = "";
    //Ask the Stage server to get the Rtt of Internet
    sprintf(rttCMD, "xping %s", getXftpName());
    if (Xsend(netStageSock, rttCMD, strlen(rttCMD), 0) < 0
            || Xrecv(netStageSock, rttCMD, sizeof(rttCMD), 0) < 0) {
        die(-1,"Unable to get the RTT.\n");
    }
    sscanf(rttCMD, "rtt %d", &rttInt);
    //Update stage window
    updateStageArg();
    say("++++++++++++++++++++++++++++++++++++The current netStageSock is %d\n", netStageSock);
    say("RTTWireless = %d RTTInternet = %d\n", rttWifi, rttInt);
    if (netStageSock == -1) {
        say("netStageOn is false!\n");
        netStageOn = false;
        pthread_exit(NULL);
    }

    // TODO: need to handle the case that new SID joins dynamically
    // TODO: handle no in-net staging service
    while (1) {
        pthread_mutex_lock(&StageControl);
        say("*********************************In while loop of stageData\n");
        string currSSID = getSSID();
        if (lastSSID != currSSID) {
            say("Thread id: %d Network changed, create another thread to continue!\n");
            lastSSID = currSSID;
            pthread_t thread_stageDataNew;
            pthread_mutex_unlock(&StageControl);
            pthread_create(&thread_stageDataNew, NULL, stageData, NULL);
            pthread_exit(NULL);
        }
        set<string> needStage;
        pthread_mutex_lock(&dagVecLock);
        for (auto pair : SIDToDAGs) {
            int sock = pair.first;
            vector<string>& dags = pair.second;
            say("Handling the sock: %d\n", sock);
            pthread_mutex_lock(&profileLock);
            pthread_mutex_lock(&stageMutex);
            int beg = stageIndex[sock];
            stageIndex[sock] = -1;
            if (beg == -1)
                continue;
            for (int i = beg, j = 0; j <= chunkToStage && i < int(dags.size()); ++i) {
                if (SIDToProfile[sock][dags[i]].state == BLANK) {
                    needStage.insert(dags[i]);
                    j++;
                }
            }
            pthread_mutex_unlock(&stageMutex);
            pthread_mutex_unlock(&profileLock);
            if (needStage.size() == 0) {
                continue;
            }
            char cmd[XIA_MAX_BUF] = {0};
            char reply[XIA_MAX_BUF] = {0};

            sprintf(cmd, "stage");
            for (auto dag : needStage) {
                say("needStage: %s\n", dag.c_str());
                sprintf(cmd, "%s %s", cmd, dag.c_str());
            }

            sendStreamCmd(netStageSock, cmd);
            for (int i = 0; i < static_cast<int>(needStage.size()); ++i) {
                sayHello(netStageSock, "READY TO RECEIVE!");
                if (Xrecv(netStageSock, reply, sizeof(reply), 0) < 0) {
                    Xclose(netStageSock);
                    die(-1, "Unable to communicate with the server\n");
                }
                char oldDag[256];
                char newDag[256];
                sscanf(reply, "%*s %s %s %ld", oldDag, newDag, &timeInt);
                updateStageArg();
                say("cmd: %s, old:%s  new:%s\n", reply, oldDag, newDag);
                pthread_mutex_lock(&profileLock);
                SIDToProfile[sock][oldDag].dag = newDag;
                SIDToProfile[sock][oldDag].state = READY;
                pthread_mutex_unlock(&profileLock);
            }
        }
        pthread_mutex_unlock(&dagVecLock);
    }
    pthread_exit(NULL);
}
int main()
{
    lastSSID = getSSID();
    currSSID = lastSSID;
    int stageSock = registerUnixStreamReceiver(UNIXMANAGERSOCK);
    pthread_t thread_stageData;

    pthread_create(&thread_stageData, NULL, stageData, NULL);
    UnixBlockListener((void*)&stageSock, clientCmd);
    return 0;
}
