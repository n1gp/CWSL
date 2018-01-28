//
// CWSL_Tee.cpp - DLL Interface for CW Skimmer Server Listener Tee
//
#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "CWSL_Tee.h"
#include "../Utils/SharedMemory.h"

///////////////////////////////////////////////////////////////////////////////
// Global variables

// Mode flags
bool  gMaster = false, gSubMaster = false, gSlave = false;
bool  gMasterSynced = false;

// Name of lower library
char nLib[_MAX_PATH];

// Instance of lower library
HINSTANCE hLib = NULL;

// Types and pointers to functions exported by lower library
typedef void (__stdcall *tGetSdrInfo)(PSdrInfo pInfo);
typedef void (__stdcall *tStartRx)(PSdrSettings pSettings);
typedef void (__stdcall *tStopRx)(void);
typedef void (__stdcall *tSetRxFrequency)(int Frequency, int Receiver);
typedef void (__stdcall *tSetAdc)(int AdcMask);
typedef void (__stdcall *tSetCtrlBits)(unsigned char Bits);
typedef int  (__stdcall *tReadPort)(int PortNumber);
tGetSdrInfo pGetSdrInfo = NULL;
tStartRx pStartRx = NULL;
tStopRx pStopRx = NULL;
tSetRxFrequency pSetRxFrequency = NULL;
tSetAdc pSetAdc = NULL;
tSetCtrlBits pSetCtrlBits = NULL;
tReadPort pReadPort = NULL;

// String with our descritpion
char gDesc[256];

// Info for Skimmer server
SdrInfo gInfo;

// Settings from Skimmer server
SdrSettings gSet;

// Sample rate of Skimmer server
int gSampleRate = 0;

// Length of block for one call of IQProc
int gBlockInSamples = 0;

// Center frequencies for receivers
int gL0[MAX_HW_RX_COUNT];

// ADC bitmask for MAX_HW_RX_COUNT
int gADCMask;

// Max number of receivers the hardware is capable of
int gHwRxCnt = 0;

// Number of current Master mode receivers
int gMastRxCnt = 0;

// Number of current Sub Master mode receivers
int gSubRxCnt = 0;

// Prefix and suffix for the shared memories names
char gPreSM[128], gPostSM[128];

// Shared memories for teeing data, plus 2 for Mast <-> SubMast comm
CSharedMemory gSM[MAX_HW_RX_COUNT + 2];

// Length of shared memories in blocks
int gSMLen = 64;

// Shared memories headers
SM_HDR *gHdr[MAX_HW_RX_COUNT];

// IQ Data buffer
CmplxA gData[MAX_RX_COUNT];

ModeData gModeData;

// Handle & ID of worker thread (Slave/SubMaster mode)
DWORD  idWrk = 0;
HANDLE hWrk = NULL;

// Handle & ID of comm thread (Master/SubMaster mode)
DWORD  idCommWrk = 0;
HANDLE hCommWrk = NULL;

// Stop flag (Slave mode)
volatile bool StopFlag = false;

// Stop flag (Master/SubMaster mode)
volatile bool StopFlagM = false;

///////////////////////////////////////////////////////////////////////////////
// Printing to debug log
void Print(const char *Fmt, ...)
{va_list Args;
 time_t now;
 struct tm *ptm;
 char Line[0x1000];
 FILE *fp;
 
 // create timestamp
 time(&now);
 ptm = localtime(&now);
 strftime(Line, 100, "%d/%m %H:%M:%S ", ptm);

 // format text
 va_start(Args, Fmt);
 vsprintf(Line + strlen(Line), (const char *)Fmt, Args);
 va_end(Args);   
 
 // add new line character
 strcat(Line, "\n");

 // add to log file
 fp = fopen("CWSL_Tee.log", "at");
 if (fp != NULL)
 {// write to file an close it
  fputs(Line, fp);
  fclose(fp);
 }
}

///////////////////////////////////////////////////////////////////////////////
// Send error
void Error(const char *Fmt, ...)
{va_list Args;
 char Line[0x1000];
 
 // for safety
 if (gSet.pErrorProc == NULL) return;
 
 // format text
 va_start(Args, Fmt);
 vsprintf(Line, (const char *)Fmt, Args);
 va_end(Args);   
 
 // save it into log
 Print("Error: %s", Line);

 // send it
 (*gSet.pErrorProc)(gSet.THandle, Line);
}

