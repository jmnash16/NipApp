/*******************************************************************************
 * Copyright © 2014 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
 * Copyright © 2009 Ocean Blue Software Ltd
 *
 * This file is part of a DTVKit Software Component
 * You are permitted to copy, modify or distribute this file subject to the terms
 * of the DTVKit 1.0 Licence which can be found in licence.txt or at www.dtvkit.org
 *
 * THIS CODE AND INFORMATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * If you or your organisation is not a member of DTVKit then you have access
 * to this source code outside of the terms of the licence agreement
 * and you are expected to delete this and any associated files immediately.
 * Further information on DTVKit, membership and terms can be found at www.dtvkit.org
 *******************************************************************************/
/**
 * @brief   Socket functions
 * @file    stbhwnet.c
 * @date    24/06/2009
 * @author  Sergio Panseri
 */

#define NETWORK_ERROR
/*#define NETWORK_DEBUG*/

/*---includes for this file--------------------------------------------------*/
/* compiler library header files */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

/* third party header files */

/* DVBCore header files */
#include <techtype.h>
#include <dbgfuncs.h>
#include "stbhwmem.h"
#include "stbhwos.h"
#include "stbhwc.h"
#include "stbhwnet.h"

/*---constant definitions for this file--------------------------------------*/
#define MAX_IFS            64
#define GATEWAY_BUFSIZE    8192

#define RESOLV_CONF        "/etc/resolv.conf"

#ifdef NETWORK_ERROR
   #define NET_ERR(x)    STB_SPDebugWrite x
#else
   #define NET_ERR(x)
#endif

#ifdef NETWORK_DEBUG
   #define NET_DBG(x)      STB_SPDebugWrite x
#else
   #define NET_DBG(x)
#endif

#define NW_TASK_STACK_SIZE 1024
#define NW_TASK_PRIORITY 8

#if 0
#ifdef FUNCTION_START
#undef FUNCTION_START
#endif
#define FUNCTION_START(x)     printf(">>> %s\n", # x);

#ifdef FUNCTION_FINISH
#undef FUNCTION_FINISH
#endif
#define FUNCTION_FINISH(x)     printf("<<< %s\n", # x);
#endif

/*---local typedef structs for this file-------------------------------------*/
typedef struct
{
   int socket;
} S_SOCKET_DESC;

typedef struct s_nw_eth_monitor
{
   struct s_nw_eth_monitor *next;
   NW_eth_callback function;
} S_NW_ETH_MONITOR;

struct route_info
{
   struct in_addr dstAddr;
   struct in_addr srcAddr;
   struct in_addr gateWay;
   char ifName[IF_NAMESIZE];
};

/*---local (static) variable declarations for this file----------------------*/
static S_NW_ETH_MONITOR *nw_monitor_list = NULL;
static void *nw_eth_task_ptr = NULL;
static void *nw_mutex = NULL;
static int nw_eth_status_running = 0;
static E_NW_LINK_STATUS current_ethernet_status;

/*---local function prototypes for this file---------------------------------*/
static void EthernetMonitorTask( void *arg );
static int readNlSock(int sockFd, char *bufPtr, unsigned int seqNum, unsigned int pId);
static void parseRoutes(struct nlmsghdr *nlHdr, struct route_info *rtInfo);
static void UpdateSocks(S_NW_SOCKSET *socks, fd_set *pfds);

/*---global function definitions---------------------------------------------*/

/*****************************************************************************
 * @brief   Initialises the socket API, must be called once before using API
 * @retval  TRUE if initialised ok, FALSE otherwise
 *****************************************************************************/
BOOLEAN STB_NWInitialise(void)
{
   FUNCTION_START(STB_NWInitialise);

   nw_eth_status_running = 1;
   nw_mutex = STB_OSCreateMutex();
   nw_eth_task_ptr = STB_OSCreateTask(EthernetMonitorTask, NULL, NW_TASK_STACK_SIZE,
         NW_TASK_PRIORITY, (U8BIT *)"ethtsk" );

   FUNCTION_FINISH(STB_NWInitialise);

   return TRUE;
}

/*****************************************************************************
 * @brief   Sets the network interface that will be used for all network or IP operations
 * @param   interface - network interface type to use
 * @retval  TRUE if interface is available, FALSE otherwise
 *****************************************************************************/
BOOLEAN STB_NWSelectInterface(E_NW_INTERFACE interface)
{
   FUNCTION_START(STB_NWSelectInterface);
   USE_UNWANTED_PARAM(interface);
   FUNCTION_FINISH(STB_NWSelectInterface);
   return(FALSE);
}

/**
 * @brief   Returns the selected interface
 * @return  Selected interface
 */
E_NW_INTERFACE STB_NWGetSelectedInterface(void)
{
   FUNCTION_START(STB_NWGetSelectedInterface);
   FUNCTION_FINISH(STB_NWGetSelectedInterface);

   return NW_WIRED;
}

/*****************************************************************************
 * @brief   Gets the current IPv4 address of the default IP connection
 * @param   ip_addr - 4 byte array in which the address will be returned
 *                    with the most significant byte in ip_addr[0]
 * @retval  TRUE if address is returned, FALSE otherwise
 *****************************************************************************/
BOOLEAN STB_IPGetIPAddress(U8BIT ip_addr[4])
{
   struct ifreq *ifr, *ifend;
   struct ifconf ifc;
   struct ifreq ifs[MAX_IFS];
   struct sockaddr address;
   struct sockaddr_in *address_in;
   int sockFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   BOOLEAN result;

   FUNCTION_START(STB_IPGetIPAddress);

   result = FALSE;

   ifc.ifc_len = sizeof(ifs);
   ifc.ifc_req = ifs;

   if (ioctl(sockFD, SIOCGIFCONF, &ifc) < 0)
   {
      NET_ERR(("ioctl(SIOCGIFCONF): Failed\n"));
   }
   else
   {
      ifend = ifs + (ifc.ifc_len / sizeof(struct ifreq));
      for (ifr = ifc.ifc_req; ifr < ifend; ifr++)
      {
         if (ifr->ifr_addr.sa_family == AF_INET)
         {
            if (strncmp(ifr->ifr_name, "eth", 3) == 0)
            {
               address = ifr->ifr_addr;
               address_in = (struct sockaddr_in *) &address;

               ip_addr[3] = (address_in->sin_addr.s_addr >> 24) & 0xFF;
               ip_addr[2] = (address_in->sin_addr.s_addr >> 16) & 0xFF;
               ip_addr[1] = (address_in->sin_addr.s_addr >> 8) & 0xFF;
               ip_addr[0] = address_in->sin_addr.s_addr & 0xFF;

               NET_DBG(("IP address: %d.%d.%d.%d", ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]));
               result = TRUE;
            }
         }
      }

      close(sockFD);
   }

   FUNCTION_FINISH(STB_IPGetIPAddress);

   return(result);
}

/*****************************************************************************
 * @brief   Gets the current IPv4 subnet mask of the default IP connection
 * @param   subnet_mask - 4 byte array in which the address will be returned
 *                        with the most significant byte in subnet_mask[0]
 * @retval  TRUE if address is returned, FALSE otherwise
 *****************************************************************************/
