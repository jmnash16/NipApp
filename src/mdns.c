/*******************************************************************************
 * Copyright © 2026 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
 *
 * This file is part of a DTVKit Software Component
 * You are permitted to copy, modify or distribute this file subject to the terms
 * of the DTVKit 1.0 Licence which can be found in licence.txt or at www.dtvkit.org
 *
 * THIS CODE AND INFORMATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 *******************************************************************************/
/**
 * @brief   NippApp DNS-SD/mDNS announcement (TS 103 876 clause 11.1), through avahi-daemon: the
 *          gateway is registered as a _dvbservdsc._tcp service, and as _http._tcp as it is an HTTP
 *          server, on each IPv4 interface of the LAN. Its TXT record gives dvbi_sep, the URL of the
 *          entry points with that interface's address (a literal address: dvbi-client-js only
 *          allows plain HTTP to private addresses), with txtvers, manuf, model and tuners.
 *
 *          The TXT keys are separate strings, as RFC 6763 has them (the clause cites RFC 6763; its
 *          "k1=v1;k2=v2" example is taken as notation). Avahi answers queries, announces, renames
 *          the service on a name clash and says goodbye when the service is withdrawn. Without the
 *          daemon the gateway works as before, and is announced when the daemon starts.
 * @file    mdns.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>

/* STB header files */
#include "techtype.h"
#include "nipphw.h"

#include "nippapp.h"

/*---constant definitions for this file---------------------------------------*/
#define DVB_SERVICE_TYPE      "_dvbservdsc._tcp"
#define HTTP_SERVICE_TYPE     "_http._tcp"
#define ENTRY_POINTS_PATH     "/dvbi/slep.xml"
#define MAX_NAME_LEN          63         /* an instance name is at most 63 bytes (clause 11.1.2.2) */

/*---local (static) variable declarations for this file-----------------------*/
static AvahiThreadedPoll *poll_thread = NULL;
static AvahiClient *client = NULL;
static AvahiEntryGroup *group = NULL;
static char *instance_name = NULL;
static int http_port = 0;
static BOOLEAN waiting_reported = FALSE;

/*---local function prototypes for this file----------------------------------*/
static void ClientCallback(AvahiClient *c, AvahiClientState state, void *user_data);
static void GroupCallback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *user_data);
static void CreateServices(AvahiClient *c);
static BOOLEAN NewClient(void);
static void Rename(void);

/*---global function definitions---------------------------------------------*/

BOOLEAN MDNS_Start(const char *name, int port)
{
   char host[64];
   char default_name[MAX_NAME_LEN + 1];

   http_port = port;
   if ((name == NULL) || (name[0] == '\0'))
   {
      if (gethostname(host, sizeof(host)) != 0)
      {
         strcpy(host, "localhost");
      }
      host[sizeof(host) - 1] = '\0';
      snprintf(default_name, sizeof(default_name), "NippApp on %.52s", host);
      name = default_name;
   }
   instance_name = avahi_strndup(name, MAX_NAME_LEN);

   poll_thread = avahi_threaded_poll_new();
   if ((poll_thread == NULL) || (instance_name == NULL) || !NewClient())
   {
      fprintf(stderr, "NippApp: mDNS: cannot start the announcement\n");
      MDNS_Stop();
      return FALSE;
   }
   if (avahi_threaded_poll_start(poll_thread) < 0)
   {
      fprintf(stderr, "NippApp: mDNS: cannot start the announcement thread\n");
      MDNS_Stop();
      return FALSE;
   }
   return TRUE;
}

void MDNS_Stop(void)
{
   if (poll_thread != NULL)
   {
      avahi_threaded_poll_stop(poll_thread);
   }
   /* freeing the client withdraws the services, which avahi-daemon announces with a goodbye */
   if (client != NULL)
   {
      avahi_client_free(client);
      client = NULL;
      group = NULL;
   }
   if (poll_thread != NULL)
   {
      avahi_threaded_poll_free(poll_thread);
      poll_thread = NULL;
   }
   avahi_free(instance_name);
   instance_name = NULL;
}

/*---local function definitions-----------------------------------------------*/

/* A client that waits for avahi-daemon when it isn't running yet */
static BOOLEAN NewClient(void)
{
   int error;

   client = avahi_client_new(avahi_threaded_poll_get(poll_thread), AVAHI_CLIENT_NO_FAIL, ClientCallback,
      NULL, &error);
   if (client == NULL)
   {
      fprintf(stderr, "NippApp: mDNS: %s\n", avahi_strerror(error));
      return FALSE;
   }
   return TRUE;
}