///////////////////////////////////////////////////////////////////////////////
// Load configuration from file
void LoadConfig(void)
{FILE *fp;
 char ln[_MAX_PATH], *pc;
 int i, j;
 
 // initialize variables to default value
 strcpy(nLib, "Qs1rIntf");
 gSMLen = 64;

 // try to read name from config file
 fp = fopen("CWSL_Tee.cfg", "rt");
 if (fp != NULL)
 {// try to read first line (name of lower library)
  if (fgets(ln, _MAX_PATH - 1, fp))
  {// cut strange chars
   for (pc = ln; *pc != '\0'; pc++)
    if (*pc < ' ') {*pc = '\0'; break;}
    
   // copy name into global variable
   strcpy(nLib, ln);
  
   // try to read second line (length of circular buffers)
   if (fgets(ln, _MAX_PATH - 1, fp))
   {// convert it into integer
    i = atoi(ln);
    
    // if this value is reasonable, assign it
    if ((i > 2) && (i < 1024)) gSMLen = i;
   }
  }

  // try to read third line, ADC mask
  // 0 or 1 per ADC1 or ADC2 for MAX_HW_RX_COUNT
  // Note: order is rx 0 MSbit rx16 LSbit
  if (fgets(ln, _MAX_PATH - 1, fp))
  {
	  for (pc = ln; *pc != '\0'; pc++)
		  if (*pc < ' ') { *pc = '\0'; break; }

	  Print("Assigning ADC bitmask, rx0 is left most: %s", ln);
	  j = strlen(ln);
	  //strcpy(ln, _strrev(_strdup(ln)));
	  for (i = 0; i < j; i++)
		  if (ln[i] == '1') gADCMask += (int)pow(2, i);
	  //Print("gADCMask=%X", gADCMask);

  }

  // close file
  fclose(fp);
 }

 // print configuration variables
 Print("Lower library is \"%s\"", nLib);
 Print("Length of shared memories is %d blocks", gSMLen);
}

///////////////////////////////////////////////////////////////////////////////
// DllMain function
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
 char fileName[_MAX_PATH + 1];
 char *pFN, *pc;

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
        Print("DLL_PROCESS_ATTACH");
        
        // create the prefix and suffix for the shared memories names
        strcpy(gPreSM, "CWSL");
        strcpy(gPostSM, "Band");
        ::GetModuleFileName(hModule, fileName, _MAX_PATH);
        #define BASE_FNAME "CWSL_Tee"
        pFN = strstr(fileName, BASE_FNAME);
        if (pFN != NULL)
        {pFN += strlen(BASE_FNAME);
         for (pc = pFN; (*pc != '\0') && (*pc != '.'); pc++);
         *pc = '\0';
         strcat(gPostSM, pFN);
        }
        Print("Shared memories names template is %s#%s", gPreSM, gPostSM);

        // initialize Slave mode memory pointers
        for (int i = 0; i < MAX_RX_COUNT; i++) {gHdr[i] = NULL; gData[i] = NULL;}

        memset(&gModeData, 0, sizeof(ModeData));
        
        // load configruation from file
        LoadConfig();
        
        // try to load lower library
        hLib = ::LoadLibrary(nLib);
        if (hLib != NULL)
        {// try to find functions exported by lower library
         pGetSdrInfo     = (tGetSdrInfo)    ::GetProcAddress(hLib, "GetSdrInfo");
         pStartRx        = (tStartRx)       ::GetProcAddress(hLib, "StartRx");
         pStopRx         = (tStopRx)        ::GetProcAddress(hLib, "StopRx");
         pSetRxFrequency = (tSetRxFrequency)::GetProcAddress(hLib, "SetRxFrequency");
         pSetCtrlBits    = (tSetCtrlBits)   ::GetProcAddress(hLib, "SetCtrlBits");
         pReadPort       = (tReadPort)      ::GetProcAddress(hLib, "ReadPort");
         pSetAdc         = (tSetAdc)        ::GetProcAddress(hLib, "SetAdc");
        }
        break;
        
	case DLL_THREAD_ATTACH: break;
	case DLL_THREAD_DETACH: break;
	case DLL_PROCESS_DETACH:
	     Print("DLL_PROCESS_DETTACH");
	     if (hLib != NULL) ::FreeLibrary(hLib);
	     hLib = NULL;
		break;
	}
    return TRUE;
}


