#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <libgen.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <string>
#include <vector>
#include <map>
#include <time.h>

#include <sys/types.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include "Xsocket.h"
#include "xcontrold.hh"
#include "dagaddr.hpp"

#define DEFAULT_NAME "controller0"
#define APPNAME "xcontrold"
#define EXPIRE_TIME 60

// Main loop iterates every 1000 usec = 1 ms = 0.001 sec
#define MAIN_LOOP_USEC 1000
#define RECV_ITERS 2
#define HELLO_ITERS 100
#define LSA_ITERS 400

char *hostname = NULL;
char *ident = NULL;

int ctrl_sock;
RouteState route_state;
XIARouter xr;
map<string,time_t> timeStamp;


// send Hello message (1-hop broadcast) with my AD and my HID to the directly connected neighbors
int sendHello()
{
	ControlMessage msg1(CTL_HELLO, route_state.myAD, route_state.myHID);
	int rc1 = msg1.send(route_state.sock, &route_state.ddag);

	// Advertize controller service
	ControlMessage msg2(CTL_HELLO, route_state.myAD, route_state.myHID);
	msg2.append(SID_XCONTROL);
	int rc2 = msg2.send(route_state.sock, &route_state.ddag);

printf("sendhello %d %d\n", rc1, rc2);
	return (rc1 < rc2)? rc1 : rc2;
}

// send LinkStateAdvertisement message to neighboring ADs (flooding)
/* Message format (delimiter=^)
    message-type{LSA=1}
    source-AD
    source-HID
    router-type{XIA=0 or XIA-IPv4-Dual=1}
    LSA-seq-num
    num_neighbors
    neighbor1-AD
    neighbor1-HID
    neighbor2-AD
    neighbor2-HID
    ...
*/
int sendInterdomainLSA()
{
	int rc = 1;
    ControlMessage msg(CTL_XBGP, route_state.myAD, route_state.myAD);

    msg.append(route_state.dual_router);
    msg.append(route_state.lsa_seq);
    msg.append(route_state.ADNeighborTable.size());

    std::map<std::string, NeighborEntry>::iterator it;
    for (it = route_state.ADNeighborTable.begin(); it != route_state.ADNeighborTable.end(); it++)
    {
        msg.append(it->second.AD);
        msg.append(it->second.HID);
        msg.append(it->second.port);
        msg.append(it->second.cost);
	}

    for (it = route_state.ADNeighborTable.begin(); it != route_state.ADNeighborTable.end(); it++)
    {
		sockaddr_x ddag;
		Graph g = Node() * Node(it->second.AD) * Node(SID_XCONTROL);
		g.fill_sockaddr(&ddag);

		//syslog(LOG_INFO, "send inter-AD LSA[%d] to %s", route_state.lsa_seq, it->second.AD.c_str());
		//syslog(LOG_INFO, "msg: %s", msg.c_str());
		int temprc = msg.send(route_state.sock, &ddag);
		if (temprc < 0) {
			syslog(LOG_ERR, "error sending inter-AD LSA to %s", it->second.AD.c_str());
		}
		rc = (temprc < rc)? temprc : rc;
printf("sendinterdomainlsa %d\n", temprc);
  	}

	route_state.lsa_seq = (route_state.lsa_seq + 1) % MAX_SEQNUM;

	return rc;
}