BOOLEAN STB_IPGetSubnetMask(U8BIT subnet_mask[4])
{
   struct ifreq *ifr, *ifend;
   struct ifreq ifreq;
   struct ifconf ifc;
   struct ifreq ifs[MAX_IFS];
   struct sockaddr address;
   struct sockaddr_in *address_in;
   int sockFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   BOOLEAN result;

   FUNCTION_START(STB_IPGetSubnetMask);

   result = FALSE;

   ifc.ifc_len = sizeof(ifs);
   ifc.ifc_req = ifs;

   if (ioctl(sockFD, SIOCGIFCONF, &ifc) < 0)
   {
      NET_ERR(("ioctl(SIOCGIFCONF): Failed"));
   }
   else
   {
      ifend = ifs + (ifc.ifc_len / sizeof(struct ifreq));
      for (ifr = ifc.ifc_req; ifr < ifend; ifr++)
      {
         if (ifr->ifr_addr.sa_family == AF_INET)
         {
            if (strncmp(ifr->ifr_name, "eth", 3) == 0)
            {
               strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof(ifreq.ifr_name));
               if (ioctl(sockFD, SIOCGIFNETMASK, &ifreq) < 0)
               {
                  NET_DBG(("SIOCSIFNETMASK(%s): %m", ifreq.ifr_name));
               }
               else
               {
                  address = ifreq.ifr_netmask;
                  address_in = (struct sockaddr_in *) &address;

                  subnet_mask[3] = (address_in->sin_addr.s_addr >> 24) & 0xFF;
                  subnet_mask[2] = (address_in->sin_addr.s_addr >> 16) & 0xFF;
                  subnet_mask[1] = (address_in->sin_addr.s_addr >> 8) & 0xFF;
                  subnet_mask[0] = address_in->sin_addr.s_addr & 0xFF;

                  NET_DBG(("Subnet mask: %u.%u.%u.%u", subnet_mask[0], subnet_mask[1],
                           subnet_mask[2], subnet_mask[3]));
                  result = TRUE;
                  break;
               }
            }
         }
      }
      close(sockFD);
   }

   FUNCTION_FINISH(STB_IPGetSubnetMask);

   return(result);
}

/*****************************************************************************
 * @brief   Gets the current IPv4 gateway IP address
 * @param   gateway_addr - 4 byte array in which the address will be returned
 *                         with the most significant byte in gateway_addr[0]
 * @retval  TRUE if address is returned, FALSE otherwise
 *****************************************************************************/
BOOLEAN STB_IPGetGatewayIPAddress(U8BIT gateway_addr[4])
{
   struct nlmsghdr *nlMsg;
   struct route_info *rtInfo;
   char *msgBuf;
   int sock, len;
   unsigned int msgSeq = 0;
   BOOLEAN found_gateway;

   FUNCTION_START(STB_IPGetGatewayIPAddress);

   found_gateway = FALSE;

   /* Create Socket */
   if ((sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) < 0)
   {
      NET_ERR(("STB_IPGetGatewayIPAddress: Failed to open socket"));
   }
   else
   {
      msgBuf = STB_MEMGetSysRAM(GATEWAY_BUFSIZE);
      if (msgBuf != NULL)
      {
         /* Initialize the buffer */
         memset(msgBuf, 0, GATEWAY_BUFSIZE);

         /* point the header and the msg structure pointers into the buffer */
         nlMsg = (struct nlmsghdr *)msgBuf;

         /* Fill in the nlmsg header*/
         nlMsg->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg)); // Length of message.
         nlMsg->nlmsg_type = RTM_GETROUTE; // Get the routes from kernel routing table .

         nlMsg->nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST; // The message is a request for dump.
         nlMsg->nlmsg_seq = msgSeq++; // Sequence of the message packet.
         nlMsg->nlmsg_pid = getpid(); // PID of process sending the request.

         /* Send the request */
         if (send(sock, nlMsg, nlMsg->nlmsg_len, 0) < 0)
         {
            NET_DBG(("STB_IPGetGatewayIPAddress: Failed to write to socket"));
         }
         else
         {
            /* Read the response */
            if ((len = readNlSock(sock, msgBuf, msgSeq, getpid())) < 0)
            {
               NET_DBG(("STB_IPGetGatewayIPAddress: Read from socket failed"));
            }
            else
            {
               /* Parse and print the response */
               rtInfo = (struct route_info *)STB_MEMGetSysRAM(sizeof(struct route_info));
               if (rtInfo != NULL)
               {
                  for (; NLMSG_OK(nlMsg, len); nlMsg = NLMSG_NEXT(nlMsg, len))
                  {
                     memset(rtInfo, 0, sizeof(struct route_info));
                     parseRoutes(nlMsg, rtInfo);

                     // Check if default gateway
                     if (strstr((char *)inet_ntoa(rtInfo->dstAddr), "0.0.0.0"))
                     {
                        found_gateway = TRUE;
                        gateway_addr[3] = (rtInfo->gateWay.s_addr >> 24) & 0xFF;
                        gateway_addr[2] = (rtInfo->gateWay.s_addr >> 16) & 0xFF;
                        gateway_addr[1] = (rtInfo->gateWay.s_addr >> 8) & 0xFF;
                        gateway_addr[0] = rtInfo->gateWay.s_addr & 0xFF;

                        NET_DBG(("Gateway: %u.%u.%u.%u", gateway_addr[0], gateway_addr[1],
                                 gateway_addr[2], gateway_addr[3]));
                        break;
                     }
                  }

                  STB_MEMFreeSysRAM(rtInfo);
               }
            }
         }

         STB_MEMFreeSysRAM(msgBuf);
      }

      close(sock);
   }

   FUNCTION_FINISH(STB_IPGetGatewayIPAddress);

   return(found_gateway);
}

/*****************************************************************************
 * @brief   Gets the current IPv4 DNS server IP address
 * @param   dns_addr - 4 byte array in which the address will be returned
 *                     with the most significant byte in dns_addr[0]
 * @retval  TRUE if address is returned, FALSE otherwise
 *****************************************************************************/
BOOLEAN STB_IPGetDnsServerIPAddress(U8BIT *dns_addr)
{
   char *pch;
   FILE *pFile;
   char buffer[255];
   struct in_addr addr;
   BOOLEAN addr_found;

   FUNCTION_START(STB_IPGetDnsServerIPAddress);

   addr_found = FALSE;

   pFile = fopen(RESOLV_CONF, "r");
   if (pFile != NULL)
   {
      while (!addr_found && (fgets(buffer, 255, pFile) != 0))
      {
         pch = strtok(buffer, " \n");
         while (pch != NULL)
         {
            if (strcmp(pch, "nameserver") == 0)
            {
               addr_found = TRUE;
            }

            pch = strtok(NULL, " \n");
            if (addr_found && (pch != NULL))
            {
               if (inet_aton(pch, &addr) != 0)
               {
                  dns_addr[3] = (addr.s_addr >> 24) & 0xFF;
                  dns_addr[2] = (addr.s_addr >> 16) & 0xFF;
                  dns_addr[1] = (addr.s_addr >> 8) & 0xFF;
                  dns_addr[0] = addr.s_addr & 0xFF;
                  addr_found = TRUE;
                  pch = NULL;
                  NET_DBG(("DNS: %u.%u.%u.%u", dns_addr[0], dns_addr[1], dns_addr[2], dns_addr[3]));
               }
            }
         }
      }
      fclose(pFile);
   }

   FUNCTION_FINISH(STB_IPGetDnsServerIPAddress);

   return(addr_found);
}

