/*
** Copyright 2013 Carnegie Mellon University / ETH Zurich
** 
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
** 
** http://www.apache.org/licenses/LICENSE-2.0 
** 
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/glue.hh>
#include <click/straccum.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <click/xiaheader.hh>
#include <click/xiacontentheader.hh>
#include <click/xiatransportheader.hh>
#include <click/xid.hh>
#include <click/standard/xiaxidinfo.hh>
#include "xiatransport.hh"
#include "xiaxidroutetable.hh"
#include "xtransport.hh"

#define SID_XROUTE  "SID:1110000000000000000000000000000000001112"


/*change this to corresponding header*/
#include "scioncertserver_core.hh"


CLICK_DECLS

int SCIONCertServerCore::configure(Vector<String> &conf, ErrorHandler *errh){

	if(cp_va_kparse(conf, this, errh,
	"AD", cpkM, cpString, &m_AD,
	"HID", cpkM, cpString, &m_HID,
	"AID", cpkM, cpUnsigned64, &m_uAid,
	"CONFIG_FILE", cpkM, cpString, &m_sConfigFile,
	"TOPOLOGY_FILE", cpkM, cpString, &m_sTopologyFile, 
	"ROT", cpkM, cpString, &m_sROTFile,
	"Cert", cpkM, cpString, &m_csCert, // tempral store in click file
	"PrivateKey", cpkM, cpString, &m_csPrvKey, // tempral store in click file
	cpEnd) <0) {
		printf("ERR: click configuration fail at SCIONCertServerCore.\n");
		printf("Fatal error, Exit SCION Network.\n");
		exit(-1);
	}
	
	XIAXIDInfo xiaxidinfo;
    struct click_xia_xid store;
    XID xid = xid;

    xiaxidinfo.query_xid(m_AD, &store, this);
    xid = store;
    m_AD = xid.unparse();

    xiaxidinfo.query_xid(m_HID, &store, this);
    xid = store;
    m_HID = xid.unparse();
	
	
	return 0;
}

int SCIONCertServerCore::initialize(ErrorHandler* errh){
	
	// initialization task
	// task 1: parse config file
	Config config;
	config.parseConfigFile((char*)m_sConfigFile.c_str());
	// get ADAID, AID, TDID
	m_uAdAid = config.getAdAid();
	m_uTdAid = config.getTdAid();
	m_iLogLevel =config.getLogLevel();
	config.getCSLogFilename(m_csLogFile);

	// setup scionPrinter for message logging
	scionPrinter = new SCIONPrint(m_iLogLevel, m_csLogFile);
	scionPrinter->printLog(IL, "TDC CS INIT.\n");
	scionPrinter->printLog(IL, "ADAID = %llu, TDID = %llu.\n", m_uAdAid, m_uTdAid);

	// task 2: parse ROT (root of trust) file
	parseROT();
	scionPrinter->printLog(IL, "Parse/Verify ROT Done.\n");
	
	// task 3: parse topology file
	//parseTopology();
	//constructIfid2AddrMap();
	//initializeOutputPort();
	scionPrinter->printLog(IL, "Parse Topology Done.\n");

	ScheduleInfo::initialize_task(this, &_task, errh);
	return 0;
}

void SCIONCertServerCore::parseROT(){
	ROTParser parser;
	if(parser.loadROTFile(m_sROTFile.c_str())!=ROTParseNoError){
    	printf("ERR: ROT File missing at TDC CS.\n");
		printf("Fatal error, Exit SCION Network.\n");
		exit(-1);
	}
	scionPrinter->printLog(IL, "Load ROT OK.\n");
	if(parser.parse(rot)!=ROTParseNoError){
    	printf("ERR: ROT File parsing error at TDC CS.\n");
		printf("Fatal error, Exit SCION Network.");
		exit(-1);
	}
	scionPrinter->printLog(IL, "Parse ROT OK.\n");
	if(parser.verifyROT(rot)!=ROTParseNoError) {
		printf("ERR: ROT File parsing error at TDC CS.\n");
		printf("Fatal error, Exit SCION Network.\n");
		exit(-1);
	}
	scionPrinter->printLog(IL, "Verify ROT OK.\n");
	// prepare ROT for delivery
	FILE* rotFile = fopen(m_sROTFile.c_str(), "r");
	fseek(rotFile, 0, SEEK_END);
	curROTLen = ftell(rotFile);
	rewind(rotFile);
	curROTRaw = (char*)malloc(curROTLen*sizeof(char));

	char buffer[128];
	int offset =0;
	
	while(!feof(rotFile)){
		memset(buffer, 0, 128);
		fgets(buffer, 128, rotFile);
		int buffLen = strlen(buffer);
		memcpy(curROTRaw+offset, buffer, buffLen);
		offset+=buffLen;
	}
	fclose(rotFile);
	scionPrinter->printLog(IL, "Stored Verified ROT for further delivery.\n");
}

