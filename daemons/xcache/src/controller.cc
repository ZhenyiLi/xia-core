#include <fcntl.h>
#include <string>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "controller.h"
#include <iostream>
#include "Xsocket.h"
#include "Xkeys.h"
#include "dagaddr.hpp"
#include "xcache_sock.h"

#define IGNORE_PARAM(__param) ((void)__param)

#define MAX_XID_SIZE 100

#define OK_SEND_RESPONSE 1
#define OK_NO_RESPONSE 0
#define BAD -1

static int context_id = 0;

static int xcache_create_click_socket(int port)
{
	struct sockaddr_in si_me;
    int s;

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		return -1;

    memset((char *)&si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&si_me, sizeof(si_me)) == -1)
		return -1;

    return s;
}

static int xcache_create_lib_socket(void)
{
	struct sockaddr_un my_addr;
	int s;
	char socket_name[512];

	s = socket(AF_UNIX, SOCK_SEQPACKET, 0);

	my_addr.sun_family = AF_UNIX;
	if(get_xcache_sock_name(socket_name, 512) < 0) {
		return -1;
	}

	remove(socket_name);

	strcpy(my_addr.sun_path, socket_name);

	bind(s, (struct sockaddr *)&my_addr, sizeof(my_addr));
	listen(s, 100);

	return s;
}

static void xcache_set_timeout(struct timeval *t)
{
	t->tv_sec = 1;
	t->tv_usec = 0;
}

void XcacheController::handleCli(void)
{
	char buf[512], *ret;
  
	ret = fgets(buf, 512, stdin);

	if(ret) {
		std::cout << "Received: " << buf;
		if(strncmp(buf, "store", strlen("store")) == 0) {
			std::cout << "Store Request" << std::endl;
		} else if(strncmp(buf, "search", strlen("search")) == 0) {
			std::cout << "Search Request" << std::endl;
		} else if(strncmp(buf, "status", strlen("status")) == 0) {
			std::cout << "status Request" << std::endl;
		} 
	}
}


void XcacheController::status(void)
{
	std::map<int32_t, XcacheSlice *>::iterator i;

	std::cout << "[Status]\n";
	for(i = sliceMap.begin(); i != sliceMap.end(); ++i) {
		i->second->status();
	}
}


void XcacheController::handleUdp(int s)
{
	IGNORE_PARAM(s);
}

int XcacheController::fetchContentRemote(XcacheCommand *resp, XcacheCommand *cmd)
{
	int ret, sock;
	socklen_t daglen;

	std::string dag(cmd->dag());
	daglen = dag.length();

	sock = Xsocket(AF_XIA, SOCK_STREAM, 0);
	if(sock < 0) {
		return BAD;
	}

	std::cout << "Fetching content from remote\n";

	if(Xconnect(sock, (struct sockaddr *)dag.c_str(), daglen) < 0) {
		return BAD;
	}

	std::cout << "-------------XCACHE_NOW_CONNECTED-----------------------\n";

	std::string data;
	char buf[512];
	while((ret = Xrecv(sock, buf, 512, 0)) == 512) {
		data += buf;
	}
	data += buf;

	std::cout << "Received DATA!!!!!!!!!!!!!! " << data << "\n";
	resp->set_cmd(XcacheCommand::XCACHE_RESPONSE);
	resp->set_data(data);

	return OK_SEND_RESPONSE;
}

int XcacheController::handleCmd(XcacheCommand *resp, XcacheCommand *cmd)
{
	int ret = OK_NO_RESPONSE;
	XcacheCommand response;

	if(cmd->cmd() == XcacheCommand::XCACHE_STORE) {
		std::cout << "Received STORE command \n";
		ret = store(cmd);
	} else if(cmd->cmd() == XcacheCommand::XCACHE_NEWSLICE) {
		std::cout << "Received NEWSLICE command \n";
		ret = newSlice(resp, cmd);
	} else if(cmd->cmd() == XcacheCommand::XCACHE_GETCHUNK) {
		// FIXME: Handle local fetch
		std::cout << "Received GETCHUNK command \n";
		ret = fetchContentRemote(resp, cmd);
		// ret = search(resp, cmd);
	} else if(cmd->cmd() == XcacheCommand::XCACHE_STATUS) {
		std::cout << "Received STATUS command \n";
		status();
	}

	return ret;
}


