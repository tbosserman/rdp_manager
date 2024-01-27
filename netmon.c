#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <gtk/gtk.h>
#include "ping_dns.h"

#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif

#define DNS_OK		1
#define ROUTE_OK	2
#define HAVE_IP		4
#define PING_OK		8
#define ALL_OK		(DNS_OK | ROUTE_OK | HAVE_IP | PING_OK)

#define all_ok(flags)	(((flags) & ALL_OK) == ALL_OK)
#define dns_ok(flags)	(((flags) & DNS_OK) != 0)
#define route_ok(flags)	(((flags) & ROUTE_OK) != 0)
#define have_ip(flags)	(((flags) & HAVE_IP) != 0)

extern int	alltrim(char *s);
extern void	mylog(char *fmt, ...);
extern void	alert(const char *fmt, ...);

struct s_info {
    char	interface[16];
    char	gateway[16];
    char	ipaddr[16];
    int		flags;
};
typedef struct s_info t_info;

t_info		netmon_info[2];
int		cur_info_index = -1;

/************************************************************************
 ********************         DEFAULT_ROUTE          ********************
 ************************************************************************/
int
default_route(t_info *info)
{
    FILE	*fp;
    char	line[256];

    /* NOTE: look into using /proc/net/route instead of the "ip" command. */
    if ((fp = popen("/bin/ip -4 -o route show default", "r")) == NULL)
	return(-1);
    line[sizeof(line)-1] = '\0';
    if (fgets(line, sizeof(line)-1, fp) == NULL || strlen(line) == 0)
    {
	pclose(fp);
	return(-1);
    }
    // default via 192.168.100.1 dev eth0 proto dhcp src 192.168.100.174 metric 100
    (void)strtok(line, " \t"); /* the word "default" */
    (void)strtok(NULL, " \t"); /* the word "via" */
    strncpy(info->gateway, strtok(NULL, " \t"), sizeof(info->gateway)-1);
    (void)strtok(NULL, " \t"); /* the word "dev" */
    strncpy(info->interface, strtok(NULL, " \t"), sizeof(info->interface)-1);

    pclose(fp);
    return(0);
}

/************************************************************************
 ********************           MY_IPADDR            ********************
 ************************************************************************/
int
my_ipaddr(t_info *info)
{
    FILE	*fp;
    char	cmd[256], line[256], *p;

    /* NOTE: look into using getifaddrs() instead of the "ip" command */
    cmd[sizeof(cmd)-1] = '\0';
    snprintf(cmd, sizeof(cmd)-1, "/bin/ip -4 -br addr show dev %s",
	info->interface);
    if ((fp = popen(cmd, "r")) == NULL)
	return(-1);

    line[sizeof(line)-1] = '\0';
    p = fgets(line, sizeof(line)-1, fp);
    pclose(fp);
    if (p == NULL || strlen(line) < 2)
	return(-1);
    line[strlen(line)-1] = '\0';
    alltrim(line);

    // wlan0            UP             192.168.100.175/24 
    if ((p = strrchr(line, ' ')) == NULL)
	return(-1);
    strncpy(info->ipaddr, p+1, sizeof(info->ipaddr)-1);

    return(0);
}

/************************************************************************
 ********************           CHECK_DNS            ********************
 ************************************************************************/
int
check_dns(void)
{
    int			code;
    struct addrinfo	hints, *ai;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;
    hints.ai_family = PF_UNSPEC;
    code = getaddrinfo("google.com", NULL, &hints, &ai);
    return(code);
}

/************************************************************************
 ********************           ALL_TESTS            ********************
 ************************************************************************/
void
all_tests(t_info *info)
{
    int			flags;
    char		buf[1024];
    struct servent	svc, *resultp;

    memset(info, 0, sizeof(t_info));
    if (default_route(info) == 0)
    {
	info->flags |= ROUTE_OK;
	if (my_ipaddr(info) == 0)
	    info->flags |= HAVE_IP;
    }
    if (check_dns() == 0)
	info->flags |= DNS_OK;

    flags = (ROUTE_OK | HAVE_IP);
    if ((info->flags & flags) != flags)
	return;

    if (getservbyname_r("domain", "udp", &svc, buf, sizeof(buf), &resultp))
	return;

    if (ping_dns("8.8.8.8", ntohs(svc.s_port)) == SUCCESS)
	info->flags |= PING_OK;
}

/************************************************************************
 ********************             NETMON             ********************
 ************************************************************************/
gboolean
netmon(gpointer user_data)
{
    int		cur, prev;
    t_info	*curp, *prevp;

    prev = cur_info_index;
    cur = (prev < 0) ? 0 : !prev;
    curp = &netmon_info[cur];
    prevp = (prev < 0) ? NULL : &netmon_info[prev];
    cur_info_index = cur;
    all_tests(curp);

    if (all_ok(curp->flags))
    {
	if (prevp && !all_ok(prevp->flags))
	{
	    mylog("Networking OK\n");
	    alert("Internet connectivity has been restored");
	}
	return(G_SOURCE_CONTINUE);
    }

    if (!route_ok(curp->flags))
    {
	if (prevp == NULL || route_ok(prevp->flags))
	   mylog("No default route\n");
    }
    else if (!have_ip(curp->flags))
    {
	if (prevp == NULL || have_ip(prevp->flags))
	    mylog("No IP address assigned\n");
    }
    if (!dns_ok(curp->flags))
    {
	if (prevp == NULL || dns_ok(prevp->flags))
	    mylog("DNS not working\n");
    }

    if (!all_ok(curp->flags) && (!prevp || all_ok(prevp->flags)))
	alert("You don't appear to be connected to the internet.\n"
	      "Make sure you are either connected to WiFi or your\n"
	      "ethernet cable is plugged in.  Flags=%08X (hex)", curp->flags);

    return(G_SOURCE_CONTINUE);
}