void SCIONCertServerCore::parseTopology(){
	TopoParser parser;
	parser.loadTopoFile(m_sTopologyFile.c_str()); 
    parser.parseServers(m_servers);
    // parser.parseRouters(m_routers);
    // parser.parseEgressIngressPairs(m_routepairs);
}

void SCIONCertServerCore::push(int port, Packet *p)
{
    TransportHeader thdr(p);
    
    uint8_t * s_pkt = (uint8_t *) thdr.payload();
    uint16_t type = SPH::getType(s_pkt);
	uint16_t packetLength = SPH::getTotalLen(s_pkt);
    uint8_t packet[packetLength];

	memset(packet, 0, packetLength);
    memcpy(packet, s_pkt, packetLength);
    p->kill();
    
    switch(type){
    
		case ROT_REQ_LOCAL:{

			scionPrinter->printLog(IH, "TDC CS received ROT request from BS\n");
			
			uint8_t hdrLen = COMMON_HEADER_SIZE+SCION_ADDR_SIZE*2;
			uint16_t totalLen = hdrLen + curROTLen;
			uint8_t rotPacket[totalLen];
			memset(rotPacket, 0, totalLen);
			// set length and type
			SPH::setHdrLen(rotPacket, hdrLen);
			SPH::setType(rotPacket, ROT_REP_LOCAL); 
			SPH::setTotalLen(rotPacket, totalLen);
			// fill address
			HostAddr srcAddr = HostAddr(HOST_ADDR_SCION, (uint64_t)strtoull((const char*)m_AD.c_str(), NULL, 10));
			scionPrinter->printLog(IH, (char *)"Local BServer: %s\n", m_servers.find(BeaconServer)->second.HID);
			HostAddr dstAddr = HostAddr(HOST_ADDR_SCION, (uint64_t)strtoull((const char*)m_servers.find(BeaconServer)->second.HID, NULL, 10));
			
			SPH::setSrcAddr(rotPacket, srcAddr);
			SPH::setDstAddr(rotPacket, dstAddr);
			
			string dest = "RE ";
			dest.append(BHID);
			dest.append(" ");
			dest.append(m_AD.c_str());
			dest.append(" ");
			dest.append("HID:");
			dest.append((const char*)m_servers.find(BeaconServer)->second.HID);
			
			memcpy(rotPacket+hdrLen, curROTRaw, curROTLen);
			sendPacket(rotPacket, totalLen, dest);
			} 
			break;

		case CERT_REQ: { //send chain

			uint16_t hops = 0;
			uint16_t hopPtr = 0;
			certReq* req= (certReq*)(packet+SCION_HEADER_SIZE+hops*PATH_HOP_SIZE);      
			certReq newReq = certReq();
			newReq.numTargets=0;

			for(int i=0;i<req->numTargets;i++){
				uint64_t target = req->targets[i];
				uint8_t certFile[MAX_FILE_LEN];
				printf("Cert Request Target = %llu in %llu\n", target, m_uAdAid);
				getCertFile(certFile, target);

				FILE* cFile;
				if((cFile=fopen((const char*)certFile,"r"))==NULL){
					if(certRequests.find(target)==certRequests.end()){
						printf("Fatal Error: TDC CS cannot found a Cert for CERT_REQ packet.\n");
						printf("It should exit the SCION network here.\n");
						exit(-1);
					}
				}else{
					uint8_t downPath[hops*PATH_HOP_SIZE];
					reversePath(packet+SCION_HEADER_SIZE, downPath, hops);
					printf("TDC CS: certificate found sending down stream\n");
					fseek(cFile,0,SEEK_END);
					long cSize = ftell(cFile);
					rewind(cFile);

					uint16_t packetLength = SCION_HEADER_SIZE+CERT_INFO_SIZE+cSize+hops*PATH_HOP_SIZE;
					uint8_t buffer[packetLength];
					memcpy(buffer+SCION_HEADER_SIZE, downPath, hops*PATH_HOP_SIZE);
					SPH::setType(buffer, CERT_REP); 
					SPH::setTotalLen(buffer, packetLength);
					SPH::setDownpathFlag(buffer);
					pathHop *hop = (pathHop*)(buffer+SCION_HEADER_SIZE+(hops-hopPtr-1)*PATH_HOP_SIZE);
					certInfo* info = (certInfo*)(buffer+SCION_HEADER_SIZE+hops*PATH_HOP_SIZE);
					info->target= target;
					info->length = cSize;
					fread(buffer+SCION_HEADER_SIZE+CERT_INFO_SIZE+hops*PATH_HOP_SIZE,1,cSize,cFile);
					fclose(cFile);
					
					string dest = "RE ";
					dest.append(BHID);
					dest.append(" ");
					dest.append(m_AD.c_str());
					dest.append(" ");
					dest.append("HID:");
					dest.append((const char*)m_servers.find(BeaconServer)->second.HID);
					
					sendPacket(buffer, packetLength, dest);
				}
			}// end for
			} 
			break;
			
		case ROT_REQ:
			processROTRequest(packet);
			break;
			
		default:
			break;
		}
}