/*****************************************************************************
 * @brief   Gets the MAC address of the default ethernet connection
 * @param   interface - NW_WIRED or NW_WIRELESS
 * @param   mac_address - 6 byte array in which the address will be returned
 *                        with the most significant byte in mac_addr[0]
 * @retval  TRUE if address is returned, FALSE otherwise
 *****************************************************************************/
BOOLEAN STB_NWGetMACAddress(E_NW_INTERFACE interface, U8BIT *mac_addr)
{
   BOOLEAN addr_found;
   struct ifreq ifr;
   struct ifreq *pifr;
   struct ifreq *last_ifr;
   struct ifconf ifc;
   U32BIT buffer_size;
   char *buffer;
   BOOLEAN overflowed;
   int sock;

   FUNCTION_START(STB_NWGetMACAddress);
   USE_UNWANTED_PARAM(interface);

   addr_found = FALSE;

   if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)) >= 0)
   {
      buffer = NULL;
      buffer_size = 1024;
      overflowed = FALSE;

      do
      {
         if (buffer != NULL)
         {
            /* Free the current buffer and allocate a bigger one */
            STB_MEMFreeSysRAM(buffer);
            buffer_size *= 2;
         }

         if ((buffer = STB_MEMGetSysRAM(buffer_size)) != NULL)
         {
            ifc.ifc_len = buffer_size;
            ifc.ifc_buf = buffer;
            overflowed = FALSE;

            if (ioctl(sock, SIOCGIFCONF, &ifc) >= 0)
            {
               if ((U32BIT)ifc.ifc_len == buffer_size)
               {
                  /* Buffer passed in has been filled, so a bigger buffer is needed */
                  overflowed = TRUE;
               }
            }
            else
            {
               NET_ERR(("%s: ioctl(SIOCGIFCONF) failed, errno %d", __FUNCTION__, errno));
               STB_MEMFreeSysRAM(buffer);
               buffer = NULL;
            }
         }
#ifdef NETWORK_ERROR
         else
         {
            NET_ERR(("%s: Failed to allocate buffer of %lu bytes", __FUNCTION__, buffer_size));
         }
#endif
      }
      while (overflowed && (buffer != NULL));

      if (buffer != NULL)
      {
         last_ifr = ifc.ifc_req + (ifc.ifc_len / sizeof(struct ifreq));

         for (pifr = ifc.ifc_req; (pifr != last_ifr) && !addr_found; pifr++)
         {
            strcpy(ifr.ifr_name, pifr->ifr_name);
            if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0)
            {
               /* Skip the loopback network */
               if ((ifr.ifr_flags & IFF_LOOPBACK) == 0)
               {
                  if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0)
                  {
                     /* Found the address, copy it to the output */
                     memcpy(mac_addr, ifr.ifr_hwaddr.sa_data, 6);
                     addr_found = TRUE;
                     NET_DBG(("%s: MAC=%02x:%02x:%02x:%02x:%02x:%02x", __FUNCTION__, mac_addr[0],
                              mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]));
                  }
               }
            }
         }

         STB_MEMFreeSysRAM(buffer);
      }
      close(sock);
   }
#ifdef NETWORK_ERROR
   else
   {
      NET_ERR(("%s: Failed to open socket", __FUNCTION__));
   }
#endif

   FUNCTION_FINISH(STB_NWGetMACAddress);

   return(addr_found);
}

/*****************************************************************************
 * @brief   Sets the IPv4 format IP address of default network connection
 * @param   ip_addr - 4 byte array containing the IP address to be set
 *                    with the most significant byte in ip_addr[0]
 *****************************************************************************/
void STB_IPSetIPAddress(const U8BIT *ip_addr)
{
   char cmd[128];

   FUNCTION_START(STB_IPSetIPAddress);

   sprintf(cmd, "ifconfig eth0 %u.%u.%u.%u", ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]);
   system(cmd);

   FUNCTION_FINISH(STB_IPSetIPAddress);
}

/*****************************************************************************
 * @brief   Sets the IPv4 format subnet mask of default network connection
 * @param   subnet_mask - 4 byte array containing the mask to be set
 *                        with the most significant byte in subnet_mask[0]
 *****************************************************************************/
void STB_IPSetSubnetMask(const U8BIT *subnet_mask)
{
   char cmd[128];

   FUNCTION_START(STB_IPSetSubnetMask);

   sprintf(cmd, "ifconfig eth0 netmask %u.%u.%u.%u", subnet_mask[0], subnet_mask[1],
      subnet_mask[2], subnet_mask[3]);
   system(cmd);

   FUNCTION_FINISH(STB_IPSetSubnetMask);
}

/*****************************************************************************
 * @brief   Sets the IPv4 format gateway IP address
 * @param   gateway_addr - 4 byte array containing the mask to be set
 *                         with the most significant byte in gateway_addr[0]
 *****************************************************************************/
void STB_IPSetGatewayIPAddress(const U8BIT *gateway_addr)
{
   char cmd[128];

   FUNCTION_START(STB_IPSetGatewayIPAddress);

   sprintf(cmd, "route add default gw %u.%u.%u.%u", gateway_addr[0], gateway_addr[1],
      gateway_addr[2], gateway_addr[3]);
   system(cmd);

   FUNCTION_FINISH(STB_IPSetGatewayIPAddress);
}

/*****************************************************************************
 * @brief   Sets the IPv4 format DNS server IP address
 * @param   dns_addr - 4 byte array containing the mask to be set
 *                     with the most significant byte in dns_addr[0]
 *****************************************************************************/
void STB_IPSetDnsServerIPAddress(const U8BIT *dns_addr)
{
   FILE *fp;

   FUNCTION_START(STB_IPSetDnsServerIPAddress);

   fp = fopen(RESOLV_CONF, "w");
   if (fp != NULL)
   {
      fprintf(fp, "nameserver %u.%u.%u.%u\n", dns_addr[0], dns_addr[1], dns_addr[2], dns_addr[3]);
      fclose(fp);
   }

   FUNCTION_FINISH(STB_IPSetDnsServerIPAddress);
}

/*****************************************************************************
 * @brief   Cause the IP address to be set using DHCP
 * @param   wait_for_completion - set as TRUE if the call shouldn't return until an IP
 *                                address has been assigned or times out
 *****************************************************************************/
void STB_IPGetIPByDhcp(BOOLEAN wait_for_completion)
{
   char cmd[128];

   FUNCTION_START(STB_IPGetIPByDhcp);

   sprintf(cmd, "sudo dhclient");
   if (!wait_for_completion)
   {
      strcat(cmd, "&");
   }

   system(cmd);

   FUNCTION_FINISH(STB_IPGetIPByDhcp);
}