///////////////////////////////////////////////////////////////////////////////
// Detect working mode
bool DetectMode(void)
{CSharedMemory SM;
 char smName[128], strErr[4096];
 
 // try to open Masters Mode shared memory buffer
 sprintf(smName, "%s%d%s", gPreSM, MAST_COMM_SM, gPostSM);
 
 if (gSM[MAST_COMM_SM].Open(smName))
 {// success -> shared memory exist, so check SubMaster & Slave modes
	 gSM[MAST_COMM_SM].WaitForNewData(2000);
	 if (!gSM[MAST_COMM_SM].Read((PBYTE)&gModeData, sizeof(ModeData), strErr))
	 {
		 Error("SubMode SM exists but cannot be read! (%s)", strErr);
		 return(FALSE);
	 }

	 // try to open Sub Master Mode shared memory buffer
	 sprintf(smName, "%s%d%s", gPreSM, SUBMAST_COMM_SM, gPostSM);
	 if (!gSM[SUBMAST_COMM_SM].IsOpen() && (gSM[SUBMAST_COMM_SM].Open(smName)))
	 {// success -> SubMaster shared memory exists, so we work in Slave mode
		 gSlave = true;
		 Print("SubMaster Slave mode detected");
		 gSM[SUBMAST_COMM_SM].Close();
	 }
	 else if (gModeData.HwRxCount > MAX_RX_COUNT)
	 {
		 gSubMaster = true;
		 gModeData.Change++;
		 Print("SubMaster mode detected");
	 }
	 else
	 {
		 gSlave = true;
		 Print("Slave mode detected");
	 }
	 Print("Detect: MasterRxCount=%d SubMasterRxCount=%d HwRxCount=%d",
		 gModeData.MasterRxCount, gModeData.SubMasterRxCount, gModeData.HwRxCount);

	 gMastRxCnt = gModeData.MasterRxCount;
	 gSubRxCnt = gModeData.SubMasterRxCount;
	 gHwRxCnt = gModeData.HwRxCount;
	 gSM[MAST_COMM_SM].Close();
 }
  else
 {// can't open -> shared memory still don't exist, so we run in Master mode
  gMaster = true;
  Print("Master mode detected");
 } 

 // success
 return(TRUE);
}

///////////////////////////////////////////////////////////////////////////////
// Free alocated
void Free(void)
{int i;

 // free Slave mode data buffers
 for (i = 0; i < MAX_RX_COUNT; i++)
 {if (gData[i] != NULL) free(gData[i]);
  gData[i] = NULL;
 }
 
 // close shared memories
 if (gSubMaster)
 {
	 for (i = gMastRxCnt; i < (gMastRxCnt + gSubRxCnt); i++) gSM[i].Close();
	 gSM[SUBMAST_COMM_SM].Close();
 }
 else
 {
	 for (i = 0; i < MAX_HW_RX_COUNT; i++) gSM[i].Close();
	 gSM[MAST_COMM_SM].Close();
 }
}

