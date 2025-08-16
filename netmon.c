#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <glob.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <gtk/gtk.h>
#include "ping_dns.h"
#include "rdp_manager.h"

#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif

#define DNS_OK		1
#define ROUTE_OK	2
#define HAVE_IP		4
#define PING_OK		8
#define NOIP2_OK	16
#define ALL_OK		(DNS_OK | ROUTE_OK | HAVE_IP | PING_OK | NOIP2_OK)

#define all_ok(flags)	(((flags) & ALL_OK) == ALL_OK)
#define dns_ok(flags)	(((flags) & DNS_OK) != 0)
#define route_ok(flags)	(((flags) & ROUTE_OK) != 0)
#define have_ip(flags)	(((flags) & HAVE_IP) != 0)
#define ping_ok(flags)	(((flags) & PING_OK) != 0)
#define noip2_ok(flags)	(((flags) & NOIP2_OK) != 0)

#define ADDRLEN		16

extern int		alltrim(char *s);
extern void		mylog(char *fmt, ...);
extern void		alert(const char *fmt, ...);

extern options_t	global_options;

struct s_info {
    char		interface[IFNAMSIZ];
    char		gateway[ADDRLEN];
    char		ipaddr[ADDRLEN];
    int			flags;
};
typedef struct s_info t_info;

t_info		netmon_info[2];
int		cur_info_index = -1;
time_t		last_reboot_warn;

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
 ********************        RESOLVE_HOSTNAME        ********************
 ************************************************************************/
int
resolve_hostname(char *host)
{
    int			code;
    struct addrinfo	hints, *ai;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;
    hints.ai_family = PF_UNSPEC;
    code = getaddrinfo(host, NULL, &hints, &ai);
    if (code == 0)
	freeaddrinfo(ai);
    return(code);
}

/************************************************************************
 ********************           CHECK_DNS            ********************
 ************************************************************************/
int
check_dns(void)
{
    return(resolve_hostname("google.com"));
}

/************************************************************************
 ********************          CHECK_NOIP2           ********************
 ************************************************************************/
int
check_noip2()
{
    glob_t	entries;
    int		i, code;
    char	*p, *fname, cmd[256];
    FILE	*fp;

    if (global_options.access_mode == LOCAL)
	return(0);

    if ((code = glob("/proc/[0-9]*[0-9]/comm", 0, NULL, &entries)) != 0)
	return(-1);

    code = -1;
    cmd[sizeof(cmd)-1] = '\0';
    for (i = 0; i < entries.gl_pathc; ++i)
    {
	fname = entries.gl_pathv[i];
	if ((fp = fopen(fname, "r")) == NULL)
	    break;
	p = fgets(cmd, sizeof(cmd)-1, fp);
	fclose(fp);
	if (p == NULL)
	    break;
	cmd[strlen(cmd)-1] = '\0';
	if (strcmp(cmd, "noip2") == 0)
	{
	    code = 0;
	    break;
	}
    }

    globfree(&entries);
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
    if (check_noip2() == 0)
	info->flags |= NOIP2_OK;
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
    if (!noip2_ok(curp->flags))
    {
	if (prevp == NULL || noip2_ok(prevp->flags))
	    mylog("Dynamic update client noip2 not running\n");
    }

    if (!all_ok(curp->flags) && (!prevp || all_ok(prevp->flags)))
    {
	flags = curp->flags;
	strcpy(msg, "You don't appear to be connected to the internet.\n"
	    "Make sure you are either connected to WiFi or your\n"
	    "ethernet cable is plugged in.  Details:\n");
	if (!noip2_ok(flags))
	    strcat(msg, "\n· Dynamic update client noip2 not running");
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

/************************************************************************
 ********************            HANDLER             ********************
 ************************************************************************/
void
test_alarm_handler(int sig)
{
    mylog("Timeout testing host connectivity\n");
}

/************************************************************************
 ********************          TEST_CONNECT          ********************
 ************************************************************************/
gboolean
test_connect(char *hostname, int port)
{
    int			code, sockfd, connfd;
    char		service[128], printable[128];
    struct addrinfo	hints, *ai, *orig_ai;
    struct sockaddr_in	*in4;
    struct sockaddr_in6	*in6;
    void		*addr;
    struct sigaction	new_action, old_action;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = PF_UNSPEC;
    snprintf(service, sizeof(service), "%d", port);
    if ((code = getaddrinfo(hostname, service, &hints, &orig_ai)) != 0)
    {
	mylog("getaddrinfo(): %d: %s\n", code, gai_strerror(code));
	return(FALSE);
    }

    // Establish a 5 second timeout for testing connectivity
    // Some failure modes can take a REALLY LONG TIME!
    memset(&new_action, 0, sizeof(new_action));
    new_action.sa_handler = test_alarm_handler;
    sigaction(SIGALRM, &new_action, &old_action);
    alarm(5);

    for (ai = orig_ai; ai != NULL; ai = ai->ai_next)
    {
	if ((sockfd = socket(ai->ai_family, SOCK_STREAM, 0)) < 0)
	{
	    mylog("socket(): %d: %s\n", errno, strerror(errno));
	    freeaddrinfo(orig_ai);
	    return(FALSE);
	}

	if (ai->ai_family == PF_INET6)
	{
	    in6 = (struct sockaddr_in6 *)ai->ai_addr;
	    addr = (void *)&in6->sin6_addr;
	}
	else
	{
	    in4 = (struct sockaddr_in *)ai->ai_addr;
	    addr = (void *)&in4->sin_addr;
	}
	inet_ntop(ai->ai_family, addr, printable, sizeof(printable));
	mylog("Connecting to %s\n", printable);
	if ((connfd = connect(sockfd, ai->ai_addr, ai->ai_addrlen)) < 0)
	    mylog("socket(): %d: %s\n", errno, strerror(errno));
	close(sockfd);
	close(connfd);
	if (connfd >= 0)
	    break;
    }
    freeaddrinfo(orig_ai);
    alarm(0);
    sigaction(SIGALRM, &old_action, NULL);
    return(ai == NULL ? FALSE : TRUE);
}

/************************************************************************
 ********************          CHECK_REBOOT          ********************
 ************************************************************************/
gboolean
check_reboot(gpointer user_data)
{
    time_t	now;

    // See if /var/run/reboot-required exists. If it does and it's been at
    // least an hour since we last warned the user, tell them it's time to
    // reboot the machine.
    if (access("/var/run/reboot-required", F_OK) < 0)
	return(G_SOURCE_CONTINUE);

    now = time(NULL);
    if (now - last_reboot_warn >= 3600)
    {
	alert("A reboot is required to apply all updates\n"
	      "Please reboot as soon as it's convenient");
	last_reboot_warn = now;
    }

    return(G_SOURCE_CONTINUE);
}