int processInterdomainLSA(ControlMessage msg)
{
	// 0. Read this LSA
	int32_t isDualRouter, numNeighbors, lastSeq;
	string srcAD, srcHID;

	msg.read(srcAD);
	msg.read(srcHID);
	msg.read(isDualRouter);


	// See if this LSA comes from AD with dualRouter
	if (isDualRouter == 1)
		route_state.dual_router_AD = srcAD;

	// First, filter out the LSA originating from myself
	if (srcAD == route_state.myAD)
		return 1;

	msg.read(lastSeq);

	// 1. Filter out the already seen LSA
	if (route_state.ADLastSeqTable.find(srcAD) != route_state.ADLastSeqTable.end()) {
		int32_t old = route_state.ADLastSeqTable[srcAD];
		if (lastSeq <= old && (old - lastSeq) < SEQNUM_WINDOW) {
			// drop the old LSA update.
			return 1;
		}
	}

	//syslog(LOG_INFO, "inter-AD LSA[%d] from %s", lastSeq, srcAD.c_str());

	route_state.ADLastSeqTable[srcAD] = lastSeq;
	
	msg.read(numNeighbors);

	// 2. Update the network table
	NodeStateEntry entry;
	entry.ad = srcAD;
	entry.hid = srcAD;
	entry.num_neighbors = numNeighbors;

	for (int i = 0; i < numNeighbors; i++)
	{
		NeighborEntry neighbor;
		msg.read(neighbor.AD);
		msg.read(neighbor.HID);
		msg.read(neighbor.port);
		msg.read(neighbor.cost);

		//syslog(LOG_INFO, "neighbor[%d] = %s", i, neighbor.AD.c_str());

		entry.neighbor_list.push_back(neighbor);
	}

	route_state.ADNetworkTable[srcAD] = entry;

	// Rebroadcast this LSA
    int rc = 1;
	std::map<std::string, NeighborEntry>::iterator it;
    for (it = route_state.ADNeighborTable.begin(); it != route_state.ADNeighborTable.end(); it++) {
		sockaddr_x ddag;
		Graph g = Node() * Node(it->second.AD) * Node(SID_XCONTROL);
		g.fill_sockaddr(&ddag);

		int temprc = msg.send(route_state.sock, &ddag);
		rc = (temprc < rc)? temprc : rc;
	}
	return rc;
}

int sendRoutingTable(std::string destHID, std::map<std::string, RouteEntry> routingTable)
{
	if (destHID == route_state.myHID) {
		// If destHID is self, process immediately
		return processRoutingTable(routingTable);
	} else {
		// If destHID is not SID, send to relevant router
		ControlMessage msg(CTL_ROUTING_TABLE, route_state.myAD, route_state.myHID);

		msg.append(route_state.myAD);
		msg.append(destHID);

		msg.append(route_state.ctl_seq);

		msg.append((int)routingTable.size());

		map<string, RouteEntry>::iterator it;
		for (it = routingTable.begin(); it != routingTable.end(); it++)
		{
			msg.append(it->second.dest);
			msg.append(it->second.nextHop);
			msg.append(it->second.port);
			msg.append(it->second.flags);
		}

		route_state.ctl_seq = (route_state.ctl_seq + 1) % MAX_SEQNUM;

		return msg.send(route_state.sock, &route_state.ddag);
	}
}

// Process received message.
int processMsg(std::string msg)
{
    int type, rc = 0;
    ControlMessage m(msg);

    m.read(type);

    switch (type)
    {
        case CTL_HOST_REGISTER:
			// Ignore. Controller does not register hosts.
            break;
        case CTL_HELLO:
            rc = processHello(m);
            break;
        case CTL_LSA:
            rc = processLSA(m);
            break;
        case CTL_ROUTING_TABLE:
            // rc = processRoutingTable(m);
            break;
		case CTL_XBGP:
			rc = processInterdomainLSA(m);
			break;
        default:
            perror("unknown routing message");
            break;
    }

    return rc;
}

int interfaceNumber(std::string xidType, std::string xid)
{
	int rc;
	vector<XIARouteEntry> routes;
	if ((rc = xr.getRoutes(xidType, routes)) > 0) {
		vector<XIARouteEntry>::iterator ir;
		for (ir = routes.begin(); ir < routes.end(); ir++) {
			XIARouteEntry r = *ir;
			if ((r.xid).compare(xid) == 0) {
				return (int)(r.port);
			}
		}
	}
	return -1;
}