///////////////////////////////////////////////////////////////////////////////
// Allocate working buffers
BOOL Alloc(void)
{char Name[32];
 SM_HDR *pHdr;
 int i;
 
 // for safety ...
 Free();

 // decode sample rate
 if (gSet.RateID == RATE_48KHZ) gSampleRate = 48000;
  else
 if (gSet.RateID == RATE_96KHZ) gSampleRate = 96000;
  else
 if (gSet.RateID == RATE_192KHZ) gSampleRate = 192000;
  else
 {// unknown sample rate
  Error("Unknown sample rate (RateID=%d)", gSet.RateID);
  return(FALSE);
 } 

 // compute length of block in samples
 gBlockInSamples = (int)((float)gSampleRate / (float)BLOCKS_PER_SEC);
 
 if (gMaster)
 {
	 // create/open IQ shared memories
	 for (i = 0; i < MAX_HW_RX_COUNT; i++)
	 {// create name of memory
		 sprintf(Name, "%s%d%s", gPreSM, i, gPostSM);

		 if (!gSM[i].Create(Name, gSMLen*gBlockInSamples * sizeof(Cmplx), TRUE))
		 {// can't
			 Error("Can't create IQ shared memory buffer %d", i);
			 return(FALSE);
		 }

		 // fill header
		 pHdr = gSM[i].GetHeader();
		 pHdr->SampleRate = 0;
		 pHdr->BlockInSamples = gBlockInSamples;
		 pHdr->L0 = 0;
	 }

	 sprintf(Name, "%s%d%s", gPreSM, MAST_COMM_SM, gPostSM);
	 if (!gSM[MAST_COMM_SM].Create(Name, gSMLen * sizeof(ModeData), TRUE))
	 {// can't
		 Error("Can't create Masters ModeData shared memory buffer %d", MAST_COMM_SM);
		 return(FALSE);
	 }
 }

 if (gSubMaster)
 {
	 sprintf(Name, "%s%d%s", gPreSM, SUBMAST_COMM_SM, gPostSM);
	 if (!gSM[SUBMAST_COMM_SM].Create(Name, gSMLen * sizeof(ModeData), TRUE))
	 {// can't
		 Error("Can't create SubMaster ModeData shared memory buffer %d", SUBMAST_COMM_SM);
		 return(FALSE);
	 }

	 // open shared memories
	 for (i = gMastRxCnt; i < (gSubRxCnt + gMastRxCnt); i++)
	 {
		 sprintf(Name, "%s%d%s", gPreSM, i, gPostSM);

		 if (!gSM[i].Open(Name))
		 {// can't
			 Error("Can't open shared memory buffer %d", i);
			 return(FALSE);
		 }
	 }
 }

 if (gSlave)
 {
	 // open shared memories
	 for (i = 0; i < (gSubRxCnt + gMastRxCnt); i++)
	 {
		 sprintf(Name, "%s%d%s", gPreSM, i, gPostSM);

		 if (!gSM[i].Open(Name))
		 {// can't
			 Error("Can't open shared memory buffer %d", i);
			 return(FALSE);
		 }

		 // check sample rate
		 gHdr[i] = gSM[i].GetHeader();
		 if (gHdr[i]->SampleRate != gSampleRate)
		 {// can't
			 Error("CWSL has different sample rate for rx%d (%d/%d)",
				 i, gHdr[i]->SampleRate, gSampleRate);
			 return FALSE;
		 }
	 }
 }

 // in SubMaster or Sub/Slave mode ...
 if (!gMaster)
 {// ... allocate data buffers
	 for (i = 0; i < MAX_RX_COUNT; i++)
	 {// try to allocate memory 
		 gData[i] = (CmplxA)calloc(gBlockInSamples, sizeof(Cmplx));
		 if (gData[i] == NULL)
		 {// can't
			 Error("Can't allocate working memory");
			 return FALSE;
		 }
	 }
 }

 // success
 return(TRUE);
}

///////////////////////////////////////////////////////////////////////////////
// Our callback procedure (Master mode)
extern "C" void __stdcall MyIQProc(int RxHandle, CmplxAA Data)
{int i;
 
 // write IQ samples to shared memories
 for (i = 0; i < (gSubRxCnt + gMastRxCnt); i++) 
 {
	 if (!gSM[i].Write((PBYTE)Data[i], gBlockInSamples * sizeof(Cmplx)))
		 Print("MyIQProc: Write FAILED, i=%d", i);
 }
  
 // call lower callback function
 (*gSet.pIQProc)(RxHandle, Data);
}