/*****************************************************************************
 * @brief   Performs a lookup to find the IP address(es) of the given host name
 * @param   name - host name
 * @param   nw_addrs - array of structures containing the results of the lookup.
 *                     This array must be deleted using STB_MEMFreeSysRAM.
 * @return  The number of addresses returned in the array
 *****************************************************************************/
U16BIT STB_NWLookupAddress(U8BIT *name, S_NW_ADDR_INFO **nw_addrs)
{
   U16BIT num_addrs;
   struct addrinfo *addrs;
   struct addrinfo *ra;
   struct sockaddr_in *addr4;
   struct sockaddr_in6 *addr6;
   U16BIT i;

   FUNCTION_START(STB_NWLookupAddress);

   if (getaddrinfo((char *)name, NULL, NULL, &addrs) == 0)
   {
      /* Count the number of returned addresses */
      for (ra = addrs, num_addrs = 0; ra != NULL; ra = ra->ai_next)
      {
         if (((ra->ai_family == AF_INET) || (ra->ai_family == AF_INET6)) &&
             ((ra->ai_socktype == SOCK_DGRAM) || (ra->ai_socktype == SOCK_STREAM)) &&
             ((ra->ai_protocol == IPPROTO_UDP) || (ra->ai_protocol == IPPROTO_TCP) || (ra->ai_protocol == IPPROTO_IP)))
         {
            num_addrs++;
         }
      }

      if (num_addrs > 0)
      {
         /* Allocate array to be returned */
         if ((*nw_addrs = (S_NW_ADDR_INFO *)STB_MEMGetSysRAM(num_addrs * sizeof(S_NW_ADDR_INFO))) != NULL)
         {
            for (ra = addrs, i = 0; ra != NULL; ra = ra->ai_next)
            {
               if (((ra->ai_family == AF_INET) || (ra->ai_family == AF_INET6)) &&
                   ((ra->ai_socktype == SOCK_DGRAM) || (ra->ai_socktype == SOCK_STREAM)) &&
                   ((ra->ai_protocol == IPPROTO_UDP) || (ra->ai_protocol == IPPROTO_TCP) || (ra->ai_protocol == IPPROTO_IP)))
               {
                  (*nw_addrs)[i].af = (ra->ai_family == AF_INET6) ? NW_AF_INET6 : NW_AF_INET;
                  (*nw_addrs)[i].type = (ra->ai_socktype == SOCK_DGRAM) ? NW_SOCK_DGRAM : NW_SOCK_STREAM;
                  (*nw_addrs)[i].protocol = (ra->ai_protocol == IPPROTO_UDP) ? NW_PROTOCOL_UDP : NW_PROTOCOL_TCP;

                  if (ra->ai_family == AF_INET)
                  {
                     addr4 = (struct sockaddr_in *)ra->ai_addr;

                     if (IN_MULTICAST(ntohl(addr4->sin_addr.s_addr)))
                     {
                        (*nw_addrs)[i].is_multicast = TRUE;
                     }
                     else
                     {
                        (*nw_addrs)[i].is_multicast = FALSE;
                     }

                     //if (inet_ntop(ra->ai_family, (void *)&((struct sockaddr_in *)ra->ai_addr)->sin_addr,
                     if (inet_ntop(ra->ai_family, (void *)&addr4->sin_addr,
                            (char *)(*nw_addrs)[i].addr, sizeof((*nw_addrs)[i].addr)) == NULL)
                     {
                        NET_ERR(("%s: inet_ntop failed, errno %d", __FUNCTION__, errno));
                     }
                  }
                  else
                  {
                     addr6 = (struct sockaddr_in6 *)ra->ai_addr;

                     if (IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr))
                     {
                        (*nw_addrs)[i].is_multicast = TRUE;
                     }
                     else
                     {
                        (*nw_addrs)[i].is_multicast = FALSE;
                     }

                     if (inet_ntop(ra->ai_family, (void *)&addr6->sin6_addr,
                            (char *)(*nw_addrs)[i].addr, sizeof((*nw_addrs)[i].addr)) == NULL)
                     {
                        NET_ERR(("%s: inet_ntop failed, errno %d", __FUNCTION__, errno));
                     }
                  }

                  i++;
               }
            }
         }
      }

      freeaddrinfo(addrs);
   }
   else
   {
      NET_ERR(("STB_NWLookupAddress: Failed to lookup address for \"%s\", errno %d", name, errno));
      num_addrs = 0;
   }

   FUNCTION_FINISH(STB_NWLookupAddress);

   return(num_addrs);
}

/*****************************************************************************
 * @brief    Opens (creates) a new socket for subsequent use
 * @param    af - the address family that the socket will be used with
 * @param    type - the stream type the socket will be used with
 * @param    protocol - the protocol the socket will be used with
 * @return   The socket handle, or NULL if failed
 *****************************************************************************/
void* STB_NWOpenSocket(E_NW_AF af, E_NW_TYPE type, E_NW_PROTOCOL protocol, BOOLEAN nonblock)
{
   void *retval = NULL;
   int fd, socket_family, socket_type;
   S_SOCKET_DESC *socket_desc;

   USE_UNWANTED_PARAM(protocol); /* used in windows socket implementation */
   USE_UNWANTED_PARAM(nonblock);

   FUNCTION_START(STB_NWOpenSocket);

   switch (af)
   {
      case NW_AF_INET:
         socket_family = AF_INET;
         break;

      case NW_AF_INET6:
         socket_family = AF_INET6;
         break;

      default:
         socket_family = -1;
         break;
   }

   switch (type)
   {
      case NW_SOCK_STREAM:
         socket_type = SOCK_STREAM;
         break;

      case NW_SOCK_DGRAM:
         socket_type = SOCK_DGRAM;
         break;

      default:
         socket_type = -1;
         break;
   }

   if ((socket_type != -1) && (socket_family != -1))
   {
      fd = socket(socket_family, socket_type, 0);
      if (fd >= 0)
      {
         socket_desc = (S_SOCKET_DESC *)STB_MEMGetAppRAM(sizeof(S_SOCKET_DESC));
         if (socket_desc != NULL)
         {
            socket_desc->socket = fd;
            retval = (void *)socket_desc;
         }
         else
         {
            close(fd);
         }
      }
   }
   else
   {
      NET_ERR(("STB_NWOpenSocket: wrong parameter af=%d, type=%d, prot=%d", af, type, protocol));
   }

   FUNCTION_FINISH(STB_NWOpenSocket);

   return retval;
}

/*****************************************************************************
 * @brief    Closes (destroys) a socket instance
 * @param    socket - handle of the socket to be closed
 *****************************************************************************/
BOOLEAN STB_NWCloseSocket(void *socket)
{
   S_SOCKET_DESC *socket_desc;
   BOOLEAN retval = FALSE;
   /*int res;*/

   FUNCTION_START(STB_NWCloseSocket);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;

      /*res =*/ shutdown(socket_desc->socket, SHUT_RDWR);
      if (close(socket_desc->socket) == 0)
      {
         STB_MEMFreeAppRAM((void *)socket_desc);
         retval = TRUE;
      }
#ifdef NETWORK_ERROR
      else
      {
         NET_ERR(("STB_NWCloseSocket: close failed, errno=%u", errno));
      }
#endif
   }

   FUNCTION_FINISH(STB_NWCloseSocket);

   return retval;
}