int processHello(ControlMessage msg)
{
	// Update neighbor table
    NeighborEntry neighbor;
    msg.read(neighbor.AD);
    msg.read(neighbor.HID);
    neighbor.port = interfaceNumber("HID", neighbor.HID);
    neighbor.cost = 1; // for now, same cost

    // Index by HID if neighbor in same domain or by AD otherwise
    bool internal = (neighbor.AD == route_state.myAD);
    route_state.neighborTable[internal ? neighbor.HID : neighbor.AD] = neighbor;
    route_state.num_neighbors = route_state.neighborTable.size();

    // Update network table
    std::string myHID = route_state.myHID;

	NodeStateEntry entry;
	entry.hid = myHID;
	entry.num_neighbors = route_state.num_neighbors;

    // Add neighbors to network table entry
    std::map<std::string, NeighborEntry>::iterator it;
    for (it = route_state.neighborTable.begin(); it != route_state.neighborTable.end(); it++)
 		entry.neighbor_list.push_back(it->second);

	route_state.networkTable[myHID] = entry;

	return 1;
}

int processRoutingTable(std::map<std::string, RouteEntry> routingTable)
{
	int rc;
	map<string, RouteEntry>::iterator it;
	for (it = routingTable.begin(); it != routingTable.end(); it++)
	{
		// TODO check for all published SIDs
		// TODO do this for xrouted as well
		// Ignore SIDs that we publish
		if (it->second.dest == SID_XCONTROL) {
			continue;
		}
		if ((rc = xr.setRoute(it->second.dest, it->second.port, it->second.nextHop, it->second.flags)) != 0)
			syslog(LOG_ERR, "error setting route %d", rc);

        timeStamp[it->second.dest] = time(NULL);
	}

	return 1;
}

/* Procedure:
   0. scan this LSA (mark AD with a DualRouter if there)
   1. filter out the already seen LSA (via LSA-seq for this dest)
   2. update the network table
   3. rebroadcast this LSA
*/
int processLSA(ControlMessage msg)
{
	// 0. Read this LSA
	int32_t isDualRouter, numNeighbors, lastSeq;
	string srcAD, srcHID;

	msg.read(srcAD);
	msg.read(srcHID);
	msg.read(isDualRouter);

	// See if this LSA comes from AD with dualRouter
	if (isDualRouter == 1)
		route_state.dual_router_AD = srcAD;

	// First, filter out the LSA originating from myself
	if (srcHID == route_state.myHID)
		return 1;

	msg.read(lastSeq);

	// 1. Filter out the already seen LSA
	if (route_state.lastSeqTable.find(srcHID) != route_state.lastSeqTable.end()) {
		int32_t old = route_state.lastSeqTable[srcHID];
		if (lastSeq <= old && (old - lastSeq) < SEQNUM_WINDOW) {
			// drop the old LSA update.
			return 1;
		}
	}

	route_state.lastSeqTable[srcHID] = lastSeq;
	
	msg.read(numNeighbors);

	// 2. Update the network table
	NodeStateEntry entry;
	entry.ad = srcAD;
	entry.hid = srcHID;
	entry.num_neighbors = numNeighbors;

	for (int i = 0; i < numNeighbors; i++)
	{
		NeighborEntry neighbor;
		msg.read(neighbor.AD);
		msg.read(neighbor.HID);
		msg.read(neighbor.port);
		msg.read(neighbor.cost);

		entry.neighbor_list.push_back(neighbor);
	}

	route_state.networkTable[srcHID] = entry;
	route_state.calc_dijstra_ticks++;

	if (route_state.calc_dijstra_ticks >= CALC_DIJKSTRA_INTERVAL)
	{
		syslog(LOG_DEBUG, "Calcuating shortest paths\n");

		// Calculate next hop for ADs
		std::map<std::string, RouteEntry> ADRoutingTable;
		populateRoutingTable(route_state.myAD, route_state.ADNetworkTable, ADRoutingTable);

		// For debugging.
		// printADNetworkTable();
		// printRoutingTable(route_state.myAD, ADRoutingTable);

		// Calculate next hop for routers
		std::map<std::string, NodeStateEntry>::iterator it1;
		// Iterate through ADs
		for (it1 = route_state.networkTable.begin(); it1 != route_state.networkTable.end(); it1++)
		{
			if ((it1->second.ad != route_state.myAD) || (it1->second.hid == "")) {
				// Don't calculate routes for external ADs
				continue;
			} else if (it1->second.hid.find(string("SID")) != string::npos) {
				// Don't calculate routes for SIDs
				continue;
			}
			std::map<std::string, RouteEntry> routingTable;

			// Calculate routing table for HIDs instead it1 AD
			populateRoutingTable(it1->second.hid, route_state.networkTable, routingTable);
			extractNeighborADs(routingTable);
			populateNeighboringADBorderRouterEntries(it1->second.hid, routingTable);
			populateADEntries(routingTable, ADRoutingTable);
			//printRoutingTable(it1->second.hid, routingTable);

			sendRoutingTable(it1->second.hid, routingTable);
		}

		route_state.calc_dijstra_ticks = 0;
	}

	return 1;
}