///////////////////////////////////////////////////////////////////////////////
// Start of SubMaster mode receiving after SubMaster connects
void ReStartRx(bool doStop)
{
	int i;
	SdrSettings gSet2;

	if (!gMaster)
	{
		Print("ERROR: calling ReStartRx !gMaster");
		return;
	}

	Print("ReStartRx starting with % rx's", gMastRxCnt + gSubRxCnt);

	memcpy(&gSet2, &gSet, sizeof(SdrSettings));

	if(doStop) (*pStopRx)();

	gSet2.pIQProc = MyIQProc;
	gSet.RecvCount = gSet2.RecvCount = gMastRxCnt + gSubRxCnt;

	// call lower start function
	(*pStartRx)(&gSet2);

	for (i = 0; i < gSet.RecvCount; i++)
	{
		Print("Calling SetRxFrequency for rx%d = %d", i, gL0[i]);
		gHdr[i] = gSM[i].GetHeader();
		gHdr[i]->SampleRate = gSampleRate;
		gHdr[i]->L0 = gL0[i];
		// call lower functions
		(*pSetRxFrequency)(gL0[i], i);
	}
	if ((pSetAdc != NULL) && gADCMask) (*pSetAdc)(gADCMask);

	Print("ReStartRx stopping");
}

///////////////////////////////////////////////////////////////////////////////
// Comm worker thread function (Master/SubMaster mode)
DWORD WINAPI CommWorker(LPVOID lpParameter)
{
	int r = (gMaster) ? SUBMAST_COMM_SM : MAST_COMM_SM;
	int w = (gMaster) ? MAST_COMM_SM : SUBMAST_COMM_SM;
	int i;
	char strErr[4096];
	ModeData subModeData;
	char smName[128];
	SM_HDR *pHdr;
	bool restartLoop;

	Print("Starting CommWorker thread as %s", (gMaster) ? "Master" : "SubMaster");
	// main loop
	while (!StopFlagM)
	{
		restartLoop = false;

		// try to open Mode shared memory buffer
		sprintf(smName, "%s%d%s", gPreSM, r, gPostSM);
		if (!gSM[r].IsOpen())
		{
			while (!gSM[r].Open(smName))
			{
				Sleep(500);
				gModeData.Change++;
				if (!gSM[w].Write((PBYTE)&gModeData, sizeof(ModeData)))
					Print("Commworker thread: cant write initial Mode data %d", w);
			}
		}
		else
		{
			gModeData.Change++;
			if (!gSM[w].Write((PBYTE)&gModeData, sizeof(ModeData)))
				Print("Commworker thread: cant write initial Mode data %d", w);
		}

		Print("CommWorker thread, opened %s gMastRxCnt=%d", smName, gMastRxCnt);

		// clear all buffers
		gSM[r].ClearBytesToRead();

		while (!StopFlagM && !restartLoop)
		{
			strErr[0] = '\0';

			Sleep(1000);

			// wait for new data
			if (gMaster) gSM[r].WaitForNewData(400);
			else gSM[r].WaitForNewData(500);

			if (!gSM[r].Read((PBYTE)&subModeData, sizeof(ModeData), strErr))
			{// no data 
				if (gMaster)
				{
					if ((GetTickCount() - gSM[r].GetLastWrite()) > 4000)
					{
						Print("SubMaster is no longer there, cleaning up");
						gSM[SUBMAST_COMM_SM].Close();
						for (i = gMastRxCnt; i < MAX_HW_RX_COUNT; i++)
						{
							gHdr[i] = gSM[i].GetHeader();
							gHdr[i]->SampleRate = gHdr[i]->L0 = 0;
						}
						gSubRxCnt = gModeData.SubMasterRxCount = 0;
						(*pStopRx)();
						ReStartRx(false);
						restartLoop = true;
						continue;
					}
				}

				Print("Received Data from %s, strErr=(%s)",
					(gMaster) ? "SubMaster" : "Master", strErr);
				continue;
			}
			else
				gSM[r].ClearBytesToRead();

			gModeData.Change++;
			if (!gSM[w].Write((PBYTE)&gModeData, sizeof(ModeData)))
				Print("Commworker thread: cant write my Mode data2");

			if (memcmp(&gModeData, &subModeData, sizeof(ModeData) != 0))
			{
				if (gMaster)
				{
					for (i = 0; i < MAX_RX_COUNT; i++)
					{
						if (gL0[i + gMastRxCnt] != subModeData.Data[i])
						{
							// the SubMaster sends rx freqs first
							gL0[i + gMastRxCnt] = subModeData.Data[i];
							Print("CommWorker: Setting Rx%d Freq: %d",
								i + gMastRxCnt, gL0[i + gMastRxCnt]);

							pHdr = gSM[i + gMastRxCnt].GetHeader();
							if (pHdr != NULL)
							{
								pHdr->L0 = subModeData.Data[i];
								pHdr->SampleRate = gSampleRate;
							}
						}
					}

					if (gModeData.SubMasterRxCount != subModeData.SubMasterRxCount)
					{
						// then the count, so Master tells HermesIntf to restart
						gSubRxCnt = gModeData.SubMasterRxCount = subModeData.SubMasterRxCount;
						Print("CommWorker: Setting gSubRxCnt = %d, gMastRxCnt = %d gHwRxCnt = %d",
							gSubRxCnt, gMastRxCnt, gHwRxCnt);
						ReStartRx(true);
					}
				}
				else
					gMasterSynced = true;
			}
		}
	}
	// that's all
	return(0);
}