/*****************************************************************************
 * @brief    Binds (names) a socket in the local address space
 * @param    socket - the handle of the socket to be bound
 * @param    af - the address family to be bound to
 * @param    address - the string address (NULL to let OS choose)
 * @param    port - the port number to be bound to (zero to let OS choose)
 * @retval   TRUE - successfully bound
 * @retval   FALSE - failed to bind
 *****************************************************************************/
BOOLEAN STB_NWBind(void *socket, U8BIT *address, U32BIT port)
{
   S_SOCKET_DESC *socket_desc;
   struct sockaddr_in addr;
   BOOLEAN retval = FALSE;

   FUNCTION_START(STB_NWBind);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;
      memset(&addr, 0, sizeof(struct sockaddr_in));
      addr.sin_family = AF_INET;
      if (address == NULL)
      {
         NET_DBG(("Binding, address ANY"));
         addr.sin_addr.s_addr = INADDR_ANY;
      }
      else
      {
         NET_DBG(("Binding with address %s", address));
         addr.sin_addr.s_addr = inet_addr((char *)address);
      }
      addr.sin_port = htons(port);
      if (bind(socket_desc->socket, (struct sockaddr *)&addr, sizeof(struct sockaddr)) >= 0)
      {
         retval = TRUE;
         NET_DBG(("Bind (fd=%d) port %d, done okay", socket_desc->socket, port));
      }
   }

   FUNCTION_FINISH(STB_NWBind);

   return retval;
}

/*****************************************************************************
 * @brief    Sets the socket option SO_REUSEADDR
 * @param    socket - the handle of the socket to be bound
 * @param    state - TRUE or FALSE to activate/deactivate the
 *           option REUSEADDR
 * @retval   TRUE - successfully set
 * @retval   FALSE - failed to set
 *****************************************************************************/
BOOLEAN STB_NWSetReuseaddr(void *socket, BOOLEAN state)
{
   S_SOCKET_DESC *socket_desc;
   BOOLEAN retval = FALSE;
   int optval;

   FUNCTION_START(STB_NWSetReuseaddr);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;
      optval = state ? 1 : 0;
      if (setsockopt(socket_desc->socket, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, 4) == 0)
      {
         retval = TRUE;
      }
   }

   FUNCTION_FINISH(STB_NWSetReuseaddr);

   return retval;
}

/*****************************************************************************
 * @brief    Gets the socket option SO_REUSEADDR
 * @param    socket - the handle of the socket
 * @param    state - TRUE or FALSE if the option is actie/inactivate
 * @retval   TRUE - successfully got
 * @retval   FALSE - failed to get
 *****************************************************************************/
BOOLEAN STB_NWGetReuseaddr(void *socket, BOOLEAN *state)
{
   S_SOCKET_DESC *socket_desc;
   BOOLEAN retval = FALSE;
   int optval;
   socklen_t option_len;

   FUNCTION_START(STB_NWGetReuseaddr);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;
      if (getsockopt(socket_desc->socket, SOL_SOCKET, SO_REUSEADDR, (char *)&optval,
             &option_len) == 0)
      {
         *state = (optval > 0) ? TRUE : FALSE;
         retval = TRUE;
      }
   }

   FUNCTION_FINISH(STB_NWGetReuseaddr);

   return retval;
}

/*****************************************************************************
 * @brief    Sets the ip option IP_ADD_MEMBERSHIP
 * @param    socket - the handle of the socket
 * @param    group_address address to add
 * @retval   TRUE - successfully set
 * @retval   FALSE - failed to set
 *****************************************************************************/
BOOLEAN STB_NWAddMembership(void *socket, U8BIT *group_address)
{
   S_SOCKET_DESC *socket_desc;
   struct ip_mreq imr;
   BOOLEAN retval = FALSE;

   FUNCTION_START(STB_NWAddMembership);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;

      /* This block configures the socket for multicast */
      memset(&imr, 0, sizeof(struct ip_mreq));
      imr.imr_multiaddr.s_addr = inet_addr((char *)group_address);
      imr.imr_interface.s_addr = INADDR_ANY;
      if (setsockopt(socket_desc->socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&imr,
             sizeof(struct ip_mreq)) == 0)
      {
         retval = TRUE;
      }
   }

   FUNCTION_FINISH(STB_NWAddMembership);

   return retval;
}

/*****************************************************************************
 * @brief    Sets the ip option IP_DROP_MEMBERSHIP
 * @param    socket - the handle of the socket
 * @param    group_address address to drop
 * @retval   TRUE - successfully set
 * @retval   FALSE - failed to set
 *****************************************************************************/
BOOLEAN STB_NWDropMembership(void *socket, U8BIT *group_address)
{
   S_SOCKET_DESC *socket_desc;
   struct ip_mreq imr;
   BOOLEAN retval = FALSE;

   FUNCTION_START(STB_NWDropMembership);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;

      imr.imr_multiaddr.s_addr = inet_addr((char *)group_address);
      imr.imr_interface.s_addr = INADDR_ANY;
      if (setsockopt(socket_desc->socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *)&imr,
             sizeof(struct ip_mreq)) == 0)
      {
         retval = TRUE;
      }
   }

   FUNCTION_FINISH(STB_NWDropMembership);

   return retval;
}

BOOLEAN STB_NWGetSocketName(void *socket, E_NW_AF *af, U8BIT *address, U32BIT *port)
{
   S_SOCKET_DESC *socket_desc;
   BOOLEAN retval = FALSE;
   struct sockaddr_in addr;
   socklen_t sock_len;
   char *a;

   FUNCTION_START(STB_NWGetSocketName);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;

      if (getsockname(socket_desc->socket, (struct sockaddr *)&addr, &sock_len) == 0)
      {
         retval = TRUE;
         a = inet_ntoa(addr.sin_addr);
         if ((a != NULL) && (address != NULL))
         {
            strcpy((char *)address, a);
         }
         if (port != NULL)
         {
            *port = addr.sin_port;
         }
         if (af != NULL)
         {
            switch (addr.sin_family)
            {
               case AF_INET:
                  *af = NW_AF_INET;
                  break;

               case AF_INET6:
                  *af = NW_AF_INET6;
                  break;
            }
         }
      }
   }

   FUNCTION_FINISH(STB_NWGetSocketName);

   return retval;
}

E_NW_ERROR STB_NWConnect(void *socket, U8BIT *address, U32BIT port)
{
   S_SOCKET_DESC *socket_desc;
   struct sockaddr_in addr;
   E_NW_ERROR retval = NW_ERROR;

   FUNCTION_START(STB_NWConnect);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;
      memset(&addr, 0, sizeof(struct sockaddr_in));
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = inet_addr((char *)address);
      addr.sin_port = htons(port);
      if (connect(socket_desc->socket, (struct sockaddr *)&addr, sizeof(struct sockaddr)) >= 0)
      {
         retval = NW_OK;
      }
#ifdef NETWORK_ERROR
      else
      {
         NET_ERR(("STB_NWConnect: connect failed, errno = %d", errno));
      }
#endif
   }