// Extract neighboring AD from the routing table
int extractNeighborADs(map<string, RouteEntry> routingTable)
{
	map<string, RouteEntry>::iterator it;
	for (it = routingTable.begin(); it != routingTable.end(); it++)
	{
		if (it->second.dest.find(string("AD:")) == 0) {
			// If AD, add to AD neighbor table
			// Update neighbor table
			NeighborEntry neighbor;
			neighbor.AD = it->second.dest;
			neighbor.HID = it->second.dest;
			neighbor.port = 0; 
			neighbor.cost = 1; // for now, same cost

			// Index by HID if neighbor in same domain or by AD otherwise
			route_state.ADNeighborTable[neighbor.AD] = neighbor;

			// Update network table
			std::string myAD = route_state.myAD;

			NodeStateEntry entry;
			entry.ad = myAD;
			entry.hid = myAD;
			entry.num_neighbors = route_state.ADNeighborTable.size();

			// Add neighbors to network table entry
			std::map<std::string, NeighborEntry>::iterator it;
			for (it = route_state.ADNeighborTable.begin(); it != route_state.ADNeighborTable.end(); it++)
				entry.neighbor_list.push_back(it->second);

			route_state.ADNetworkTable[myAD] = entry;
		}
	}
	return 1;
}

void populateNeighboringADBorderRouterEntries(string currHID, std::map<std::string, RouteEntry> &routingTable)
{
	vector<NeighborEntry> currNeighborTable = route_state.networkTable[currHID].neighbor_list;

	vector<NeighborEntry>::iterator it;
	for (it = currNeighborTable.begin(); it != currNeighborTable.end(); it++) { 
		if (it->AD != route_state.myAD) {
			// Add HID of border routers of neighboring ADs into routing table
			string neighborHID = it->HID;
			RouteEntry &entry = routingTable[neighborHID];
			entry.dest = neighborHID;
			entry.nextHop = neighborHID;
			entry.port = it->port;
			//entry.flags = 0;
		}
	}
}

void populateADEntries(std::map<std::string, RouteEntry> &routingTable, std::map<std::string, RouteEntry> ADRoutingTable)
{
	std::map<std::string, RouteEntry>::iterator it1;  // Iter for route table
	
	for (it1 = ADRoutingTable.begin(); it1 != ADRoutingTable.end(); it1++) {
		string destAD = it1->second.dest;
		string nextHopAD = it1->second.nextHop;

		RouteEntry &entry = routingTable[destAD];
		entry.dest = destAD;
		entry.nextHop = routingTable[nextHopAD].nextHop;
		entry.port = routingTable[nextHopAD].port;
		entry.flags = routingTable[nextHopAD].flags;
	}
}