/*
	SLN:
	process ROT request from a child AD
*/
void
SCIONCertServerCore::processROTRequest(uint8_t * packet) {

	//1. return ROT if the requested version is available
	//2. forward it to the upstream otherwise (SL: I think this should not happen) 
	#ifdef _SL_DEBUG_CS
	printf("CS (%llu:%llu): Received ROT_REQ request from downstream CS.\n", m_uAdAid, m_uAid);
	#endif

	uint16_t hops = 0; 
	uint16_t hdrLen = SPH::getHdrLen(packet);
	specialOpaqueField* sOF = (specialOpaqueField *)SPH::getFirstOF(packet);
	hops = sOF->hops;

	//ROTRequest* req= (ROTRequest*)SPH::getData(packet);

	uint8_t downPath[(hops+1)*OPAQUE_FIELD_SIZE];
	reversePath(SPH::getFirstOF(packet), downPath, hops);
	
	//1. Set header
	scionHeader hdr;

	hdr.src = HostAddr(HOST_ADDR_SCION, m_uAid);
	hdr.cmn.type = ROT_REP;
	hdr.cmn.hdrLen = COMMON_HEADER_SIZE+SCION_ADDR_SIZE*2+(hops+1)*OPAQUE_FIELD_SIZE;
	hdr.cmn.totalLen = hdr.cmn.hdrLen + sizeof(ROTRequest) + curROTLen;
	
	//2. Set Opaque Fields
	hdr.cmn.timestamp = COMMON_HEADER_SIZE+SCION_ADDR_SIZE*2;
	hdr.n_of =  hops + 1;
	hdr.p_of = downPath;
	
	uint16_t currOFPtr = SPH::getCurrOFPtr(packet);
	uint16_t offset = currOFPtr-SCION_ADDR_SIZE*2;
	hdr.cmn.currOF = SCION_ADDR_SIZE*2+(hops+1)*OPAQUE_FIELD_SIZE-offset;
	currOFPtr = hdr.cmn.currOF;

	opaqueField* of = (opaqueField*)(downPath+currOFPtr-SCION_ADDR_SIZE*2);
	hdr.dst = ifid2addr.find(of->egressIf)->second;

	#ifdef _SL_DEBUG_CS
	printf("CS(%llu:%llu): ROT_REP: currOFPtr = %d, n_of = %d\n", 
		m_uAdAid, m_uAid, currOFPtr, hdr.n_of);
	#endif
	
	uint8_t buffer[hdr.cmn.totalLen];
	SPH::setHeader(buffer,hdr);
	SPH::setDownpathFlag(buffer);
	
	//3. copy ROT
	ROTRequest * req = (ROTRequest*)(buffer+hdr.cmn.hdrLen);
	//copy the ROT request hdr, telling the recipient the request version is being returned
	memcpy(req, SPH::getData(packet), sizeof(ROTRequest));	
	//If the requested ROT version is higher than the currently available one,
	//it indicates something wrong happened (i.e., unverified PCB propagated to a customer AD,
	//implying bogus PCB injection.
	//For now, we assume currentVersion = previousVersion +1. Otherwise, all versions of ROT
	//starting from previousVersion+1 to currentVersion should be provided to the requester.
	if(req->currentVersion != rot.version)
		req->currentVersion = rot.version;
	#ifdef _SL_DEBUG_CS
	printf("CS (%llu:%llu): ROT_REP: req version = %d, send version = %d\n", 
		m_uAdAid, m_uAid, req->currentVersion, rot.version);
	#endif
	//copy the ROT
	memcpy(buffer+hdr.cmn.hdrLen+sizeof(ROTRequest), curROTRaw, curROTLen);	

	//4. Send ROT
	string dest = "RE ";
	dest.append(BHID);
	dest.append(" ");
	dest.append(m_AD.c_str());
	dest.append(" ");
	dest.append("HID:");
	dest.append((const char*)m_servers.find(BeaconServer)->second.HID);
					
	sendPacket(buffer, hdr.cmn.totalLen, dest);
}