XcacheSlice *XcacheController::lookupSlice(XcacheCommand *cmd)
{
	std::map<int32_t, XcacheSlice *>::iterator i = sliceMap.find(cmd->contextid());

	if(i != sliceMap.end()) {
		std::cout << "[Success] Slice Exists.\n";
		return i->second;
	} else {
		/* TODO use default slice */
		std::cout << "Slice does not exist. Falling back to default slice.\n";
		return NULL;
	}
}


int XcacheController::newSlice(XcacheCommand *resp, XcacheCommand *cmd)
{
	XcacheSlice *slice;

	if(lookupSlice(cmd)) {
		/* Slice already exists */
		return -1;
	}

	slice = new XcacheSlice(++context_id);

	//  slice->setPolicy(cmd->cachepolicy());
	slice->setTtl(cmd->ttl());
	slice->setSize(cmd->cachesize());

	sliceMap[slice->getContextId()] = slice;
	resp->set_cmd(XcacheCommand::XCACHE_RESPONSE);
	resp->set_contextid(slice->getContextId());

	return OK_SEND_RESPONSE;
}

int XcacheController::store(XcacheCommand *cmd)
{
	XcacheSlice *slice;
	XcacheMeta *meta;
	std::string emptyStr("");
	std::map<std::string, XcacheMeta *>::iterator i = metaMap.find(cmd->cid());

	std::cout << "Reached " << __func__ << std::endl;

	if(i != metaMap.end()) {
		meta = i->second;
		std::cout << "Meta Exsits." << std::endl;
	} else {
		/* New object - Allocate a meta */
		meta = new XcacheMeta(cmd);
		metaMap[cmd->cid()] = meta;
		std::cout << "New Meta." << std::endl;
	}

	slice = lookupSlice(cmd);
	if(!slice)
		return -1;

	if(slice->store(meta) < 0) {
		std::cout << "Slice store failed\n";
		return -1;
	}

	std::string tempCID("CID:");
	tempCID += meta->getCid();

	std::cout << "Setting Route for " << tempCID << "\n";
	std::cout << "RV=" << xr.setRoute(tempCID, DESTINED_FOR_LOCALHOST, emptyStr, 0) << "\n";

	return storeManager.store(meta, cmd->data());
}

int XcacheController::search(XcacheCommand *resp, XcacheCommand *cmd)
{
	int xcacheRecvSock = Xsocket(AF_XIA, SOCK_STREAM, 0);

	IGNORE_PARAM(resp);

#ifdef TODOEnableLocalSearch
	XcacheSlice *slice;
	Graph g(cmd->dag);

	std::cout << "Search Request\n";
	slice = lookupSlice(cmd);
	if(!slice) {
		resp->set_cmd(XcacheCommand::XCACHE_ERROR);
	} else {
		std::string data = slice->search(cmd);

		resp->set_cmd(XcacheCommand::XCACHE_RESPONSE);
		resp->set_data(data);
		std::cout << "Looked up Data = " << data << "\n";
	}
#else
  
	if(Xconnect(xcacheRecvSock, (struct sockaddr *)&cmd->dag(), sizeof(cmd->dag())) < 0) {
		std::cout << "TODO THIS IS ERROR\n";
	};

#endif

	return OK_SEND_RESPONSE;
}