#ifdef NETWORK_ERROR
   else
   {
      NET_ERR(("STB_NWConnect: socket id NULL"));
   }
#endif

   FUNCTION_FINISH(STB_NWConnect);

   return retval;
}

BOOLEAN STB_NWListen(void *socket, S32BIT backlog)
{
   S_SOCKET_DESC *socket_desc;
   BOOLEAN retval = FALSE;

   FUNCTION_START(STB_NWListen);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;
      if (listen(socket_desc->socket, backlog) == 0)
      {
         retval = TRUE;
      }
   }

   FUNCTION_FINISH(STB_NWListen);

   return retval;
}

void* STB_NWAccept(void *socket, U8BIT *address, U32BIT *port)
{
   S_SOCKET_DESC *socket_desc;
   int new_socket;
   S_SOCKET_DESC *new_desc = NULL;
   socklen_t sock_len;
   struct sockaddr_in addr;
   char *a;

   FUNCTION_START(STB_NWAccept);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;

      new_socket = accept(socket_desc->socket, (struct sockaddr *)&addr, &sock_len);
      if (new_socket >= 0)
      {
         new_desc = (S_SOCKET_DESC *)STB_MEMGetAppRAM(sizeof(S_SOCKET_DESC));
         if (new_desc != NULL)
         {
            new_desc->socket = new_socket;
            a = inet_ntoa(addr.sin_addr);
            if ((a != NULL) && (address != NULL))
            {
               strcpy((char *)address, a);
            }
            if (port != NULL)
            {
               *port = ntohs(addr.sin_port);
            }
         }
      }
   }

   FUNCTION_FINISH(STB_NWAccept);

   return (void *)new_desc;
}

S32BIT STB_NWSend(void *socket, U8BIT *buf, U32BIT num_bytes)
{
   S_SOCKET_DESC *socket_desc;
   S32BIT retval = -1;

   FUNCTION_START(STB_NWSend);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;
      retval = send(socket_desc->socket, buf, (size_t)num_bytes, 0);
      if (retval <= 0)
      {
         NET_ERR(("STB_NWSend: Send failed errno = %d", errno));
      }
   }
   else
   {
      NET_ERR(("STB_NWSend: socket id NULL"));
   }

   FUNCTION_FINISH(STB_NWSend);

   return retval;
}

S32BIT STB_NWReceive(void *socket, U8BIT *buf, U32BIT max_bytes)
{
   S_SOCKET_DESC *socket_desc;
   S32BIT retval;

   FUNCTION_START(STB_NWReceive);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;
      retval = recv(socket_desc->socket, buf, (size_t)max_bytes, MSG_DONTWAIT);
#ifdef NETWORK_ERROR
      if (retval < 0)
      {
         NET_ERR(("STB_NWReceive(%p): Receive failed errno = %d", socket, errno));
      }
#endif
   }
#ifdef NETWORK_ERROR
   else
   {
      NET_ERR(("STB_NWReceive: socket id NULL"));
      retval = -1;
   }
#endif

   FUNCTION_FINISH(STB_NWReceive);

   return(retval);
}

S32BIT STB_NWReceiveFrom(void *socket, U8BIT *buf, U32BIT max_bytes, U8BIT *address, U32BIT *port)
{
   S_SOCKET_DESC *socket_desc;
   S32BIT retval = -1;
   struct sockaddr_in addr;
   struct sockaddr_in *addr_p;
   socklen_t addr_len;
   ssize_t r;
   char *a;

   FUNCTION_START(STB_NWReceiveFrom);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;

      if ((address == NULL) || (port == NULL))
      {
         /* Receive for a connected socket */
         addr_p = NULL;
      }
      else
      {
         addr_p = &addr;
      }

      r = recvfrom(socket_desc->socket, (void *)buf, (size_t)max_bytes, 0, (struct sockaddr *)addr_p,
            &addr_len);
      retval = (S32BIT)r;
      if (r >= 0)
      {
         if (addr_p != NULL)
         {
            a = inet_ntoa(addr.sin_addr);
            if (a != NULL)
            {
               strcpy((char *)address, a);
            }
            *port = ntohs(addr.sin_port);
         }
      }
      else
      {
         NET_ERR(("STB_NWReceiveFrom: recvfrom error, errno = %d", errno));
      }
   }
   else
   {
      NET_ERR(("STB_NWReceiveFrom: socket id NULL"));
   }

   FUNCTION_FINISH(STB_NWReceiveFrom);

   return retval;
}