// Run Dijkstra shortest path algorithm, and populate the next hops.
// This code is hacky to support AD and HID. This can be rewritten better.
void populateRoutingTable(std::string srcHID, std::map<std::string, NodeStateEntry> &networkTable, std::map<std::string, RouteEntry> &routingTable)
{
	std::map<std::string, NodeStateEntry>::iterator it1;  // Iter for network table
	std::vector<NeighborEntry>::iterator it2;             // Iter for neighbor list

	map<std::string, NodeStateEntry> unvisited;  // Set of unvisited nodes

	routingTable.clear();

	// Filter out anomalies
	//@ (When do these appear? Should they not be introduced in the first place? How about SIDs?)	
	for (it1 = networkTable.begin(); it1 != networkTable.end(); it1++) {
		if (it1->second.num_neighbors == 0 || it1->second.ad.empty() || it1->second.hid.empty()) {
			networkTable.erase(it1);
		}
	}

	unvisited = networkTable;

	// Initialize Dijkstra variables for all nodes
	for (it1=networkTable.begin(); it1 != networkTable.end(); it1++) {
		it1->second.checked = false;
		it1->second.cost = 10000000;
	}

	string currXID;

	// Visit root node (srcHID)
	unvisited.erase(srcHID);
	networkTable[srcHID].checked = true;
	networkTable[srcHID].cost = 0;

	// Process neighboring nodes of root node
	for (it2 = networkTable[srcHID].neighbor_list.begin(); it2 < networkTable[srcHID].neighbor_list.end(); it2++) {
		currXID = (it2->AD == route_state.myAD) ? it2->HID : it2->AD;

		if (networkTable.find(currXID) != networkTable.end()) {
			networkTable[currXID].cost = it2->cost;
			networkTable[currXID].prevNode = srcHID;
		}
		else {
			// We have an endhost
			NeighborEntry neighbor;
			neighbor.AD = route_state.myAD;
			neighbor.HID = srcHID;
			neighbor.port = 0; // Endhost only has one port
			neighbor.cost = 1;

			NodeStateEntry entry;
			entry.ad = it2->AD;
			entry.hid = it2->HID;
			entry.num_neighbors = 1;
			entry.neighbor_list.push_back(neighbor);
			entry.cost = neighbor.cost;
			entry.prevNode = neighbor.HID;

			networkTable[currXID] = entry;
		}
	}

	// Loop until all nodes have been visited
	while (!unvisited.empty()) {
		int minCost = 10000000;
		string selectedHID;
		// Select unvisited node with min cost
		for (it1=unvisited.begin(); it1 != unvisited.end(); it1++) {
			currXID = (it1->second.ad == route_state.myAD) ? it1->second.hid : it1->second.ad;
			if (networkTable[currXID].cost < minCost) {
				minCost = networkTable[currXID].cost;
				selectedHID = currXID;
			}
		}
		if(selectedHID.empty()) {
			// Rest of the nodes cannot be reached from the visited set
			return;
		}

		// Remove selected node from unvisited set
		unvisited.erase(selectedHID);
		networkTable[selectedHID].checked = true;

		// Process all unvisited neighbors of selected node
		for (it2 = networkTable[selectedHID].neighbor_list.begin(); it2 != networkTable[selectedHID].neighbor_list.end(); it2++) {
			currXID = (it2->AD == route_state.myAD) ? it2->HID : it2->AD;
			if (networkTable[currXID].checked != true) {
				if (networkTable[currXID].cost > networkTable[selectedHID].cost + 1) {
					//@ Why add 1 to cost instead of using link cost from neighbor_list?
					networkTable[currXID].cost = networkTable[selectedHID].cost + 1;
					networkTable[currXID].prevNode = selectedHID;
				}
			}
		}
	}

	// For each destination ID, find the next hop ID and port by going backwards along the Dijkstra graph
	string tempHID1;			// ID of destination in srcHID's routing table
	string tempHID2;			// ID of node currently being processed
	string tempNextHopHID2;		// HID of next hop to reach destID from srcHID
	int hop_count;

	for (it1 = networkTable.begin(); it1 != networkTable.end(); it1++) {
		tempHID1 = (it1->second.ad == route_state.myAD) ? it1->second.hid : it1->second.ad;
		if (tempHID1.find(string("SID")) != string::npos) {
			// Skip SIDs on first pass
			continue;
		}
		if (srcHID.compare(tempHID1) != 0) {
			tempHID2 = tempHID1;
			tempNextHopHID2 = it1->second.hid;
			hop_count = 0;
			while (networkTable[tempHID2].prevNode.compare(srcHID)!=0 && hop_count < MAX_HOP_COUNT) {
				tempHID2 = networkTable[tempHID2].prevNode;
				tempNextHopHID2 = networkTable[tempHID2].hid;
				hop_count++;
			}
			if (hop_count < MAX_HOP_COUNT) {
				routingTable[tempHID1].dest = tempHID1;
				routingTable[tempHID1].nextHop = tempNextHopHID2;
				
				// Find port of next hop
				for (it2 = networkTable[srcHID].neighbor_list.begin(); it2 < networkTable[srcHID].neighbor_list.end(); it2++) {
					if (((it2->AD == route_state.myAD) ? it2->HID : it2->AD) == tempHID2) {
						routingTable[tempHID1].port = it2->port;
					}
				}
			}
		}
	}

	for (it1 = networkTable.begin(); it1 != networkTable.end(); it1++) {
		tempHID1 = (it1->second.ad == route_state.myAD) ? it1->second.hid : it1->second.ad;
		if (tempHID1.find(string("SID")) == string::npos) {
			// Process SIDs on second pass 
			continue;
		}
		if (srcHID.compare(tempHID1) != 0) {
			tempHID2 = tempHID1;
			tempNextHopHID2 = it1->second.hid;
			hop_count = 0;
			while (networkTable[tempHID2].prevNode.compare(srcHID)!=0 && hop_count < MAX_HOP_COUNT) {
				tempHID2 = networkTable[tempHID2].prevNode;
				tempNextHopHID2 = networkTable[tempHID2].hid;
				hop_count++;
			}
			if (hop_count < MAX_HOP_COUNT) {
				routingTable[tempHID1].dest = tempHID1;
				
				// Find port of next hop
				for (it2 = networkTable[srcHID].neighbor_list.begin(); it2 < networkTable[srcHID].neighbor_list.end(); it2++) {
					if (((it2->AD == route_state.myAD) ? it2->HID : it2->AD) == tempHID2) {
						routingTable[tempHID1].port = it2->port;
					}
				}
				
				// Dest is SID, so we search existing ports for entry with same port and HID as next hop
				bool entryFound = false;
				map<string, RouteEntry>::iterator it3;
				for (it3 = routingTable.begin(); it3 != routingTable.end(); it3++) {
					if (it3->second.port == routingTable[tempHID1].port && it3->second.nextHop.find(string("HID")) != string::npos) {
						routingTable[tempHID1].nextHop = it3->second.nextHop;
						entryFound = true;
						break;
					}
				}
				if (!entryFound) {
					// Delete SID entry from routingTable
					routingTable.erase(tempHID1);
				}
			}
		}
	}
	//printRoutingTable(srcHID, routingTable);
}