void *XcacheController::startXcache(void *arg)
{
	XcacheController *ctrl = (XcacheController *)arg;
	ctrl = ctrl; //fixme
	char sid_string[strlen("SID:") + XIA_SHA_DIGEST_STR_LEN];
	int xcacheSock, acceptSock;

	if ((xcacheSock = Xsocket(AF_XIA, SOCK_STREAM, 0)) < 0)
		return NULL;

	if(XmakeNewSID(sid_string, sizeof(sid_string))) {
		std::cout << "Could not allocate SID for xcache\n";
		return NULL;
	}

	if(XsetXcacheSID(xcacheSock, sid_string, strlen(sid_string)) < 0)
		return NULL;

	std::cout << "XcacheSID is " << sid_string << "\n";

	struct addrinfo *ai;

	if (Xgetaddrinfo(NULL, sid_string, NULL, &ai) != 0)
		return NULL;

	sockaddr_x *dag = (sockaddr_x*)ai->ai_addr;

	if (Xbind(xcacheSock, (struct sockaddr*)dag, sizeof(dag)) < 0) {
		Xclose(xcacheSock);
		return NULL;
	}

	Xlisten(xcacheSock, 5);

	Graph g(dag);
	std::cout << "listening on dag: " << g.dag_string() << "\n";

	while(1) {
		sockaddr_x mypath;
		socklen_t mypath_len = sizeof(mypath);

		std::cout << "XcacheSender waiting for incoming connections\n";
		if ((acceptSock = XacceptAs(xcacheSock, (struct sockaddr *)&mypath, &mypath_len, NULL, NULL)) < 0) {
			std::cout << "Xaccept failed\n";
			pthread_exit(NULL);
		}

		// FIXME: Send appropriate data, perform actual search,
		// make updates in slices / policies / stores

		std::cout << "Accept Success\n";
 		Graph g(&mypath);
 		std::cout << "They want " << g.get_final_intent().to_string() << "\n";
#define DATA "IfYouReceiveThis!"
 		Xsend(acceptSock, DATA, strlen(DATA), 0);
 		Xclose(acceptSock);
	}
}

void XcacheController::run(void)
{
	fd_set fds, allfds;
	struct timeval timeout;
	int max, libsocket, s, n, rc;
	pthread_t xcacheSender;

	std::vector<int> activeConnections;
	std::vector<int>::iterator iter;

	s = xcache_create_click_socket(1444);
	libsocket = xcache_create_lib_socket();

	pthread_create(&xcacheSender, NULL, startXcache, NULL);
	if ((rc = xr.connect()) != 0) {
		std::cout << "unable to connect to click " << rc << "\n";
		return;
	}

	xr.setRouter(hostname); 

	FD_ZERO(&fds);
	FD_SET(s, &allfds);
	FD_SET(libsocket, &allfds);
	//#define MAX(_a, _b) ((_a > _b) ? (_a) : (_b))

	xcache_set_timeout(&timeout);

	std::cout << "Entering The Loop\n";

	while(1) {
		memcpy(&fds, &allfds, sizeof(fd_set));

		max = MAX(libsocket, s);
		for(iter = activeConnections.begin(); iter != activeConnections.end(); ++iter) {
			max = MAX(max, *iter);
		}

		n = Xselect(max + 1, &fds, NULL, NULL, NULL);

		std::cout << "Broken\n";
		if(FD_ISSET(s, &fds)) {
			std::cout << "Action on UDP" << std::endl;
			handleUdp(s);
		}

		if(FD_ISSET(libsocket, &fds)) {
			int new_connection = accept(libsocket, NULL, 0);
			std::cout << "Action on libsocket" << std::endl;
			activeConnections.push_back(new_connection);
			FD_SET(new_connection, &allfds);
		}

		for(iter = activeConnections.begin(); iter != activeConnections.end();) {
			if(FD_ISSET(*iter, &fds)) {
				char buf[512] = "";
				std::string buffer("");
				XcacheCommand resp, cmd;
				int ret;

				do {
					printf("Reading\n");
					ret = recv(*iter, buf, 512, 0);
					if(ret == 0)
						break;

					printf("ret = %d\n", ret);
					buffer.append(buf, ret);
				} while(ret == 512);

				if(ret != 0) {
					bool parseSuccess = cmd.ParseFromString(buffer);
					printf("%s: Controller received %lu bytes\n", __func__, buffer.length());
					if(!parseSuccess) {
						std::cout << "[ERROR] Protobuf could not parse\n;";
					} else {
						if(handleCmd(&resp, &cmd) == OK_SEND_RESPONSE) {
							resp.SerializeToString(&buffer);
							if(write(*iter, buffer.c_str(), buffer.length()) < 0) {
								std::cout << "FIXME: handle return value of write\n";
							}
						}
					}
				}

				if(ret == 0) {
					std::cout << "Closing\n";
					close(*iter);
					FD_CLR(*iter, &allfds);
					activeConnections.erase(iter);
					continue;
				}
			}
			++iter;
		}

		if((n == 0) && (timeout.tv_sec == 0) && (timeout.tv_usec == 0)) {
			// std::cout << "Timeout" << std::endl;
		}
	}
}
