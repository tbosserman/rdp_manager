#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <arpa/inet.h>
#include <resolv.h>
#include "ping_dns.h"

/************************************************************************
 ********************            PING_DNS            ********************
 ************************************************************************/
int
ping_dns(char *host_ip, int port)
{
    int		i, fd, code, readlen, writelen, buflen, timeout_msec;
    u_char	result[1500], buf[1500];
    res_state	statep;
    struct __res_state		state;
    struct sockaddr_in		bindaddr, connaddr;
    struct pollfd		pollfd;

    fd = -1;
    memset(result, 0, sizeof(result));
    memset(buf, 0, sizeof(buf));

    statep = &state;
    if ((code = res_ninit(statep)) != 0)
	return(ERR_RES_NINIT);

    buflen = res_nmkquery(statep, QUERY, "google.com.", ns_c_in, ns_t_a,
	result, sizeof(result), NULL, buf, sizeof(buf));
    if (buflen < 0)
	return(ERR_RES_NMKQUERY);

    memset(&connaddr, 0, sizeof(connaddr));
    connaddr.sin_family = AF_INET;
    if (inet_pton(AF_INET, host_ip, &(connaddr.sin_addr)) < 1)
	return(ERR_INET_PTON);

    memset(&bindaddr, 0, sizeof(bindaddr));
    bindaddr.sin_family = AF_INET;
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	return(ERR_SOCKET);
    if (bind(fd, (const struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0)
    {
	close(fd);
	return(ERR_BIND);
    }

    connaddr.sin_port = htons(port);
    if (connect(fd, (const struct sockaddr *)&connaddr, sizeof(connaddr)) < 0)
    {
	close(fd);
	return(ERR_CONNECT);
    }

    pollfd.fd = fd;
    pollfd.events = POLLRDNORM;
    timeout_msec = 250;
    for (i = 0; i < 3; ++i)
    {
	if ((writelen = write(fd, buf, buflen)) != buflen)
	{
	    close(fd);
	    return(ERR_WRITE);
	}
	if ((code = poll(&pollfd, 1, timeout_msec)) < 0)
	{
	    close(fd);
	    return(ERR_POLL);
	}
	timeout_msec <<= 2;
	if (code == 0) /* No data read to read -- timed out */
	    continue;
	if ((readlen = read(fd, result, sizeof(result))) < 0)
	{
	    close(fd);
	    return(ERR_READ);
	}
	break;
    }

    close(fd);
    
    if (i >= 3)
	return(ERR_TIMEOUT);

    return(SUCCESS);
}
