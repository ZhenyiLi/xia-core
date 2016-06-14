#include "stage_utils.h"
int main(int argc, char **argv){
	if(argc != 2){
		printf("Usage: testXping host\n");
		return 0;
	}
	int rtt = getRTT(argv[1]);
	printf("RTT to %s is %d ms\n", argv[1], rtt);
	return 0;
}