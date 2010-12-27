#ifndef CLICK_RPC_HH
#define CLICK_RPC_HH
#include <click/element.hh>
#include <click/string.hh>
//#define NUM_PORTS 10
CLICK_DECLS

/*
=c

RPC([LABEL, MAXLENGTH, I<keywords>])
*/

class rpc : public Element { public:

    rpc();
    ~rpc();
  
    const char *class_name() const		{ return "rpc"; }
  const char *port_count() const		{ return "1-10/1-10"; }  //? extend 10 to N
  const char *processing() const		{ return "a/h"; }  

  //void static_initialize();
  int initialize();
  int configure(Vector<String> &, ErrorHandler *);
    bool can_live_reconfigure() const		{ return true; }
    void add_handlers();
    void push(int, Packet *);
  //Packet *pull (int);
  //Packet *simple_action(Packet *);

 private:
    int states ; 
    char pktdata[][100];
    bool _active;
  

};

CLICK_ENDDECLS
#endif