void printRoutingTable(std::string srcHID, std::map<std::string, RouteEntry> &routingTable)
{
	syslog(LOG_INFO, "Routing table for %s", srcHID.c_str());
	map<std::string, RouteEntry>::iterator it1;
	for ( it1=routingTable.begin() ; it1 != routingTable.end(); it1++ ) {
		syslog(LOG_INFO, "Dest=%s, NextHop=%s, Port=%d, Flags=%u", (it1->second.dest).c_str(), (it1->second.nextHop).c_str(), (it1->second.port), (it1->second.flags) );
	}
}

void printADNetworkTable()
{
	syslog(LOG_INFO, "Network table for %s:", route_state.myAD);
	std::map<std::string, NodeStateEntry>::iterator it;
	for (it = route_state.ADNetworkTable.begin();
		   	it != route_state.ADNetworkTable.end(); it++) {
		syslog(LOG_INFO, "%s", it->first.c_str());
		for (size_t i = 0; i < it->second.neighbor_list.size(); i++) {
			syslog(LOG_INFO, "neighbor[%d]: %s", (int) i,
					it->second.neighbor_list[i].AD.c_str());
		}
	}

}

void initRouteState()
{
	// make the dest DAG (broadcast to other routers)
	Graph g = Node() * Node(BHID) * Node(SID_XROUTE);
	g.fill_sockaddr(&route_state.ddag);

	syslog(LOG_INFO, "xroute Broadcast DAG: %s", g.dag_string().c_str());

	// read the localhost AD and HID
	if ( XreadLocalHostAddr(route_state.sock, route_state.myAD, MAX_XID_SIZE, route_state.myHID, MAX_XID_SIZE, route_state.my4ID, MAX_XID_SIZE) < 0 ) {
		syslog(LOG_ALERT, "Unable to read local XIA address");
		exit(-1);
	}

printf("xcontrold calling getaddrinfo\n");
	// make the src DAG (the one the routing process listens on)
	struct addrinfo *ai;
	if (Xgetaddrinfo(NULL, SID_XROUTE, NULL, &ai) != 0) {
		syslog(LOG_ALERT, "unable to create source DAG");
		exit(-1);
	}
	memcpy(&route_state.sdag, ai->ai_addr, sizeof(sockaddr_x));

	route_state.num_neighbors = 0; // number of neighbor routers
	route_state.lsa_seq = 0;	// LSA sequence number of this router
	route_state.hello_seq = 0;  // hello seq number of this router
	route_state.hello_lsa_ratio = (int32_t) ceil(AD_LSA_INTERVAL/HELLO_INTERVAL);
	route_state.calc_dijstra_ticks = 0;

	route_state.ctl_seq = 0;	// LSA sequence number of this router

	route_state.dual_router_AD = "NULL";
	// mark if this is a dual XIA-IPv4 router
	if( XisDualStackRouter(route_state.sock) == 1 ) {
		route_state.dual_router = 1;
		syslog(LOG_DEBUG, "configured as a dual-stack router");
	} else {
		route_state.dual_router = 0;
	}
}