///////////////////////////////////////////////////////////////////////////////
// Worker thread function (Slave and SubMaster mode)
DWORD WINAPI Worker(LPVOID lpParameter)
{bool incomplete = false;
 int i, j, k;
 char strErr[4096];
 
 // for sure ...
 if ((gSubRxCnt + gMastRxCnt) < 1) return(0);

 j = gSubRxCnt + gMastRxCnt;

 Print("Starting Worker thread, top rx count=%d", j);

 while (gSubMaster && !gMasterSynced)
 {
	 //Print("Waiting to sync with Master");
	 Sleep(100);
 }

 gSM[j-1].ClearBytesToRead();

 // wait for new data on highest receiver
 gSM[j-1].WaitForNewData(100);
 
 // main loop
 while (!StopFlag)
 {// reset incomplete flag
  incomplete = false;
 
  // wait for new data on highest receiver
  gSM[j-1].WaitForNewData(100);

  if (gSubMaster)
  {
	  // for every sub master receiver ...
	  for (i = gMastRxCnt; i < j; i++)
	  {// try to read data
		  if (!gSM[i].Read((PBYTE)gData[i - gMastRxCnt], gBlockInSamples * sizeof(Cmplx), strErr))
		  {// no data 
			  incomplete = true;
			  break;
		  }
	  }
  }
  else
  {
	  // for every receiver ...
	  for (i = 0, k = 0; i < j; i++)
	  {// find the corresponding slave rx and try to read data
		  // i.e. make them contiguous in the gData structure
		  if (gHdr[i]->L0 == gL0[k])
		  {
			  if (!gSM[i].Read((PBYTE)gData[k++], gBlockInSamples * sizeof(Cmplx), strErr))
			  {// no data 
				  incomplete = true;
				  break;
			  }
		  }
	  }
  }
 
  // have we all of data ?
  if (StopFlag || incomplete) {
	  Print("Incomplete data !!!");
	  continue;
  }
   
  // send data to skimmer
  (*gSet.pIQProc)(gSet.THandle, gData);
 }
  
 // that's all
 Print("Stopping Worker thread");
 return(0);
}

///////////////////////////////////////////////////////////////////////////////
// Get info about driver
extern "C" CWSL_TEE_API void __stdcall GetSdrInfo(PSdrInfo pInfo)
{
	char *numRxStr;
	char smName[128];
	Print("GetSdrInfo starting with pInfo = %08X", pInfo);
 
 // have we info ?
 if (pInfo == NULL) return;
 
 // have we loaded lower library ?
 if ((hLib != NULL) && 
     (pGetSdrInfo != NULL) && (pStartRx != NULL) && (pStopRx != NULL) &&
     (pSetRxFrequency != NULL) && (pSetCtrlBits != NULL) && (pReadPort != NULL)
    )
 {// yes -> call GetSdrInfo of lower library
  (*pGetSdrInfo)(pInfo);
 
  // modify device name
  if (pInfo->DeviceName != NULL) sprintf(gDesc, "CWSL_Tee on %s", pInfo->DeviceName);
                            else sprintf(gDesc, "CWSL_Tee on Unnamed device");
  pInfo->DeviceName = gDesc;
 
  // do local copy of SdrInfo
  memcpy(&gInfo, pInfo, sizeof(SdrInfo));

  // if the hardware supports > 8 rx's HermesIntf will report it in DeviceName
  if ((numRxStr = strstr(pInfo->DeviceName, "rx")) != NULL) {
	  sscanf(numRxStr, "rx%d", &gHwRxCnt);
  }

  // limit it to our max (Hermes-Lite can do 32 rx's !!)
  gHwRxCnt = (gHwRxCnt > MAX_HW_RX_COUNT) ? MAX_HW_RX_COUNT : gHwRxCnt;
  Print("GetSdrInfo setting gHwRxCnt=%d", gHwRxCnt);
 }
  else
 {
  // see if there is a Master already
  sprintf(smName, "%s%d%s", gPreSM, MAST_COMM_SM, gPostSM);
  if (gSM[MAST_COMM_SM].Open(smName))
  {
	  pInfo->DeviceName = "CWSL_Tee in Sub Master mode";
	  pInfo->MaxRecvCount = MAX_RX_COUNT;
	  pInfo->ExactRates[RATE_48KHZ] = 48e3;
	  pInfo->ExactRates[RATE_96KHZ] = 96e3;
	  pInfo->ExactRates[RATE_192KHZ] = 192e3;
	  gSM[MAST_COMM_SM].Close();
  }
  else // no - report it
  {
	  pInfo->DeviceName = "CWSL_Tee without lower library";
	  pInfo->MaxRecvCount = 0;
  }
 }

 Print("GetSdrInfo stopping");
}


