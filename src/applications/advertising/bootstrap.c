/*
     This file is part of GNUnet.
     (C) 2001, 2002, 2003, 2004 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file advertising/bootstrap.c
 * @brief Cron-jobs that trigger bootstrapping
 *  if we have too few connections.
 *
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_util.h"
#include "gnunet_protocols.h"
#include "gnunet_bootstrap_service.h"

#define HELO_HELPER_TABLE_START_SIZE 64

static CoreAPIForApplication * coreAPI;

static Bootstrap_ServiceAPI * bootstrap;

static PTHREAD_T pt;

static int ptPID;

static int abort_bootstrap = YES;
  
typedef struct {
  HELO_Message ** helos;
  int helosCount;
  int helosLen;
} HeloListClosure;

static void processHELOs(HeloListClosure * hcq) {
  int rndidx;
  int i;
  HELO_Message * msg;

  if (NULL == hcq) {
    BREAK();
    return;
  }
  while ( (abort_bootstrap == NO) &&
	  (hcq->helosCount > 0) ) {
    /* select HELO by random */
    rndidx = randomi(hcq->helosCount);
#if DEBUG_HELOEXCHANGE
    LOG(LOG_DEBUG,
	"%s chose HELO %d of %d\n",
	__FUNCTION__,
	rndidx, hcq->helosCount);
#endif
    msg = (HELO_Message*) hcq->helos[rndidx];
    hcq->helos[rndidx]
      = hcq->helos[hcq->helosCount-1];
    GROW(hcq->helos,
	 hcq->helosCount,
	 hcq->helosCount-1);
    
    coreAPI->injectMessage(&msg->senderIdentity,
			   (char*)msg,
			   HELO_Message_size(msg),
			   NO,
			   NULL);
    FREE(msg);
    if ( (hcq->helosCount > 0) &&
	 (abort_bootstrap == NO) ) {
      /* wait a bit */ 
      int load;
      int nload;
      load = getCPULoad();
      nload = getNetworkLoadUp();
      if (nload > load)
	load = nload;
      nload = getNetworkLoadDown();
      if (nload > load)
	load = nload;
      if (load > 100)
	load = 100;
      gnunet_util_sleep(50 + randomi((load+1)*(load+1)));
    }
  }
  for (i=0;i<hcq->helosCount;i++)
    FREE(hcq->helos[i]);
  GROW(hcq->helos,
       hcq->helosCount,
       0);
}

static void downloadHostlistCallback(const HELO_Message * helo,
				     HeloListClosure * cls) {
  if (cls->helosCount >= cls->helosLen) {
    GROW(cls->helos,
	 cls->helosLen,
	 cls->helosLen + HELO_HELPER_TABLE_START_SIZE);
  }
  cls->helos[cls->helosCount++] = MALLOC(HELO_Message_size(helo));
  memcpy(cls->helos[cls->helosCount-1],
	 helo,
	 HELO_Message_size(helo));
}

static int needBootstrap() {
  /* FIXME: better do it based on % connections with
     respect to connection table size... */
  /* Maybe it should ALSO be based on how many peers
     we know (identity).  
     Sure, in the end it goes to the topology, so
     probably that API should be extended here... */
  return (coreAPI->forAllConnectedNodes(NULL, NULL) < 4);  
}

static void processThread(void * unused) {
  HeloListClosure cls;

  ptPID = getpid();
  cls.helos = NULL;
  while (abort_bootstrap == NO) {    
    while (abort_bootstrap == NO) {
      gnunet_util_sleep(2 * cronSECONDS);
      if (needBootstrap())
	break;
    }
    if (abort_bootstrap != NO)
      break;
    cls.helosLen = 0;
    cls.helosCount = 0;
    bootstrap->bootstrap((HELO_Callback)&downloadHostlistCallback,
			 &cls);
    GROW(cls.helos,
	 cls.helosLen,
	 cls.helosCount);
    processHELOs(&cls);
  }
  ptPID = 0;
}

/**
 * Start using the bootstrap service to obtain
 * advertisements if needed.
 */
void startBootstrap(CoreAPIForApplication * capi) {
  coreAPI = capi;
  bootstrap = capi->requestService("bootstrap");
  GNUNET_ASSERT(bootstrap != NULL);
  abort_bootstrap = NO;
  GNUNET_ASSERT(0 == PTHREAD_CREATE(&pt,
				    (PThreadMain)&processThread,
				    NULL,
				    8 * 1024));	
}

/**
 * Stop advertising.
 * @todo [WIN] Check if this works under Windows
 */
void stopBootstrap() {
  void * unused;

  abort_bootstrap = YES;
#if SOMEBSD || OSX || SOLARIS || MINGW
  PTHREAD_KILL(&pt, SIGALRM);
#else
  /* linux */
  if (ptPID != 0) 
    kill(ptPID, SIGALRM);
#endif
  PTHREAD_JOIN(&pt, &unused);
  coreAPI->releaseService(bootstrap);
  bootstrap = NULL;
  coreAPI = NULL;
}

/* end of bootstrap.c */