void help(const char *name)
{
	printf("\nusage: %s [-l level] [-v] [-c config] [-h hostname]\n", name);
	printf("where:\n");
	printf(" -l level    : syslog logging level 0 = LOG_EMERG ... 7 = LOG_DEBUG (default=3:LOG_ERR)");
	printf(" -v          : log to the console as well as syslog");
	printf(" -h hostname : click device name (default=controller0)\n");
	printf("\n");
	exit(0);
}

void config(int argc, char** argv)
{
	int c;
	unsigned level = 3;
	int verbose = 0;

	opterr = 0;

	while ((c = getopt(argc, argv, "h:l:v")) != -1) {
		switch (c) {
			case 'h':
				hostname = strdup(optarg);
				break;
			case 'l':
				level = MIN(atoi(optarg), LOG_DEBUG);
				break;
			case 'v':
				verbose = LOG_PERROR;
				break;
			case '?':
			default:
				// Help Me!
				help(basename(argv[0]));
				break;
		}
	}

	if (!hostname)
		hostname = strdup(DEFAULT_NAME);

	// load the config setting for this hostname
	set_conf("xsockconf.ini", hostname);

	// note: ident must exist for the life of the app
	ident = (char *)calloc(strlen(hostname) + strlen (APPNAME) + 4, 1);
	sprintf(ident, "%s:%s", APPNAME, hostname);
	openlog(ident, LOG_CONS|LOG_NDELAY|LOG_LOCAL4|verbose, LOG_LOCAL4);
	setlogmask(LOG_UPTO(level));
}