S32BIT STB_NWSendTo(void *socket, U8BIT *buf, U32BIT num_bytes,
   U8BIT *address, U32BIT port)
{
   S_SOCKET_DESC *socket_desc;
   S32BIT retval = -1;
   struct sockaddr_in addr;

   FUNCTION_START(STB_NWSendTo);

   if (socket != NULL)
   {
      socket_desc = (S_SOCKET_DESC *)socket;

      memset(&addr, 0, sizeof(struct sockaddr_in));
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = inet_addr((char *)address);
      addr.sin_port = htons(port);
      retval = sendto(socket_desc->socket, buf, num_bytes, 0,
            (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
   }

   FUNCTION_FINISH(STB_NWSendTo);

   return retval;
}

BOOLEAN STB_NWSockIsSet(void *socket, S_NW_SOCKSET *socks)
{
   BOOLEAN retval = FALSE;
   U16BIT i = 0;

   FUNCTION_START(STB_NWSockIsSet);

   if ((socket != NULL) && (socks != NULL))
   {
      while (!retval && (i < socks->sock_count))
      {
         if (socks->sock_array[i] == socket)
         {
            retval = TRUE;
         }
         else
         {
            i++;
         }
      }
   }

   FUNCTION_FINISH(STB_NWSockIsSet);

   return retval;
}

void STB_NWSockZero(S_NW_SOCKSET *socks)
{
   FUNCTION_START(STB_NWSockIsSet);

   if (socks != NULL)
   {
      socks->sock_count = 0;
   }

   FUNCTION_FINISH(STB_NWSockIsSet);
}

void STB_NWSockClear(void *socket, S_NW_SOCKSET *socks)
{
   U16BIT i;

   FUNCTION_START(STB_NWSockIsSet);

   if ((socket != NULL) && (socks != NULL))
   {
      for (i = 0; i < socks->sock_count; ++i)
      {
         if (socks->sock_array[i] == socket)
         {
            while (i < socks->sock_count - 1)
            {
               socks->sock_array[i] = socks->sock_array[i + 1];
               ++i;
            }
            --socks->sock_count;
            break;
         }
      }
   }

   FUNCTION_FINISH(STB_NWSockIsSet);
}

void STB_NWSockSet(void *socket, S_NW_SOCKSET *socks)
{
   FUNCTION_START(STB_NWSockIsSet);

   if ((socket != NULL) && (socks != NULL))
   {
      if (socks->sock_count < SOCK_SETSIZE)
      {
         socks->sock_array[socks->sock_count] = socket;
         ++socks->sock_count;
      }
   }

   FUNCTION_FINISH(STB_NWSockIsSet);
}

S32BIT  STB_NWSelect(S_NW_SOCKSET *read_sockets, S_NW_SOCKSET *write_sockets,
   S_NW_SOCKSET *except_sockets, S32BIT timeout_ms)
{
   S_SOCKET_DESC *socket_desc;
   S32BIT retval;
   U16BIT i;
   struct timeval *timeout_p;
   struct timeval timeout;
   fd_set readfds;
   fd_set *readfds_p = NULL;
   fd_set writefds;
   fd_set *writefds_p = NULL;
   fd_set exceptfds;
   fd_set *exceptfds_p = NULL;
   int nfds;

   FUNCTION_START(STB_NWSelect);

   if (timeout_ms >= 0)
   {
      timeout.tv_sec = timeout_ms / 1000;
      timeout.tv_usec = (timeout_ms % 1000) * 1000;
      timeout_p = &timeout;
   }
   else
   {
      timeout_p = NULL;
   }

   nfds = 0;

   /* remap the set of sockets from the OBS API to sets for the socket API */
   FD_ZERO(&readfds);
   if (read_sockets != NULL)
   {
      for (i = 0; i < read_sockets->sock_count; i++)
      {
         socket_desc = (S_SOCKET_DESC *)read_sockets->sock_array[i];
         FD_SET(socket_desc->socket, &readfds);
         if (socket_desc->socket > nfds)
         {
            nfds = socket_desc->socket;
         }
      }
      readfds_p = &readfds;
   }

   FD_ZERO(&writefds);
   if (write_sockets != NULL)
   {
      for (i = 0; i < write_sockets->sock_count; i++)
      {
         socket_desc = (S_SOCKET_DESC *)write_sockets->sock_array[i];
         FD_SET(socket_desc->socket, &writefds);
         if (socket_desc->socket > nfds)
         {
            nfds = socket_desc->socket;
         }
      }
      writefds_p = &writefds;
   }

   FD_ZERO(&exceptfds);
   if (except_sockets != NULL)
   {
      for (i = 0; i < except_sockets->sock_count; i++)
      {
         socket_desc = (S_SOCKET_DESC *)except_sockets->sock_array[i];
         FD_SET(socket_desc->socket, &exceptfds);
         if (socket_desc->socket > nfds)
         {
            nfds = socket_desc->socket;
         }
      }
      exceptfds_p = &exceptfds;
   }

   retval = select(nfds + 1, readfds_p, writefds_p, exceptfds_p, timeout_p);

   if (retval != -1)
   {
      NET_DBG(("select rtn %d", retval));

      /* remove all the socket handles from the OBS API sets that select did not report as ready */
      if (read_sockets != NULL)
      {
         UpdateSocks(read_sockets, readfds_p);
      }

      if (write_sockets != NULL)
      {
         UpdateSocks(write_sockets, writefds_p);
      }

      if (except_sockets != NULL)
      {
         UpdateSocks(except_sockets, exceptfds_p);
      }
   }
#ifdef NETWORK_ERROR
   else
   {
      NET_ERR(("STB_NWSelect: select failed, errno %d", errno));
   }
#endif

   FUNCTION_FINISH(STB_NWSelect);

   return retval;
}

E_NW_LINK_STATUS STB_NWGetLinkStatus(void)
{
   FUNCTION_START(STB_NWGetLinkStatus);
   NET_DBG(("STB_NWGetLinkStatus: %s", ((current_ethernet_status == NW_LINK_ACTIVE) ? "active" :
                                        (current_ethernet_status == NW_LINK_INACTIVE) ? "inactive" : "disabled")));
   FUNCTION_FINISH(STB_NWGetLinkStatus);
   return current_ethernet_status;
}

NW_handle STB_NWStartEthernetMonitor( NW_eth_callback func )
{
   S_NW_ETH_MONITOR *p_mtr;

   FUNCTION_START(STB_NWStartEthernetMonitor);

   if (func == NULL)
   {
      p_mtr = NULL;
   }
   else
   {
      p_mtr = (S_NW_ETH_MONITOR *)STB_MEMGetSysRAM(sizeof(S_NW_ETH_MONITOR));

      if (p_mtr)
      {
         p_mtr->function = func;

         STB_OSMutexLock( nw_mutex );
         p_mtr->next = nw_monitor_list;
         nw_monitor_list = p_mtr;
         STB_OSMutexUnlock( nw_mutex );
      }
   }

   FUNCTION_FINISH(STB_NWStartEthernetMonitor);

   return p_mtr;
}

void STB_NWStopEthernetMonitor( NW_handle hdl )
{
   S_NW_ETH_MONITOR *p_mtr, *prev;

   FUNCTION_START(STB_NWStopEthernetMonitor);

   if (hdl && nw_monitor_list)
   {
      p_mtr = (S_NW_ETH_MONITOR *)hdl;
      STB_OSMutexLock( nw_mutex );
      if (p_mtr == nw_monitor_list)
      {
         nw_monitor_list = p_mtr->next;
      }
      else
      {
         prev = nw_monitor_list;
         while (prev->next)
         {
            if (prev->next == p_mtr)
            {
               prev->next = p_mtr->next;
               break;
            }
            prev = prev->next;
         }
      }
      STB_OSMutexUnlock( nw_mutex );

      STB_MEMFreeSysRAM( hdl );
   }

   FUNCTION_FINISH(STB_NWStopEthernetMonitor);
}

void STB_NWEnableEthernetDevice( BOOLEAN enable )
{
   struct ifreq ifr;
   int sock_fd;

   FUNCTION_START(STB_NWEnableEthernetDevice);

   sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
   if (sock_fd < 0)
   {
      NET_ERR(("EthernetMonitorTask socket FAILURE"));
   }
   else
   {
      strcpy(ifr.ifr_name, "eth0");

      if (ioctl(sock_fd, SIOCGIFFLAGS, &ifr) < 0)
      {
         NET_ERR(("ioctl SIOCGIFFLAGS failed"));
      }
      else
      {
         if (!enable)
         {
            ifr.ifr_flags &= ~IFF_UP;
         }
         else
         {
            ifr.ifr_flags |= IFF_UP;
         }
         if (ioctl(sock_fd, SIOCSIFFLAGS, &ifr) < 0)
         {
            NET_ERR(("ioctl SIOCSIFFLAGS failed"));
         }
      }
      close( sock_fd );
   }

   FUNCTION_FINISH(STB_NWEnableEthernetDevice);
}

/*****************************************************************************
 * @brief   Scans and returns all access points visible on the wireless network
 * @param   access_points - pointer to an array that will be allocated containing
 *                          info on each access point found
 * @return  number of access points found
 *****************************************************************************/
U16BIT STB_NWGetWirelessAccessPoints(S_NW_ACCESS_POINT **access_points)
{
   FUNCTION_START(STB_NWGetWirelessAccessPoints);
   *access_points = NULL;
   FUNCTION_FINISH(STB_NWGetWirelessAccessPoints);
   return(0);
}

/*****************************************************************************
 * @brief   Frees the array of access points returned by STB_NWGetWirelessAccessPoints
 * @param   access_points - array of access points to be freed
 * @param   num_aps - number of access points in the array
 *****************************************************************************/
void STB_NWFreeWirelessAccessPoints(S_NW_ACCESS_POINT *access_points, U16BIT num_aps)
{
   FUNCTION_START(STB_NWFreeWirelessAccessPoints);
   USE_UNWANTED_PARAM(access_points);
   USE_UNWANTED_PARAM(num_aps);
   FUNCTION_FINISH(STB_NWFreeWirelessAccessPoints);
}

/*****************************************************************************
 * @brief   Attempts to connect to the wireless network with the given SSID.
 *          If the network is open then 'password' can be NULL.
 * @param   essid - network to connect to
 * @param   password - password to be used for an encrypted network,
 *                     can be NULL for an open network
 * @return  TRUE if connected successfully to the network, FALSE otherwise
 *****************************************************************************/
BOOLEAN STB_NWConnectToAccessPoint(U8BIT *essid, U8BIT *password)
{
   FUNCTION_START(STB_NWConnectToAccessPoint);
   USE_UNWANTED_PARAM(essid);
   USE_UNWANTED_PARAM(password);
   FUNCTION_FINISH(STB_NWConnectToAccessPoint);
   return(FALSE);
}

/*---local function definitions----------------------------------------------*/

static void EthernetMonitorTask( void *arg )
{
   S_NW_ETH_MONITOR *p_nw_monitor;
   E_NW_LINK_STATUS status_now;
   struct ifreq ifr;
   int sock_fd;
   struct ethtool_value edata;

   USE_UNWANTED_PARAM(arg);

   sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
   if (sock_fd < 0)
   {
      NET_ERR(("EthernetMonitorTask socket FAILURE"));
   }
   else
   {
      strcpy(ifr.ifr_name, "eth0");

      while (nw_eth_status_running)
      {
         if (ioctl(sock_fd, SIOCGIFFLAGS, &ifr) < 0)
         {
            NET_ERR(("ioctl SIOCGIFFLAGS failed"));
            status_now = NW_LINK_DISABLED;
         }
         else
         {
            if (ifr.ifr_flags & (IFF_UP | IFF_RUNNING))
            {
               edata.cmd = ETHTOOL_GLINK;
               ifr.ifr_data = (char *) &edata;
               if (ioctl(sock_fd, SIOCETHTOOL, &ifr) < 0)
               {
                  NET_ERR(("ioctl SIOCETHTOOL failed"));
                  status_now = NW_LINK_DISABLED;
               }
               else
               {
                  status_now = (edata.data) ? NW_LINK_ACTIVE : NW_LINK_INACTIVE;
               }
            }
            else
            {
               status_now = NW_LINK_DISABLED;
            }
         }
         if (current_ethernet_status != status_now)
         {
            current_ethernet_status = status_now;

            NET_DBG(("EthernetMonitorTask: current ethernet status changed to %s",
                     ((current_ethernet_status == NW_LINK_ACTIVE) ? "active" :
                      (current_ethernet_status == NW_LINK_INACTIVE) ? "inactive" : "disabled")));

            STB_OSMutexLock( nw_mutex );
            p_nw_monitor = nw_monitor_list;
            while (p_nw_monitor)
            {
               (p_nw_monitor->function)(NW_WIRED, current_ethernet_status);
               p_nw_monitor = p_nw_monitor->next;
            }
            STB_OSMutexUnlock( nw_mutex );
         }

         STB_OSTaskDelay( 100 );
      }
      close( sock_fd );
   }
}

static int readNlSock(int sockFd, char *bufPtr, unsigned int seqNum, unsigned int pId)
{
   struct nlmsghdr *nlHdr;
   int readLen, msgLen = 0;

   do
   {
      /* Recieve response from the kernel */
      if ((readLen = recv(sockFd, bufPtr, GATEWAY_BUFSIZE - msgLen, 0)) < 0)
      {
         perror("SOCK READ: ");
         return -1;
      }

      nlHdr = (struct nlmsghdr *)bufPtr;

      /* Check if the header is valid */
      if ((NLMSG_OK(nlHdr, readLen) == 0) || (nlHdr->nlmsg_type == NLMSG_ERROR))
      {
         perror("Error in recieved packet");
         return -1;
      }

      /* Check if the its the last message */
      if (nlHdr->nlmsg_type == NLMSG_DONE)
      {
         break;
      }
      else
      {
         /* Else move the pointer to buffer appropriately */
         bufPtr += readLen;
         msgLen += readLen;
      }

      /* Check if its a multi part message */
      if ((nlHdr->nlmsg_flags & NLM_F_MULTI) == 0)
      {
         /* return if its not */
         break;
      }
   }
   while ((nlHdr->nlmsg_seq != seqNum) || (nlHdr->nlmsg_pid != pId));

   return msgLen;
}

/* parse the route info returned */
static void parseRoutes(struct nlmsghdr *nlHdr, struct route_info *rtInfo)
{
   struct rtmsg *rtMsg;
   struct rtattr *rtAttr;
   int rtLen;

   rtMsg = (struct rtmsg *)NLMSG_DATA(nlHdr);

   /* If the route is not for AF_INET or does not belong to main routing table then return. */
   if ((rtMsg->rtm_family != AF_INET) || (rtMsg->rtm_table != RT_TABLE_MAIN))
      return;

   /* get the rtattr field */
   rtAttr = (struct rtattr *)RTM_RTA(rtMsg);
   rtLen = RTM_PAYLOAD(nlHdr);

   for (; RTA_OK(rtAttr, rtLen); rtAttr = RTA_NEXT(rtAttr, rtLen))
   {
      switch (rtAttr->rta_type)
      {
         case RTA_OIF:
            if_indextoname(*(int *)RTA_DATA(rtAttr), rtInfo->ifName);
            break;

         case RTA_GATEWAY:
            memcpy(&rtInfo->gateWay, RTA_DATA(rtAttr), sizeof(rtInfo->gateWay));
            break;

         case RTA_PREFSRC:
            memcpy(&rtInfo->srcAddr, RTA_DATA(rtAttr), sizeof(rtInfo->srcAddr));
            break;

         case RTA_DST:
            memcpy(&rtInfo->dstAddr, RTA_DATA(rtAttr), sizeof(rtInfo->dstAddr));
            break;
      }
   }
}

/* remove socket handles from the OBS API sets that select did not report as ready */
static void UpdateSocks(S_NW_SOCKSET *socks, fd_set *pfds)
{
   S_SOCKET_DESC *socket_desc;
   U16BIT i;
   U16BIT j;

   i = 0;
   while (i < socks->sock_count)
   {
      socket_desc = (S_SOCKET_DESC *)socks->sock_array[i];
      if (FD_ISSET(socket_desc->socket, pfds))
      {
         i++;
      }
      else
      {
         j = i;
         while (j != (socks->sock_count - 1))
         {
            socks->sock_array[j] = socks->sock_array[j + 1];
            ++j;
         }
         --socks->sock_count;
      }
   }
}

/******************************************************************************
 * End of file
 *****************************************************************************/

