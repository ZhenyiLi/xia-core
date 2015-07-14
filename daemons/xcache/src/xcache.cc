#include <stdio.h>
#include <iostream>
#include "controller.h"
#include "clicknet/xia.h"
#include <getopt.h>

static char version[] = "0.1";

static void display_version(void)
{
	printf("XIA Cache Daemon version %s\n", version);
}

static void usage(char *argv[])
{
	display_version();
	printf("Usage: %s [OPTIONS]\n", argv[0]);
	printf("  -h, --host=HOSTNAME       Specify the host on which this daemon should run.\n");
	printf("                            Default host = \"router0\"\n");
	printf("  -v, --version             Displays xcache version.\n");
	printf("  --help                    Displays this help.\n");
}

static void sethostname(struct xcache_conf *conf, char *hostname)
{
	strcpy(conf->hostname, hostname);
}

int main(int argc, char *argv[])
{
	int c;
	XcacheController ctrl;
	struct xcache_conf conf;

	strcpy(conf.hostname, "router0");

	struct option options[] = {
		{"host", required_argument, 0, 0},
		{"help", no_argument, 0, 0},
		{"version", no_argument, 0, 0},
		{0, 0, 0, 0},
	};

	while(1) {
		int option_index = 0;

		c = getopt_long(argc, argv, "vh:", options, &option_index);
		if(c == -1)
			break;

		switch(c) {
		case 0:
			/* long option passed */
			if(!strcmp(options[option_index].name, "host")) {
				sethostname(&conf, optarg);
			} else if(!strcmp(options[option_index].name, "help")) {
				usage(argv);
				return 0;
			} else if(!strcmp(options[option_index].name, "version")) {
				display_version();
				return 0;
			} else {
				usage(argv);
				return 1;
			}
			break;
			
		case 'h':
			sethostname(&conf, optarg);
			break;
		case 'v':
			display_version();
			return 0;
		default:
			usage(argv);
			return 1;
			
		}
	}

	ctrl.setConf(&conf);
	ctrl.run();
	return 0;
}

bool operator<(const struct click_xia_xid& x, const struct click_xia_xid& y)
{
	if(x.type < y.type) {
		return true;
	} else if(x.type > y.type) {
		return false;
	}

	for(int i = 0; i < CLICK_XIA_XID_ID_LEN; i++) {
		if(x.id[i] < y.id[i])
			return true;
		else if(x.id[i] > y.id[i])
			return false;
	}

	return false;
}

