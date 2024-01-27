#define SUCCESS			0
#define ERR_RES_NINIT		1
#define ERR_RES_NMKQUERY	2
#define ERR_SOCKET		3
#define ERR_BIND		4
#define ERR_INET_PTON		5
#define ERR_CONNECT		6
#define ERR_WRITE		7
#define ERR_POLL		8
#define ERR_READ		9
#define ERR_TIMEOUT		10

extern int ping_dns(char *host_ip, int port);