///////////////////////////////////////////////////////////////////////////////
// Start of receiving
extern "C" CWSL_TEE_API void __stdcall StartRx(PSdrSettings pSettings)
{
 Print("StartRx starting with THandle=%d, RecvCount=%d, RateID=%08X, LowLatency=%d, "
       "pIQProc=%08X, pAudioProc=%08X, pStatusBitProc=%08X, pLoadProgressProc=%08X, pErrorProc=%08X",
       pSettings->THandle, pSettings->RecvCount, pSettings->RateID, pSettings->LowLatency,
       pSettings->pIQProc, pSettings->pAudioProc, pSettings->pStatusBitProc, 
       pSettings->pLoadProgressProc, pSettings->pErrorProc
      );

 // have we settings ?
 if (pSettings == NULL) return;
 
 // make a copy of SDR settings
 memcpy(&gSet, pSettings, sizeof(gSet));
 
 // from skimmer server version 1.1 in high bytes is something strange
 gSet.RateID &= 0xFF;

 // detect working mode 
 if (!DetectMode())
	 return;

 // save number of receivers
 if (gMaster)
 {
	 gMastRxCnt = gSet.RecvCount;
	 gSubRxCnt = gModeData.SubMasterRxCount = 0;
 }
 else if (gSubMaster)
	 gSubRxCnt = gSet.RecvCount;

 // allocate buffers 
 if (!Alloc()) 
 {// something wrong ...
  Free();
  return;
 }

 // according to mode ...
 if (gMaster || gSubMaster)
 {// Master -> redirect IQ callback routine
	 if (gMaster)
	 {
		 pSettings->pIQProc = MyIQProc;

		 // call lower function
		 (*pStartRx)(pSettings);
		 if ((pSetAdc != NULL) && gADCMask) (*pSetAdc)(gADCMask);

		 // bring back original callback routine
		 pSettings->pIQProc = gSet.pIQProc;

		 gModeData.HwRxCount = gHwRxCnt;
		 gModeData.MasterRxCount = gMastRxCnt;
		 gModeData.Change++;
		 if (!gSM[MAST_COMM_SM].Write((PBYTE)&gModeData, sizeof(ModeData)))
			 Print("gSM[MAST_COMM_SM].Write FAILED!");
	 }
	 else
	 {
		 gModeData.SubMasterRxCount = gSubRxCnt;
		 gModeData.Change++;
		 if (!gSM[SUBMAST_COMM_SM].Write((PBYTE)&gModeData, sizeof(ModeData)))
			 Print("gSM[SUBMAST_COMM_SM].Write FAILED!");
	 }

	 // Master/SubMaster -> start comm thread
	 StopFlagM = false;
	 hCommWrk = CreateThread(NULL, 0, CommWorker, NULL, 0, &idCommWrk);
	 if (hCommWrk == NULL)
	 {// can't
		 Error("Can't start comm worker thread");
		 return;
	 }
 }
 
 if (!gMaster)
 {// SubMaster or Slave -> start worker thread

	 StopFlag = false;
	 hWrk = CreateThread(NULL, 0, Worker, NULL, 0, &idWrk);
	 if (hWrk == NULL)
	 {// can't
		 Error("Can't start worker thread");
		 return;
	 }
 }
  
 Print("StartRx stopping");
}