int main(int argc, char *argv[])
{
	int rc;
	int selectRetVal, n;
	//size_t found, start;
	socklen_t dlen;
	char recv_message[10240];
	sockaddr_x theirDAG;
	fd_set socks;
	struct timeval timeoutval;
	vector<string> routers;

	config(argc, argv);
	syslog(LOG_NOTICE, "%s started on %s", APPNAME, hostname);

    // connect to the click route engine
	if ((rc = xr.connect()) != 0) {
		syslog(LOG_ALERT, "unable to connect to click (%d)", rc);
		return -1;
	}

	xr.setRouter(hostname);

	// open socket for route process
	route_state.sock=Xsocket(AF_XIA, SOCK_DGRAM, 0);
	if (route_state.sock < 0) {
		syslog(LOG_ALERT, "Unable to create a socket");
		exit(-1);
	}

	// initialize the route states (e.g., set HELLO/LSA timer, etc)
	initRouteState();

	// bind to the src DAG
	if (Xbind(route_state.sock, (struct sockaddr*)&route_state.sdag, sizeof(sockaddr_x)) < 0) {
		Graph g(&route_state.sdag);
		syslog(LOG_ALERT, "unable to bind to local DAG : %s", g.dag_string().c_str());
		Xclose(route_state.sock);
		exit(-1);
	}

	// open socket for controller service
	int32_t tempSock = Xsocket(AF_XIA, SOCK_DGRAM, 0);
	if (tempSock < 0) {
		syslog(LOG_ALERT, "Unable to create a socket");
		exit(-1);
	}

	// bind to the controller service
	struct addrinfo *ai;
	sockaddr_x tempDAG;
	if (Xgetaddrinfo(NULL, SID_XCONTROL, NULL, &ai) != 0) {
		syslog(LOG_ALERT, "unable to bind controller service");
		exit(-1);
	}
	memcpy(&tempDAG, ai->ai_addr, sizeof(sockaddr_x));

	if (Xbind(tempSock, (struct sockaddr*)&tempDAG, sizeof(sockaddr_x)) < 0) {
		Graph g(&tempDAG);
		syslog(LOG_ALERT, "unable to bind to local DAG : %s", g.dag_string().c_str());
		perror("bind");
		Xclose(tempSock);
		exit(-1);
	}

	int sock;
	time_t last_purge = time(NULL);
	int iteration = 0;
	while (1) {
		iteration++;
		FD_ZERO(&socks);
		FD_SET(route_state.sock, &socks);
		FD_SET(tempSock, &socks);
		timeoutval.tv_sec = 0;
		timeoutval.tv_usec = 2000; // every 0.002 sec, check if any received packets

		int32_t highSock = max(route_state.sock, tempSock);
		selectRetVal = Xselect(highSock+1, &socks, NULL, NULL, &timeoutval);
		if (selectRetVal > 0) {
			// receiving a Hello or LSA packet
			memset(&recv_message[0], 0, sizeof(recv_message));
			dlen = sizeof(sockaddr_x);
			if (FD_ISSET(route_state.sock, &socks)) {
				sock = route_state.sock;
			} else if (FD_ISSET(tempSock, &socks)) {
				sock = tempSock;
			} else {
				continue;
			}
			n = Xrecvfrom(sock, recv_message, 10240, 0, (struct sockaddr*)&theirDAG, &dlen);
			if (n < 0) {
				perror("recvfrom");
			}

			string msg = recv_message;
            processMsg(msg);
		
		} else if (selectRetVal < 0) {
			syslog(LOG_WARNING, "ERROR: Xselect returned %d", selectRetVal);
		}

		// Send HELLO every 100 ms
		if((iteration % HELLO_ITERS) == 0) {
			// Except when we are sending an LSA
			if((iteration % LSA_ITERS) != 0) {
				route_state.hello_seq++;
				if(sendHello()) {
					syslog(LOG_WARNING, "ERROR: Failed sending hello");
				}
			}
		}
		// Send an LSA every 400 ms
		if((iteration % LSA_ITERS) == 0) {
			route_state.hello_seq = 0;
			if(sendInterdomainLSA()) {
				syslog(LOG_WARNING, "ERROR: Failed sending LSA");
			}
		}

		// FIXME: is this needed on the controller???
		time_t now = time(NULL);
		if (now - last_purge >= EXPIRE_TIME)
		{
			last_purge = now;
			fprintf(stderr, "checking entry\n");
			map<string, time_t>::iterator iter;

			for (iter = timeStamp.begin(); iter != timeStamp.end(); iter++)	
			{
				if (now - iter->second >= EXPIRE_TIME){
					xr.delRoute(iter->first);
					timeStamp.erase(iter);
					syslog(LOG_INFO, "purging host route for : %s", iter->first.c_str());
				}
			}
		}
	}

	return 0;
}
