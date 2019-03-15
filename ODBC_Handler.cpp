/****************************************************************************/
/* Copyright(C) : Advantech Technologies, Inc.														 */
/* Create Date  : 2016 by Zach Chih															     */
/* Modified Date: 2016/8/15 by Zach Chih															 */
/* Abstract     : ODBC Handler                                   													*/
/* Reference    : None																									 */
/****************************************************************************/

#include "stdafx.h"
#include "odbcinst.h"
#include "susiaccess_handler_api.h"
#include "ODBC_Handler.h"
#include "pthread.h"
#include "util_path.h"
#include "cJSON.h"
#include "unistd.h"
#include "IoTMessageGenerate.h"
#include "ReadINI.h"
#include "ReadODBCDlg.h"
#include <string.h>
#include <stdio.h>
#include "HandlerKernel.h"
#include <Log.h>

//-----------------------------------------------------------------------------
// Defines:
//-----------------------------------------------------------------------------

#define cagent_request_custom 2102 /*define the request ID for V3.0, not used on V3.1 or later*/
#define cagent_custom_action 31002 /*define the action ID for V3.0, not used on V3.1 or later*/
#define DEF_Rec_Folder   "ODBC_Rec" 
#define strVersion "1.1.2"

//-----------------------------------------------------------------------------
// Logger defines:
//-----------------------------------------------------------------------------
#define SAMPLEHANDLER_LOG_ENABLE
//#define DEF_SAMPLEHANDLER_LOG_MODE    (LOG_MODE_NULL_OUT)
//#define DEF_SAMPLEHANDLER_LOG_MODE    (LOG_MODE_FILE_OUT)
#define DEF_SAMPLEHANDLER_LOG_MODE    (LOG_MODE_CONSOLE_OUT|LOG_MODE_FILE_OUT)


LOGHANDLE g_samolehandlerlog = NULL;