/*
int SCIONCertServerCore::verifyCert(uint8_t* packet){

	// TODO: verify certs
	uint16_t hops = 0;
	certInfo* info = (certInfo*)(packet+SCION_HEADER_SIZE+hops*PATH_HOP_SIZE);
	uint64_t target = info->target;
	uint16_t length = info->length;
	char cFileName[MAX_FILE_LEN];

	// should be fixed here since no td and version numbers
	// file format: td#-ad#-#.crt
	sprintf(cFileName,"./TD1/TDC/AD%llu/certserver/certificates/td1-ad%llu-0.crt",m_uAdAid,target);
	printf("%s\n",cFileName);
	FILE* cFile = fopen(cFileName,"w");
	fwrite(packet+SCION_HEADER_SIZE+CERT_INFO_SIZE+hops*PATH_HOP_SIZE,1,length,cFile);
	fclose(cFile);

	return SCION_SUCCESS;
}
*/

void SCIONCertServerCore::sendPacket(uint8_t* data, uint16_t data_length, string dest) {

    string src = "RE ";
    src.append(m_AD.c_str());
    src.append(" ");
    src.append(m_HID.c_str());
    src.append(" ");
    src.append(SID_XROUTE);

    XIAPath src_path, dst_path;
	src_path.parse(src.c_str());
	dst_path.parse(dest.c_str());

    XIAHeaderEncap xiah;
    xiah.set_nxt(CLICK_XIA_NXT_TRN);
    xiah.set_last(LAST_NODE_DEFAULT);
    //xiah.set_hlim(hlim.get(_sport));
    xiah.set_src_path(src_path);
    xiah.set_dst_path(dst_path);

    WritablePacket *p = Packet::make(DEFAULT_HD_ROOM, data, data_length, DEFAULT_TL_ROOM);
    TransportHeaderEncap *thdr = TransportHeaderEncap::MakeDGRAMHeader(0); // length
	WritablePacket *q = thdr->encap(p);

    thdr->update();
    xiah.set_plen(data_length + thdr->hlen()); // XIA payload = transport header + transport-layer data

    q = xiah.encap(q, false);
	output(0).push(q);
}

void SCIONCertServerCore::getCertFile(uint8_t* fn, uint64_t target){
	sprintf((char*)fn,"./TD1/TDC/AD%llu/certserver/certificates/td%llu-ad%d-0.crt",m_uAdAid, m_uTdAid, target);
}

void SCIONCertServerCore::reversePath(uint8_t* path, uint8_t* output, uint8_t hops){
	uint16_t offset = (hops)*OPAQUE_FIELD_SIZE; 
	uint8_t* ptr = path+OPAQUE_FIELD_SIZE;
	memcpy(output, path, OPAQUE_FIELD_SIZE);
	opaqueField* hopPtr = (opaqueField*)ptr; 
	
	for(int i=0;i<hops;i++){
		memcpy(output+offset, ptr, OPAQUE_FIELD_SIZE);
		offset-=OPAQUE_FIELD_SIZE;
		ptr+=OPAQUE_FIELD_SIZE;
		hopPtr = (opaqueField*)ptr;
	}
}

void SCIONCertServerCore::sendROT(){
	uint16_t packetLen = curROTLen + SCION_HEADER_SIZE;
	uint8_t packet[packetLen];
	memcpy(packet+SCION_HEADER_SIZE,curROTRaw, curROTLen);
	SPH::setType(packet, ROT_REP_LOCAL); 
	SPH::setTotalLen(packet, packetLen);
	// sendPacket(packet, packetLen, 0);
}


CLICK_ENDDECLS
EXPORT_ELEMENT(SCIONCertServerCore)


