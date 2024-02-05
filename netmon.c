#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if.h>
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
#define ping_ok(flags)	(((flags) & PING_OK) != 0)

#define ADDRLEN		16

extern int	alltrim(char *s);
extern void	mylog(char *fmt, ...);
extern void	alert(const char *fmt, ...);

struct s_info {
    char	interface[IFNAMSIZ];
    char	gateway[ADDRLEN];
    char	ipaddr[ADDRLEN];
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
    int		err, dest, gw;
    char	line[256], iface[IFNAMSIZ];
    struct in_addr	addr;

    info->interface[0] = info->gateway[0] = '\0';

    if ((fp = fopen("/proc/net/route", "r")) == NULL)
	return(-1);

    while (fgets(line, sizeof(line), fp) != NULL)
    {
	if (line[0] == 'I') /* skip the header line */
	    continue;
	sscanf(line, "%s %X %X ", iface, &dest, &gw);
	if (dest == 0)
	    break;
    }
    err = ferror(fp);
    fclose(fp);
    if (err || dest != 0 || gw == 0)
	return(-1);

    addr.s_addr = gw;
    inet_ntop(AF_INET, &addr, info->gateway, ADDRLEN);
    strncpy(info->interface, iface, ADDRLEN);

    return(0);
}

/************************************************************************
 ********************           MY_IPADDR            ********************
 ************************************************************************/
int
my_ipaddr(t_info *info)
{
    int			have_route;
    struct ifaddrs	*ifaddrs, *ifp;
    struct sockaddr_in	*addrp;

    if (getifaddrs(&ifaddrs) < 0)
	return(-1);

    have_route = route_ok(info->flags);
    addrp = NULL;
    for (ifp = ifaddrs; ifp != NULL; ifp = ifp->ifa_next)
    {
	if (have_route && strcmp(ifp->ifa_name, info->interface) != 0)
	    continue;
	if (strncmp(ifp->ifa_name, "lo", 2) == 0)
	    continue;
	if (ifp->ifa_addr->sa_family != AF_INET)
	    continue;
	addrp = (struct sockaddr_in *)ifp->ifa_addr;
	break;
    }
    if (addrp != NULL)
	(void)inet_ntop(AF_INET, &addrp->sin_addr, info->ipaddr, ADDRLEN);

    freeifaddrs(ifaddrs);
    return(addrp ? 0: -1);
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
    if (code == 0)
	freeaddrinfo(ai);
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
	info->flags |= ROUTE_OK;
    if (my_ipaddr(info) == 0)
    {
	info->flags |= HAVE_IP;
	if (check_dns() == 0)
	    info->flags |= DNS_OK;
    }

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
    char	msg[1024];
    int		cur, prev, flags;
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
    {
	flags = curp->flags;
	strcpy(msg, "You don't appear to be connected to the internet.\n"
	    "Make sure you are either connected to WiFi or your\n"
	    "ethernet cable is plugged in.  Details:\n");
	if (!have_ip(flags))
	    strcat(msg, "\n· No IP address assigned");
	if (!route_ok(flags))
	    strcat(msg, "\n· No default route");
	if (have_ip(flags) && !dns_ok(flags))
	    strcat(msg, "\n· DNS name resolution not working");
	if ((have_ip(flags) && route_ok(flags)) && !ping_ok(flags))
	    strcat(msg, "\n· Unable to ping google");
	alert(msg);
    }

    return(G_SOURCE_CONTINUE);
}