#ifdef SAMPLEHANDLER_LOG_ENABLE
#define SampleHLog(level, fmt, ...)  do { if (g_samolehandlerlog != NULL)   \
    WriteLog(g_samolehandlerlog, DEF_SAMPLEHANDLER_LOG_MODE, level, fmt, ##__VA_ARGS__); } while(0)
#else
#define SampleHLog(level, fmt, ...)
#endif


//-----------------------------------------------------------------------------
// Internal Prototypes:
//-----------------------------------------------------------------------------
//
typedef struct{
    pthread_t threadHandler;
    bool isThreadRunning;
}handler_context_t;



//-----------------------------------------------------------------------------
// Variables
//-----------------------------------------------------------------------------
CReadODBCDlg ODBC_handle;

static Handler_info  g_PluginInfo;
static handler_context_t g_AutoReportContex;
static handler_context_t g_TestWriteContex;

static bool g_bAutoReport = false;
static bool g_bTestWrite = false;

static HandlerSendCbf  g_sendcbf = NULL;						// Client Sends information (in JSON format) to Cloud Server	
static HandlerSendCustCbf  g_sendcustcbf = NULL;			    // Client Sends information (in JSON format) to Cloud Server with custom topic	
static HandlerSubscribeCustCbf g_subscribecustcbf = NULL;
static HandlerAutoReportCbf g_sendreportcbf = NULL;				// Client Sends report (in JSON format) to Cloud Server with AutoReport topic
static HandlerSendCapabilityCbf g_sendcapabilitycbf = NULL;		
static HandlerSendEventCbf g_sendeventcbf = NULL;


//-----------------------------------------------------------------------------
// Types
//-----------------------------------------------------------------------------
char *strPluginName = NULL;										//be used to set the customized handler name by module_config.xml

MSG_CLASSIFY_T *g_Capability = NULL;

//-----------
INI_context_t INI_Context;
bool bFind=false; //Find INI file
bool g_bHandleStart=false; //Status of ODBC_Handle
char tablename[100];
char g_FieldName[1000][32];
char g_FieldDataType[1000][32];
int g_Number_Field = 0;

//-----------------------------------------------------------------------------
//Record data
//-----------------------------------------------------------------------------
RecContext_t  g_aRecContext_list[MAX_TABLE];
char g_ODBC_Path[MAX_FILE_PATH]={0};

//-----------------------------------------------------------------------------
// AutoReport:
//-----------------------------------------------------------------------------
cJSON *Report_root=NULL;
int Report_interval=1;
cJSON *Report_first,*Report_second_interval;

//-----------------------------------------------------------------------------
// Function:
//-----------------------------------------------------------------------------
void Handler_Uninitialize();
bool IoTSetValues(MSG_CLASSIFY_T* parentGroup, int diffIndex, bool bCreateDefault);
time_t ConvertToEpoch(char* strDateTime);
MSG_CLASSIFY_T* IoT_AddTimeStamp(MSG_CLASSIFY_T* pNode, time_t opTS);
void RecCSV(int diffIndex);
char *GetRecPath(char *tablename);
bool readREC();


#ifdef _MSC_VER
BOOL WINAPI DllMain(HINSTANCE module_handle, DWORD reason_for_call, LPVOID reserved)
{
    if (reason_for_call == DLL_PROCESS_ATTACH) // Self-explanatory
    {
        printf("DllInitializer\r\n");
        DisableThreadLibraryCalls(module_handle); // Disable DllMain calls for DLL_THREAD_*
        if (reserved == NULL) // Dynamic load
        {
	  // Initialize your stuff or whatever
	  // Return FALSE if you don't want your module to be dynamically loaded
        }
        else // Static load
        {
	  // Return FALSE if you don't want your module to be statically loaded
	  return FALSE;
        }
    }

    if (reason_for_call == DLL_PROCESS_DETACH) // Self-explanatory
    {
        printf("DllFinalizer\r\n");
        if (reserved == NULL) // Either loading the DLL has failed or FreeLibrary was called
        {
	  // Cleanup
	  Handler_Uninitialize();
        }
        else // Process is terminating
        {
	  // Cleanup
	  Handler_Uninitialize();
        }
    }
    return TRUE;
}
#else
__attribute__((constructor))
/**
* initializer of the shared lib.
*/
static void Initializer(int argc, char** argv, char** envp)
{
    printf("DllInitializer\r\n");
}

__attribute__((destructor))
/** 
* It is called when shared lib is being unloaded.
* 
*/
static void Finalizer()
{
    printf("DllFinalizer\r\n");
    Handler_Uninitialize();
}
#endif
//---------------------------------------------------------------------------------------------------------------------
//------------------------------------------------CAPABILITY_GET_SET_UPLOAD--------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
void ParseFieldName()
{
    char tempFieldName[MaxIniStr];
    char* pch = NULL;
    int loc_sep = 0;
    int i = 0;

    strcpy(tempFieldName, "");

    if(INI_Context.Field_Name != "")
    {
        strcpy(tempFieldName, INI_Context.Field_Name);

      while (tempFieldName[0] != 0)
      {
	  pch = strchr(tempFieldName, ',');
	  loc_sep = pch - tempFieldName;

		  if(loc_sep > 0)
		  {
			  strncpy(g_FieldName[i], tempFieldName, loc_sep);
			  strcpy(tempFieldName, pch+1);
			  i++;
		  }
		  else
		  {
			  strcpy(g_FieldName[i], tempFieldName);
			  strcpy(tempFieldName, "");
			  i++;
		  }
       }
	  g_Number_Field = i;
    }
}


void ParseFieldDataType()
{
    char tempFieldDataType[MaxIniStr];
    char* pch = NULL;
    int loc_sep = 0;
    int i = 0;

    strcpy(tempFieldDataType, "");

    if(INI_Context.Field_Name != "")
    {
        strcpy(tempFieldDataType, INI_Context.Field_DataType);

        while (tempFieldDataType[0] != 0)
        {
		  pch = strchr(tempFieldDataType, ',');
		  loc_sep = pch - tempFieldDataType;

		  if(loc_sep > 0)
		  {
			  strncpy(g_FieldDataType[i], tempFieldDataType, loc_sep);
			  strcpy(tempFieldDataType, pch+1);
			  i++;
		  }
		  else
		  {
			  strcpy(g_FieldDataType[i], tempFieldDataType);
			  strcpy(tempFieldDataType, "");
			  i++;
		  }
        }
		if (i<g_Number_Field)
			g_Number_Field = i;
    }

}

static void ReportNewData(bool bFirstCheck)
{
    int i=0,j=0,k=0;
    int max_diff_num=0;
    char * repJsonStr = NULL;


    ODBC_handle.CheckDiff(g_aRecContext_list, bFirstCheck);

    for(i=0;i<ODBC_handle.CEdb.table_num;i++)
    {
        if(ODBC_handle.CEdb.table_info_list[i].diff_flag)
        {
	  if(max_diff_num<ODBC_handle.CEdb.table_info_list[i].diff_num)
	      max_diff_num=ODBC_handle.CEdb.table_info_list[i].diff_num;
        }
    }

    if(max_diff_num==0)
    {
        return;
    }

    //for(i=0;i<max_diff_num;i++) // send from big number to small number
    for(i=max_diff_num-1;i>=0;i--)
    {
        MSG_CLASSIFY_T *myReport = IoT_CreateRoot((char*) strPluginName);	

        if(bFind)
        {
	  IoTSetValues(myReport, i, false);		
        }

        if(i == 0)
        {  
	  RecCSV(i);
        }

        if(myReport)
        {
	  IoT_ReleaseAll(myReport);
	  myReport = NULL;
        }
    }

    ODBC_handle.UpdateRecordNum();
}

void SendServer(MSG_CLASSIFY_T *myReport)
{
    int size = 9;
    char* filter[] = { "n", "bn", "v","sv","bv","id","StatusCode","sessionID", "$date" };
    char * repJsonStr = NULL;
    TRY{
        repJsonStr = MSG_PrintWithFiltered(myReport, filter, size);
        printf("Handler_Report=%s\n",repJsonStr);
        printf("---------------------\n");

        if(g_sendreportcbf)
        {
			g_sendreportcbf(&g_PluginInfo, repJsonStr, strlen(repJsonStr), NULL, NULL);
        }

        if(repJsonStr)
			free(repJsonStr);

		Sleep(INI_Context.update_interval);
    }
    CATCH (CDBException, e){
        //CReadODBCLog(g_loghandle, Error, "CReadODBCDlg - %s",e->m_strError);
        printf("\n SendServer - %s \n", e->m_strError);	
    }
    END_CATCH;
}

/*callback function to handle threshold rule check event*/
void on_threshold_triggered(threshold_event_type type, char* sensorname, double value, MSG_ATTRIBUTE_T* attr, void *pRev)
{
    SampleHLog(Debug, " %s> threshold triggered:[%d, %s, %f]", g_PluginInfo.Name, type, sensorname, value);
}

//--------------------------------------------------------------------------------------------------------------
//------------------------------------------------Threads-------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------
/*static void* ReadThreadStart(void *args)
{

handler_context_t *pHandlerContex = (handler_context_t *)args;
if(pHandlerContex->isThreadRunning)
RED.Start();
return 0;

}*/
static void*  AutoReportThreadStart(void *args)
{
    handler_context_t *pHandlerContex = (handler_context_t *)args;

    while (g_PluginInfo.agentInfo->status == 0)
    {
        if(!pHandlerContex->isThreadRunning)
	  return 0;
        sleep(1);
    }

    while(!g_bHandleStart)
    {
        g_bHandleStart = ODBC_handle.Start(INI_Context);
    }
	
    ReportNewData(true);
    while(g_AutoReportContex.isThreadRunning)
    {

        sleep(Report_interval);
#pragma region g_bAutoReport
        if(g_bAutoReport)
        {	
	  printf("\nAutoReport...........\n");

	  if(g_bHandleStart)
	      ReportNewData(false);
        }
        else
	  sleep(1);
#pragma endregion g_bAutoReport
    }
    printf("AutoReportThreadStart leave \r\n");	
    return 0;
}
//------------------------------------------------------------------------------------------------------------------------------------------------
//------------------------Note : Can't execute SQL on the same database instance of same source at same time with Autoreport-ReportNewData()
//------------------------In doing so, agent will crash
//------------------------However, the way different database instance of same source can work well.
//------------------------This can be proven by using exeexcel-write in code mdb with this Autoreport-ReportNewData().
//------------------------------------------------------------------------------------------------------------------------------------------------
/*static void* TestWriteThreadStart(void *args)
{
handler_context_t *pHandlerContex = (handler_context_t *)args;
int i=7664;
CString strCmd;
char cstrCmd[512];

while (g_PluginInfo.agentInfo->status == 0)
{
if(!pHandlerContex->isThreadRunning)
return 0;
sleep(1);
}

while(g_TestWriteContex.isThreadRunning)
{
if(g_bTestWrite)
{
sprintf(cstrCmd,"INSERT INTO ErrorMessage VALUES (%d,1,1,1,1,1,1,1,1,1,1,1,1,1,1)",i);
printf("%s\n",cstrCmd);
strCmd.Format(_T(cstrCmd));
i++;
if(ODBC_handle.CEdb.IsOpen())
ODBC_handle.CEdb.ExecuteSQL(strCmd);			
}
sleep(10);

}

return 0;

}*/


//==========================================================
bool read_INI()
{
    char modulePath[200]={0};
    char iniPath[200]={0};
    int i=0;

    FILE *fPtr;

    char *temp_INI_name=NULL;

    temp_INI_name=(char *)calloc(strlen(strPluginName)+1+4,sizeof(char));	//+4 for ".ini"
    strcpy(temp_INI_name,strPluginName);
    strcat(temp_INI_name,".ini");
    // Load ini file
    util_module_path_get(modulePath);
    util_path_combine(iniPath,modulePath,temp_INI_name);

    printf("iniFile: %s\n",iniPath);

    fPtr = fopen(iniPath, "r");
    if (fPtr) {
        printf("INI Opened Successfully...\n");
        bFind=true;

        strcpy(INI_Context.ODBC_Driver,GetIniKeyString("Setting","ODBCDriver",iniPath));

        if(GetIniKeyInt("Setting","ReadOnly",iniPath)==0)
		  INI_Context.bRead_Only=false;
        else
		  INI_Context.bRead_Only=true;

		INI_Context.update_interval = GetPrivateProfileInt("Setting","UpdateInterval",10,iniPath);
		if (INI_Context.update_interval >1000)
			 INI_Context.update_interval = 1000;

        if(strstr(INI_Context.ODBC_Driver,"Excel")!=NULL)
        {
		  strcpy(INI_Context.File_Path,GetIniKeyString("Excel","FilePath",iniPath));
        }
        else if(strstr(INI_Context.ODBC_Driver,"Access")!=NULL)
        {
		  GetPrivateProfileString("Access","FilePath","null",INI_Context.File_Path,sizeof(INI_Context.File_Path),iniPath);
		  //strcpy(INI_Context.File_Path,GetIniKeyString("Access","FilePath",iniPath));
		  GetPrivateProfileString("Access","Uid","null",INI_Context.Uid,sizeof(INI_Context.Uid),iniPath);
		  //strcpy(INI_Context.Uid,GetIniKeyString("Access","Uid",iniPath));
		  GetPrivateProfileString("Access","Pwd","null",INI_Context.Pwd,sizeof(INI_Context.Pwd),iniPath);
		  //strcpy(INI_Context.Pwd,GetIniKeyString("Access","Pwd",iniPath));
		  GetPrivateProfileString("Access","FieldName","null",INI_Context.Field_Name,sizeof(INI_Context.Field_Name),iniPath);
		  //strcpy(INI_Context.Field_Name,GetIniKeyString("Access","FieldName",iniPath));
		  GetPrivateProfileString("Access","FieldDataType","null",INI_Context.Field_DataType,sizeof(INI_Context.Field_DataType),iniPath);
		  //strcpy(INI_Context.Field_DataType,GetIniKeyString("Access","FieldDataType",iniPath));
        }
        else if(strstr(INI_Context.ODBC_Driver,"MySQL")!=NULL)
        {
		  GetPrivateProfileString("MySQL","Server","null",INI_Context.Server,sizeof(INI_Context.Server),iniPath);
		  //strcpy(INI_Context.Server,GetIniKeyString("MySQL","Server",iniPath));
		  GetPrivateProfileString("MySQL","Port","null",INI_Context.Port,sizeof(INI_Context.Port),iniPath);
		  //strcpy(INI_Context.Port,GetIniKeyString("MySQL","Port",iniPath));
		  GetPrivateProfileString("MySQL","DataBase","null",INI_Context.DataBase,sizeof(INI_Context.DataBase),iniPath);
		  //strcpy(INI_Context.DataBase,GetIniKeyString("MySQL","DataBase",iniPath));
		  GetPrivateProfileString("MySQL","Uid","null",INI_Context.Uid,sizeof(INI_Context.Uid),iniPath);
		  //strcpy(INI_Context.Uid,GetIniKeyString("MySQL","Uid",iniPath));
		  GetPrivateProfileString("MySQL","Pwd","null",INI_Context.Pwd,sizeof(INI_Context.Pwd),iniPath);
		  //strcpy(INI_Context.Pwd,GetIniKeyString("MySQL","Pwd",iniPath));	
        }
        else if(strstr(INI_Context.ODBC_Driver,"PostgreSQL")!=NULL)
        {
		  GetPrivateProfileString("PostgreSQL","Server","null",INI_Context.Server,sizeof(INI_Context.Server),iniPath);
		  //strcpy(INI_Context.Server,GetIniKeyString("PostgreSQL","Server",iniPath));
		  GetPrivateProfileString("PostgreSQL","Port","null",INI_Context.Port,sizeof(INI_Context.Port),iniPath);
		  //strcpy(INI_Context.Port,GetIniKeyString("PostgreSQL","Port",iniPath));
		  GetPrivateProfileString("PostgreSQL","DataBase","null",INI_Context.DataBase,sizeof(INI_Context.DataBase),iniPath);
		  //strcpy(INI_Context.DataBase,GetIniKeyString("PostgreSQL","DataBase",iniPath));
		  GetPrivateProfileString("PostgreSQL","Uid","null",INI_Context.Uid,sizeof(INI_Context.Uid),iniPath);
		  //strcpy(INI_Context.Uid,GetIniKeyString("PostgreSQL","Uid",iniPath));
		  GetPrivateProfileString("PostgreSQL","Pwd","null",INI_Context.Pwd,sizeof(INI_Context.Pwd),iniPath);
		  //strcpy(INI_Context.Pwd,GetIniKeyString("PostgreSQL","Pwd",iniPath));	
        }
        else if(strstr(INI_Context.ODBC_Driver,"MongoDB")!=NULL)
        {
		  GetPrivateProfileString("MongoDB","HostName","null",INI_Context.Server,sizeof(INI_Context.Server),iniPath);
		  //strcpy(INI_Context.Server,GetIniKeyString("MongoDB","HostName",iniPath));
		  GetPrivateProfileString("MongoDB","Port","null",INI_Context.Port,sizeof(INI_Context.Port),iniPath);
		  //strcpy(INI_Context.Port,GetIniKeyString("MongoDB","Port",iniPath));
		  GetPrivateProfileString("MongoDB","DataBase","null",INI_Context.DataBase,sizeof(INI_Context.DataBase),iniPath);
		  //strcpy(INI_Context.DataBase,GetIniKeyString("MongoDB","DataBase",iniPath));
		  GetPrivateProfileString("MongoDB","Uid","null",INI_Context.Uid,sizeof(INI_Context.Uid),iniPath);
		  //strcpy(INI_Context.Uid,GetIniKeyString("MongoDB","Uid",iniPath));
		  GetPrivateProfileString("MongoDB","Pwd","null",INI_Context.Pwd,sizeof(INI_Context.Pwd),iniPath);
		  //strcpy(INI_Context.Pwd,GetIniKeyString("MongoDB","Pwd",iniPath));	
        }
        else if(strstr(INI_Context.ODBC_Driver,"Text")!=NULL)
        {
		  GetPrivateProfileString("Text","FilePath","null",INI_Context.File_Path,sizeof(INI_Context.File_Path),iniPath);
		  GetPrivateProfileString("Text","FieldName","null",INI_Context.Field_Name,sizeof(INI_Context.Field_Name),iniPath);
		  GetPrivateProfileString("Text","FieldDataType","null",INI_Context.Field_DataType,sizeof(INI_Context.Field_DataType),iniPath);
        }
        fclose (fPtr);
        free(temp_INI_name);
        return true;
    }
    else {
        printf("INI Opened Fail...\n");
        bFind=false;
        free(temp_INI_name);
        return false;
    }
    return true;

}

//Load record last CSV data
bool readREC()
{	  
    FILE *fPtr;
    char CSVname[MAX_NAME]={0};
    char RecPath[MAX_FILE_PATH] = {0};

    for(int i = 0; i < ODBC_handle.CEdb.table_num; i++)
    {
		if (i >= MAX_TABLE)
			return true;

        memset(CSVname,0,MAX_NAME);
        memset(RecPath,0,MAX_FILE_PATH);

        strcpy(CSVname, ODBC_handle.CEdb.table_info_list[i].table_name);
        strcpy(CSVname, strtok(CSVname, "."));
        sprintf(RecPath,"%s",GetRecPath(CSVname));

		fPtr = fopen(RecPath, "r");
        if (fPtr) {
	       strcpy(g_aRecContext_list[i].table_name,ODBC_handle.CEdb.table_info_list[i].table_name);
	       g_aRecContext_list[i].index = GetIniKeyInt(CSVname,"index",RecPath);
		   strcpy(g_aRecContext_list[i].time, GetIniKeyString(CSVname,"time",RecPath));
	       fclose (fPtr);
        }
        else{
			strcpy(g_aRecContext_list[i].table_name, ODBC_handle.CEdb.table_info_list[i].table_name);
			g_aRecContext_list[i].index = 0;
			printf("Rec Opened Fail...\n");
        }	
    }
    return true;
}

char *GetRecPath(char *tablename)
{	char RecPath[256] = {0};

sprintf(RecPath,"%s\\%s_Rec",g_ODBC_Path,tablename);

return RecPath;

}

void fileput(char *mode, char *data, char *RecPath)
{
    FILE *fRec;
    fRec = fopen(RecPath, mode);
    if (fRec) {
        fputs(data, fRec);
        fclose (fRec);	    
    }
    else
        printf("Write Rec  Fail...\n");
}

//Record last CSV data
void RecCSV(int diffIndex)
{
    char CSVname[MAX_NAME]={0};
    char recpath[MAX_FILE_PATH]={0};

    for (int i = 0; i < ODBC_handle.CEdb.table_num; i++)
    {
        if (ODBC_handle.CEdb.table_info_list[i].diff_flag && i < MAX_TABLE )
        {
		  memset(CSVname,0,MAX_NAME);
		  memset(recpath,0,MAX_FILE_PATH);
		  strcpy(CSVname, ODBC_handle.CEdb.table_info_list[i].table_name);
		  strcpy(CSVname, strtok(CSVname, "."));
		  sprintf(recpath,"%s",GetRecPath(CSVname));

		  char strtemp[1024];
		  sprintf(strtemp, "[%s]\n",  CSVname); 
		  fileput("w", strtemp, recpath); // Use "w" to clear old data

		  sprintf(strtemp, "index=%d\n",  ODBC_handle.CEdb.table_info_list[i].new_record_num);
		  fileput("a", strtemp, recpath);

		  //record last timestamp of each table
		  sprintf(strtemp, "time=%s\n",  ODBC_handle.CEdb.table_info_list[i].diff_record[diffIndex].value[0]);
		  fileput("a", strtemp, recpath);
        }
    }
}

MSG_CLASSIFY_T * CreateCapability()
{
    MSG_CLASSIFY_T *myCapability = IoT_CreateRoot((char*) strPluginName);
    MSG_CLASSIFY_T *myGroup,*myGroup_2;
    MSG_ATTRIBUTE_T* attr;
    IoT_READWRITE_MODE mode=IoT_READONLY;

    int i=0,j=0;

    if(bFind)
    {
        IoTSetValues(myCapability, 0, true);
    }

    return myCapability;
}



//1.Create Json tree in parentGroup
//2.bCreateDfault : true = Create  and insert null value in parentGroup
//			         false = Create and insert value in parentGroup
//3.Send data to server :  SendServer(parentGroup) when the last field
//4.delete table group in parentGroup after send data to server: IoT_DelGroup(subGroup, tablename)
bool IoTSetValues(MSG_CLASSIFY_T* parentGroup, int diffIndex, bool bCreateDefault)
{
	bool retValue = true;
	int i, j, k;
	char* pstr = NULL;
	char* pfilename = NULL;
	time_t epochTime;
	char* t_fieldname = NULL;
	char* t_fieldtype = NULL;
	char tempFieldName[2048];

	MSG_ATTRIBUTE_T* attr;
	MSG_CLASSIFY_T* subGroupData;
	MSG_CLASSIFY_T* subGroup;
	IoT_READWRITE_MODE mode = IoT_READONLY;

	subGroup = IoT_FindGroup(parentGroup, "info");
	if (subGroup == NULL)
	{
		subGroup = IoT_AddGroup(parentGroup, "info");
	}

	if(subGroup)
	{
		mode = IoT_READONLY;

		attr = IoT_FindSensorNode(subGroup, "name");
		if (attr == NULL)
		{
			attr = IoT_AddSensorNode(subGroup, "name");
		}
		if (attr)
			IoT_SetStringValue(attr, strPluginName, mode);

		attr = IoT_FindSensorNode(subGroup, "description");
		if (attr == NULL)
		{
			attr =  IoT_AddSensorNode(subGroup, "description");
		}
		if (attr)
			IoT_SetStringValue(attr, "This service is ODBC Service", mode);

		attr = IoT_FindSensorNode(subGroup, "version");
		if(attr == NULL)
		{
			attr = IoT_AddSensorNode(subGroup, "version");
		}
		if (attr)
			IoT_SetStringValue(attr, strVersion, mode);
	}

	subGroup = IoT_FindGroup(parentGroup, "Setting");
	if (subGroup == NULL)
	{
		subGroup = IoT_AddGroup(parentGroup, "Setting");
	}

	if (subGroup)
	{
		mode = IoT_READONLY;
		attr = IoT_AddSensorNode(subGroup, "ODBCDriver");
		if (attr)
			IoT_SetStringValue(attr, INI_Context.ODBC_Driver, mode);

		attr = IoT_AddSensorNode(subGroup, "ReadOnly");
		if (attr)
			IoT_SetBoolValue(attr, INI_Context.bRead_Only, mode);

		if (strstr(INI_Context.ODBC_Driver, "Excel") != NULL)
		{
			attr = IoT_AddSensorNode(subGroup, "FilePath");
			if (attr)
				IoT_SetStringValue(attr, INI_Context.File_Path, mode);
		}
		else if (strstr(INI_Context.ODBC_Driver, "Access") != NULL)
		{
			attr = IoT_AddSensorNode(subGroup, "FilePath");
			if (attr)
				IoT_SetStringValue(attr, INI_Context.File_Path, mode);

			attr = IoT_AddSensorNode(subGroup, "Uid");
			if (attr)
				IoT_SetStringValue(attr, INI_Context.Uid, mode);

			attr = IoT_AddSensorNode(subGroup, "Pwd");
			if (attr)
				IoT_SetStringValue(attr, INI_Context.Pwd, mode);

		}
		else if (strstr(INI_Context.ODBC_Driver, "MySQL") != NULL || strstr(INI_Context.ODBC_Driver, "PostgreSQL") != NULL)
		{
			attr = IoT_AddSensorNode(subGroup, "Server");
			if (attr)
				IoT_SetStringValue(attr, INI_Context.Server, mode);

			attr = IoT_AddSensorNode(subGroup, "Port");
			if (attr)
				IoT_SetStringValue(attr, INI_Context.Port, mode);

			attr = IoT_AddSensorNode(subGroup, "DataBase");
			if (attr)
				IoT_SetStringValue(attr, INI_Context.DataBase, mode);

			attr = IoT_AddSensorNode(subGroup, "Uid");
			if (attr)
				IoT_SetStringValue(attr, INI_Context.Uid, mode);

			attr = IoT_AddSensorNode(subGroup, "Pwd");
			if (attr)
				IoT_SetStringValue(attr, INI_Context.Pwd, mode);
		}
	}

	subGroup = IoT_FindGroup(parentGroup, "Tables");
	if (subGroup == NULL)
	{
		subGroup = IoT_AddGroup(parentGroup, "Tables");
	}

	if (subGroup)
	{
		for (i = 0; i < ODBC_handle.CEdb.table_num; i++)
		{
			mode = IoT_READONLY;
			strcpy(tablename, ODBC_handle.CEdb.table_info_list[i].table_name);
			strcpy(tablename, strtok(tablename, "."));

			subGroupData = IoT_FindGroup(subGroup, tablename);
			if (subGroupData == NULL)
			{
				subGroupData = IoT_AddGroup(subGroup, tablename);								
			}			  

			for (j = 0; j < ODBC_handle.CEdb.table_info_list[i].field_num; j++)
			{
				attr = IoT_FindSensorNode(subGroupData, ODBC_handle.CEdb.table_info_list[i].field_list[j]);
				if (attr == NULL)
				{
					attr = IoT_AddSensorNode(subGroupData, ODBC_handle.CEdb.table_info_list[i].field_list[j]);
				}

				if (attr)
				{
					if (bCreateDefault == true)
					{	
						for(k = 0; k < g_Number_Field; k++)
						{
							if(!strcmp(ODBC_handle.CEdb.table_info_list[i].field_list[j], g_FieldName[k]))
							{
								if(!strcmp(g_FieldDataType[k], "string"))
								{
									IoT_SetStringValue(attr, "", mode);
									break;
								}

								else if(!strcmp(g_FieldDataType[k], "double"))
								{
									IoT_SetDoubleValue(attr, 0, mode, "");
									break;
								}

								else if(!strcmp(g_FieldDataType[k], "boolean"))
								{
									IoT_SetBoolValue(attr, false, mode);
									break;
								}

								else if(!strcmp(g_FieldDataType[k], "timestamp"))
								{
									epochTime = 0;
									IoT_SetStringValue(attr, "", mode);								    
									IoT_AddTimeStamp(parentGroup->sub_list, epochTime);
									break;
								}

								else if(!strcmp(g_FieldDataType[k], "float"))
								{
									IoT_SetFloatValue(attr, 0, mode, "");
									break;
								}
							}						    
						}
					}
					else
					{					
						if (ODBC_handle.CEdb.table_info_list[i].diff_flag && diffIndex < ODBC_handle.CEdb.table_info_list[i].diff_num)
						{
							for(k = 0; k < g_Number_Field; k++)
							{
								if(!strcmp(ODBC_handle.CEdb.table_info_list[i].field_list[j], g_FieldName[k]))  //compare to ini file defined field name & data type
								{								    
									if(!strcmp(g_FieldDataType[k], "float"))
									{
										float value = atof(ODBC_handle.CEdb.table_info_list[i].diff_record[diffIndex].value[j]);
										IoT_SetFloatValue(attr, value, mode, "");
										break;
									}

									else if(!strcmp(g_FieldDataType[k], "double"))
									{
										int value = atoi(ODBC_handle.CEdb.table_info_list[i].diff_record[diffIndex].value[j]);
										IoT_SetDoubleValue(attr, value, mode, "");
										break;
									}

									else if(!strcmp(g_FieldDataType[k], "boolean"))
									{
										bool boolvalue = atoi(ODBC_handle.CEdb.table_info_list[i].diff_record[diffIndex].value[j]);
										IoT_SetBoolValue(attr, boolvalue, mode);
										break;
									}

									else if(!strcmp(g_FieldDataType[k], "timestamp"))
									{
										IoT_SetStringValue(attr, ODBC_handle.CEdb.table_info_list[i].diff_record[diffIndex].value[j], mode);
										epochTime = ConvertToEpoch(ODBC_handle.CEdb.table_info_list[i].diff_record[diffIndex].value[j]);
										IoT_AddTimeStamp(parentGroup->sub_list, epochTime);
										break;
									}

									else if(!strcmp(g_FieldDataType[k], "string"))
									{
										IoT_SetStringValue(attr,ODBC_handle.CEdb.table_info_list[i].diff_record[diffIndex].value[j], mode);
										break;
									}
								} 
								else
								{
									if(k == g_Number_Field-1) //If not define in ini file, delete this field from capability.
									IoT_DelSensorNode(subGroupData, ODBC_handle.CEdb.table_info_list[i].field_list[j]);
								}
							} // for loop k end
						}
						if (j == ODBC_handle.CEdb.table_info_list[i].field_num-1)
						{
								if(ODBC_handle.CEdb.table_info_list[i].diff_flag)
								SendServer(parentGroup);

								IoT_DelGroup(subGroup, tablename);
						}
					}
				}
			}
		}
	}
	return retValue;
}


time_t ConvertToEpoch(char* strTime)
{
    char* pStr = NULL;
    char *delim = " ";
    char *delimDate = "-";
    char *delimTime = ":";
    struct tm tmTime = { 0 };
    int tm_msec;
    time_t timeSinceEpoch;

    sscanf_s(strTime, "%4d/%2d/%2d %2d:%2d:%2d:%3d", &(tmTime.tm_year), &(tmTime.tm_mon), &(tmTime.tm_mday),
        &(tmTime.tm_hour), &(tmTime.tm_min), &(tmTime.tm_sec), &tm_msec);  //(yyyy/MM/dd hh:mm:ss:fff)

    if(tm_msec >= 0 && tm_msec < 1000) //timestamp in milliseconds format 
    {
        tmTime.tm_year -= 1900;  // This is year-1900, so 112 = 2012
        tmTime.tm_mon -= 1;

        timeSinceEpoch = mktime(&tmTime);
        timeSinceEpoch = timeSinceEpoch * 1000+ tm_msec;
    }

    else  //timestamp in seconds format 
    {
        sscanf_s(strTime, "%4d-%2d-%2d %2d:%2d:%2d", &(tmTime.tm_year), &(tmTime.tm_mon), &(tmTime.tm_mday),
	  &(tmTime.tm_hour), &(tmTime.tm_min), &(tmTime.tm_sec));  //(yyyy-MM-dd hh:mm:ss)

        tmTime.tm_year -= 1900;  // This is year-1900, so 112 = 2012
        tmTime.tm_mon -= 1;

        timeSinceEpoch = mktime(&tmTime);
        timeSinceEpoch = timeSinceEpoch * 1000;
    }


    return timeSinceEpoch;
}


MSG_CLASSIFY_T* IoT_AddTimeStamp(MSG_CLASSIFY_T* pNode, time_t opTS)
{
    MSG_CLASSIFY_T *pOPTSNode= NULL;
    MSG_ATTRIBUTE_T* attr;

    if (pNode)
    {
        pOPTSNode = MSG_FindClassify(pNode, "opTS");
        if (!pOPTSNode)
        {
	  pOPTSNode = MSG_AddJSONClassify(pNode, "opTS", NULL, false);
        }

        attr = MSG_FindJSONAttribute(pOPTSNode, "$date");
        if (!attr)
        {
	  attr = MSG_AddJSONAttribute(pOPTSNode, "$date");
        }

        if (attr)
        {
	  //IoT_SetFloatValue(attr, (float)(opTS), IoT_READONLY, NULL);
	  IoT_SetDoubleValue(attr, opTS, IoT_READONLY, NULL);
        }
    }
    return pOPTSNode;
}



//--------------------------------------------------------------------------------------------------------------
//--------------------------------------Handler Functions-------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------
/* **************************************************************************************
*  Function Name: Handler_Initialize
*  Description: Init any objects or variables of this handler
*  Input :  PLUGIN_INFO *pluginfo
*  Output: None
*  Return:  0  : Success Init Handler
*              -1 : Fail Init Handler
* ***************************************************************************************/
int HANDLER_API Handler_Initialize( HANDLER_INFO *pluginfo )
{	
    //return 0;	
    char path[MAX_FILE_PATH]={0};
    strPluginName=(char *)calloc(strlen(pluginfo->Name)+1,sizeof(char));	
    strcpy(strPluginName,pluginfo->Name);

    printf(" >Name: %s\r\n", strPluginName);
    // 2. Copy agent info 
    memcpy(&g_PluginInfo, pluginfo, sizeof(HANDLER_INFO));
    g_PluginInfo.agentInfo = pluginfo->agentInfo;

    // 3. Callback function -> Send JSON Data by this callback function
    g_sendcbf = g_PluginInfo.sendcbf = pluginfo->sendcbf;
    g_sendcustcbf = g_PluginInfo.sendcustcbf = pluginfo->sendcustcbf;
    g_subscribecustcbf = g_PluginInfo.subscribecustcbf = pluginfo->subscribecustcbf;
    g_sendreportcbf = g_PluginInfo.sendreportcbf = pluginfo->sendreportcbf;
    g_sendcapabilitycbf =g_PluginInfo.sendcapabilitycbf = pluginfo->sendcapabilitycbf;
    g_sendeventcbf = g_PluginInfo.sendeventcbf = pluginfo->sendeventcbf;

    g_AutoReportContex.threadHandler = NULL;
    g_AutoReportContex.isThreadRunning = false;


    //--------------------------------------initialize INI_Context
    strcpy(INI_Context.ODBC_Driver,"");
    INI_Context.bRead_Only=false;	//default - r+w
    strcpy(INI_Context.File_Path,"");
    strcpy(INI_Context.Server,"");
    strcpy(INI_Context.Port,"");
    strcpy(INI_Context.DataBase,"");
    strcpy(INI_Context.Uid,"");
    strcpy(INI_Context.Pwd,"");

    memset(g_ODBC_Path,0,sizeof(g_ODBC_Path));
    util_module_path_get(path);
    sprintf(g_ODBC_Path,"%s\%s",path,DEF_Rec_Folder);

    if(_access(g_ODBC_Path, 0) != 0)
        mkdir(g_ODBC_Path);

    return HandlerKernel_Initialize(pluginfo);
}

/* **************************************************************************************
*  Function Name: Handler_Uninitialize
*  Description: Release the objects or variables used in this handler
*  Input :  None
*  Output: None
*  Return:  void
* ***************************************************************************************/
void Handler_Uninitialize()
{

    //if(g_AutoReportContex.threadHandler)
    //{
    //	g_AutoReportContex.isThreadRunning = false;
    //	pthread_join(g_AutoReportContex.threadHandler, NULL);
    //	g_AutoReportContex.threadHandler = NULL;
    //}

    if(strPluginName)
        free(strPluginName);

    HandlerKernel_Uninitialize();

    if(g_Capability)
    {
        IoT_ReleaseAll(g_Capability);
        g_Capability = NULL;
    }
}

/* **************************************************************************************
*  Function Name: Handler_Get_Status
*  Description: Get Handler Threads Status. CAgent will restart current Handler or restart CAgent self if busy.
*  Input :  None
*  Output: char * : pOutStatus       // cagent handler status
*  Return:  handler_success  : Success Init Handler
*			 handler_fail : Fail Init Handler
* **************************************************************************************/
int HANDLER_API Handler_Get_Status( HANDLER_THREAD_STATUS * pOutStatus )
{
    return 0;
}


/* **************************************************************************************
*  Function Name: Handler_OnStatusChange
*  Description: Agent can notify handler the status is changed.
*  Input :  PLUGIN_INFO *pluginfo
*  Output: None
*  Return:  None
* ***************************************************************************************/
void HANDLER_API Handler_OnStatusChange( HANDLER_INFO *pluginfo )
{
    SampleHLog(Debug, " %s> Update Status", strPluginName);

    if (pluginfo)
        memcpy(&g_PluginInfo, pluginfo, sizeof(HANDLER_INFO));
    else
    {
        memset(&g_PluginInfo, 0, sizeof(HANDLER_INFO));
        sprintf_s(g_PluginInfo.Name, sizeof(g_PluginInfo.Name), "%s", strPluginName);
        g_PluginInfo.RequestID = cagent_request_custom;
        g_PluginInfo.ActionID = cagent_custom_action;
    }
}


/* **************************************************************************************
*  Function Name: Handler_Start
*  Description: Start Running
*  Input :  None
*  Output: None
*  Return:  0  : Success Init Handler
*              -1 : Fail Init Handler
* ***************************************************************************************/
int HANDLER_API Handler_Start( void )
{      
   read_INI();
   ParseFieldName();
   ParseFieldDataType();

    if(!g_bHandleStart)
        g_bHandleStart = ODBC_handle.Start(INI_Context);

    readREC();

    g_AutoReportContex.isThreadRunning = true;
    if(pthread_create(&g_AutoReportContex.threadHandler,NULL, AutoReportThreadStart, &g_AutoReportContex) != 0)
    {
        g_AutoReportContex.isThreadRunning = false;
        printf("> start AutoReport thread failed!\r\n");	
        return handler_fail;
    }
    return 0;
}

/* **************************************************************************************
*  Function Name: Handler_Stop
*  Description: Stop the handler
*  Input :  None
*  Output: None
*  Return:  0  : Success Init Handler
*              -1 : Fail Init Handler
* ***************************************************************************************/
int HANDLER_API Handler_Stop( void )
{

    if(g_AutoReportContex.threadHandler)
    {
        g_AutoReportContex.isThreadRunning = false;
        pthread_join(g_AutoReportContex.threadHandler, NULL);
        g_AutoReportContex.threadHandler = NULL;
    }

    ODBC_handle.Stop();

    printf("Handler_Stop \r\n");	

    return 0;
}

/* **************************************************************************************
*  Function Name: Handler_Recv
*  Description: Receive Packet from MQTT Server
*  Input : char * const topic, 
*			void* const data, 
*			const size_t datalen
*  Output: void *pRev1, 
*			void* pRev2
*  Return: None
* ***************************************************************************************/
void HANDLER_API Handler_Recv(char * const topic, void* const data, const size_t datalen, void *pRev1, void* pRev2  )
{
    int cmdID = 0;
    char sessionID[33] = { 0 };
    printf(" >Recv Topic [%s] Data %s\n", topic, (char*)data);
    printf(" >datalen: %d\n", datalen);
	
    /*Parse Received Command*/
    if (HandlerKernel_ParseRecvCMDWithSessionID((char*)data, &cmdID, sessionID) != handler_success)
        return;

    switch (cmdID)
    {
    case hk_auto_upload_req:
        /*start live report*/
        HandlerKernel_LiveReportStart(hk_auto_upload_rep, (char*)data);
        break;
    case hk_set_thr_req:
        /*Stop threshold check thread*/
        HandlerKernel_StopThresholdCheck();
        /*setup threshold rule*/
        HandlerKernel_SetThreshold(hk_set_thr_rep, (char*)data);
        /*register the threshold check callback function to handle trigger event*/
        HandlerKernel_SetThresholdTrigger(on_threshold_triggered);
        /*Restart threshold check thread*/
        HandlerKernel_StartThresholdCheck();
        break;
    case hk_del_thr_req:
        /*Stop threshold check thread*/
        HandlerKernel_StopThresholdCheck();
        /*clear threshold check callback function*/
        HandlerKernel_SetThresholdTrigger(NULL);
        /*Delete all threshold rules*/
        HandlerKernel_DeleteAllThreshold(hk_del_thr_rep);
        break;
        //case hk_get_sensors_data_req:
        //	/*Get Sensor Data with callback function*/
        //	HandlerKernel_GetSensorData(hk_get_sensors_data_rep, sessionID, (char*)data, on_get_sensor);
        //	break;
        //case hk_set_sensors_data_req:
        //	/*Set Sensor Data with callback function*/
        //	HandlerKernel_SetSensorData(hk_set_sensors_data_rep, sessionID, (char*)data, on_set_sensor);
        //	break;
	default :
		printf(" cmdID: %d\n", cmdID);

    }



}

/* **************************************************************************************
*  Function Name: Handler_AutoReportStart
*  Description: Start Auto Report
*  Input : char *pInQuery
*  Output: None
*  Return: None
* ***************************************************************************************/
void HANDLER_API Handler_AutoReportStart(char *pInQuery)
{
    Report_root=NULL;
    Report_root = cJSON_Parse(pInQuery);
    if(!Report_root) 
    {
        printf("get root failed !\n");

    }
    else
    {	
        Report_first=cJSON_GetObjectItem(Report_root,"susiCommData");
        if(Report_first)
        {
	  Report_second_interval=cJSON_GetObjectItem(Report_first,"autoUploadIntervalSec");
	  if(Report_second_interval)
	  {	
	      Report_interval=Report_second_interval->valueint;
	  }
        }
    }

    if(Report_root)
        cJSON_Delete(Report_root);

    g_bAutoReport=true;

    HandlerKernel_AutoReportStart(pInQuery);

}

/* **************************************************************************************
*  Function Name: Handler_AutoReportStop
*  Description: Stop Auto Report
*  Input : None
*  Output: None
*  Return: None
* ***************************************************************************************/
void HANDLER_API Handler_AutoReportStop(char *pInQuery)
{	
    g_bAutoReport=false;
}

/* **************************************************************************************
*  Function Name: Handler_Get_Capability
*  Description: Get Handler Information specification. 
*  Input :  None
*  Output: char ** : pOutReply       // JSON Format
*  Return:  int  : Length of the status information in JSON format
*                :  0 : no data need to trans
* **************************************************************************************/
int HANDLER_API Handler_Get_Capability( char ** pOutReply ) // JSON Format
{
    char* result = NULL;
    int len = 0;
    if(!pOutReply) return len;

    /*if(g_Capability)
    {
    IoT_ReleaseAll(g_Capability);
    g_Capability = NULL;
    }*/

    if (!g_Capability)
    {
        g_Capability = CreateCapability();

       // HandlerKernel_SetCapability(g_Capability, false); //170425 add 
    }

    result = IoT_PrintCapability(g_Capability);
    printf("Handler_Get_Capability=%s\n",result);
    printf("---------------------\n");

    len = strlen(result);
    *pOutReply = (char *)malloc(len + 1);
    memset(*pOutReply, 0, len + 1);
    strcpy(*pOutReply, result);      
    free(result);
    return len;
}