///////////////////////////////////////////////////////////////////////////////
// Stop of receiving
extern "C" CWSL_TEE_API void __stdcall StopRx(void)
{
 Print("StopRx starting");

 if (gMaster) (*pStopRx)();

 if (hWrk != NULL)
 {// set stop flag
	 StopFlag = true;

	 // wait for thread
	 WaitForSingleObject(hWrk, 1000);

	 // close thread handle 
	 CloseHandle(hWrk);
	 hWrk = NULL;
 }

 if (hCommWrk != NULL)
 {// stop comm thread
	 StopFlagM = true;

	 // wait for thread
	 WaitForSingleObject(hCommWrk, 1000);

	 // close thread handle 
	 CloseHandle(hCommWrk);
	 hCommWrk = NULL;
	 Print("StopRx: closed ");
 }

 // free all
 Free();

 Print("StopRx stopping");
}

///////////////////////////////////////////////////////////////////////////////
// Set center frequency of Receiver-th receiver
extern "C" CWSL_TEE_API void __stdcall SetRxFrequency(int Frequency, int Receiver)
{SM_HDR *pHdr;
 int i, subRx = Receiver;
 bool found = false;

 Print("SetRxFrequency starting with Frequency=%d, Receiver=%d", Frequency, Receiver);

 // check receiver number
 if ((Receiver < 0) || (Receiver > MAX_RX_COUNT))
 {// bad receiver number
  Error("SetRxFrequency: Unknown receiver with number %d", Receiver);
  return;
 }
 
 if (gSubMaster)
 	 Receiver += gMastRxCnt;
 
 // have this receiver created/opened shared memory ?
 if (!gSM[Receiver].IsOpen())
 {// no -> we can't set or check the frequency
  Error("SetRxFrequency: Receiver with number %d does not have %s shared memory", 
        Receiver, gMaster ? "created" : "opened"
       );
  return;
 }
 
 // according to mode ...
 if (gMaster)
 {// Master -> save it into our variable ...
  gL0[Receiver] = Frequency;
  
  // ... and into shared variable
  pHdr = gSM[Receiver].GetHeader();
  if (pHdr != NULL)
  {
	  pHdr->L0 = Frequency;
	  pHdr->SampleRate = gSampleRate;
  }
 
  // call lower functions
  (*pSetRxFrequency)(Frequency, Receiver);
 }
 else if (gSubMaster)
 {
	 gModeData.Data[subRx] = Frequency;
 }
 else
  {// Slave -> only check the frequency
	  for (i = 0; i < (gSubRxCnt + gMastRxCnt); i++)
	  {
		  // see if we actually have this frequency
		  if (gHdr[i]->L0 == Frequency)
		  {
			  found = true;
			  gL0[Receiver] = Frequency;
		  }
	  }

	  if (!found)
	  {// bad frequency
		  Error("SetRxFrequency: don't have L0=%d for rx%d",Frequency, Receiver);
		  return;
	  }
  }

 Print("SetRxFrequency stopping");
}

///////////////////////////////////////////////////////////////////////////////
//
extern "C" CWSL_TEE_API void __stdcall SetCtrlBits(unsigned char Bits)
{
 Print("SetCtrlBits starting with Bits=%d", Bits);

 // in Master mode ...
 if (gMaster)
 {// ... call lower function
  (*pSetCtrlBits)(Bits);
 }

 Print("SetCtrlBits stopping");
}

///////////////////////////////////////////////////////////////////////////////
//
extern "C" CWSL_TEE_API int __stdcall ReadPort(int PortNumber)
{int Res = 0;

 Print("ReadPort starting with PortNumber=%d", PortNumber);

 // in Master mode ...
 if (gMaster)
 {// ... call lower function
  Res = (*pReadPort)(PortNumber);
 }

 Print("ReadPort stopping");

 // return result
 return(Res);
}