static void ClientCallback(AvahiClient *c, AvahiClientState state, void *user_data)
{
   (void)user_data;

   switch (state)
   {
      case AVAHI_CLIENT_S_RUNNING:
         waiting_reported = FALSE;
         CreateServices(c);
         break;

      case AVAHI_CLIENT_S_COLLISION:
      case AVAHI_CLIENT_S_REGISTERING:
         /* the host name is changing: register again once it has */
         if (group != NULL)
         {
            avahi_entry_group_reset(group);
         }
         break;

      case AVAHI_CLIENT_CONNECTING:
         if (!waiting_reported)
         {
            printf("NippApp: mDNS: waiting for avahi-daemon\n");
            waiting_reported = TRUE;
         }
         break;

      case AVAHI_CLIENT_FAILURE:
         if (avahi_client_errno(c) == AVAHI_ERR_DISCONNECTED)
         {
            /* the daemon went away: wait for it to come back */
            printf("NippApp: mDNS: avahi-daemon stopped; waiting for it\n");
            avahi_client_free(c);
            client = NULL;
            group = NULL;
            NewClient();
         }
         else
         {
            fprintf(stderr, "NippApp: mDNS: %s\n", avahi_strerror(avahi_client_errno(c)));
         }
         break;
   }
}

static void GroupCallback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *user_data)
{
   (void)user_data;

   switch (state)
   {
      case AVAHI_ENTRY_GROUP_ESTABLISHED:
         printf("NippApp: mDNS: announced as '%s'\n", instance_name);
         break;

      case AVAHI_ENTRY_GROUP_COLLISION:
         /* another service has the name */
         Rename();
         CreateServices(avahi_entry_group_get_client(g));
         break;

      case AVAHI_ENTRY_GROUP_FAILURE:
         fprintf(stderr, "NippApp: mDNS: %s\n",
            avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
         break;

      default:
         break;
   }
}

static void Rename(void)
{
   char *alternative = avahi_alternative_service_name(instance_name);

   printf("NippApp: mDNS: '%s' is taken; using '%s'\n", instance_name, alternative);
   avahi_free(instance_name);
   instance_name = alternative;
}

/* The services, on each IPv4 interface of the LAN, with that interface's address in dvbi_sep */
static void CreateServices(AvahiClient *c)
{
   struct ifaddrs *ifaddr, *ifa;
   char address[INET_ADDRSTRLEN];
   char sep[128];
   char tuners[32];
   AvahiIfIndex index;
   int added, error;

   if (group == NULL)
   {
      group = avahi_entry_group_new(c, GroupCallback, NULL);
      if (group == NULL)
      {
         fprintf(stderr, "NippApp: mDNS: %s\n", avahi_strerror(avahi_client_errno(c)));
         return;
      }
   }
   if (!avahi_entry_group_is_empty(group))
   {
      return;
   }
   if (getifaddrs(&ifaddr) != 0)
   {
      fprintf(stderr, "NippApp: mDNS: cannot list the network interfaces\n");
      return;
   }

   snprintf(tuners, sizeof(tuners), "tuners=DVB-S2/%u", (unsigned)TUN_GetNumTuners());
   added = 0;
   error = 0;
   for (ifa = ifaddr; (ifa != NULL) && (error == 0); ifa = ifa->ifa_next)
   {
      if ((ifa->ifa_addr == NULL) || (ifa->ifa_addr->sa_family != AF_INET) ||
          ((ifa->ifa_flags & (IFF_UP | IFF_RUNNING | IFF_MULTICAST)) != (IFF_UP | IFF_RUNNING | IFF_MULTICAST)) ||
          ((ifa->ifa_flags & IFF_LOOPBACK) != 0))
      {
         continue;
      }
      index = (AvahiIfIndex)if_nametoindex(ifa->ifa_name);
      inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, address, sizeof(address));
      snprintf(sep, sizeof(sep), "dvbi_sep=http://%s:%d" ENTRY_POINTS_PATH, address, http_port);

      error = avahi_entry_group_add_service(group, index, AVAHI_PROTO_INET, 0, instance_name, DVB_SERVICE_TYPE,
         NULL, NULL, (uint16_t)http_port, "txtvers=1", sep, "manuf=DTVKit", "model=NippApp", tuners, NULL);
      if (error == 0)
      {
         error = avahi_entry_group_add_service(group, index, AVAHI_PROTO_INET, 0, instance_name, HTTP_SERVICE_TYPE,
            NULL, NULL, (uint16_t)http_port, "path=/", NULL);
      }
      if (error == 0)
      {
         printf("NippApp: mDNS: %s on %s, %s\n", DVB_SERVICE_TYPE, ifa->ifa_name, sep);
         added++;
      }
   }
   freeifaddrs(ifaddr);

   if (error == AVAHI_ERR_COLLISION)
   {
      Rename();
      avahi_entry_group_reset(group);
      CreateServices(c);
      return;
   }
   if (error != 0)
   {
      fprintf(stderr, "NippApp: mDNS: %s\n", avahi_strerror(error));
      avahi_entry_group_reset(group);
      return;
   }
   if (added == 0)
   {
      printf("NippApp: mDNS: no network interface to announce on\n");
      return;
   }
   error = avahi_entry_group_commit(group);
   if (error < 0)
   {
      fprintf(stderr, "NippApp: mDNS: %s\n", avahi_strerror(error));
   }
}

/******************************************************************************
 * End of file
 *****************************************************************************/
