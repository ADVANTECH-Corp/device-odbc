/****************************************************************************/
/* Copyright(C) : Advantech Technologies, Inc.														 */
/* Create Date  : 2016 by Zach Chih															     */
/* Modified Date: 2016/8/15 by Zach Chih															 */
/* Abstract     : ODBC Handler                                   													*/
/* Reference    : None																									 */
/****************************************************************************/

// ReadODBCDlg.cpp : Implementierungsdatei
//
#include "stdafx.h"
#include "CEDatabase.h"
#include "util_string.h"
#include "util_path.h"
#include "ReadODBCDlg.h"
#include "odbcinst.h"
#include "ODBC_Handler.h"

#include <Log.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <direct.h>


#define TABLE_NAME_LENGTH 50
#define FIELD_NAME_LENGTH 50
#define VALUE_LENGTH 512
#define SQL_LENGTH 256

#define REPORT_ORI true

#define DEF_LOG_NAME    "CReadODBCLog.txt"   //default log file name
#define LOG_ENABLE
//#define DEF_LOG_MODE    (LOG_MODE_NULL_OUT)
//#define DEF_LOG_MODE    (LOG_MODE_FILE_OUT)
#define DEF_LOG_MODE    (LOG_MODE_CONSOLE_OUT|LOG_MODE_FILE_OUT)

#ifdef LOG_ENABLE
#define CReadODBCLog(handle, level, fmt, ...)  do { if (handle != NULL)   \
    WriteLog(handle, DEF_LOG_MODE, level, fmt, ##__VA_ARGS__); } while(0)
#else
#define ReadODBCLog(level, fmt, ...)
#endif

static void* g_loghandle = NULL;

//******************************************************************
// Read that ODBC Sheet
//******************************************************************
// The method OnButton1() and GetODBCDriver() demonstrate how
// an ODBC file can be read. Besides that two more interesting
// features are demonstrated: 
//		1) The use of ODBC without having a complete DSN 
//       installed in the ODBC manager
//    2) The use of CRecordset without having a class 
//       derived from it
//
// But there have to be preparations:
//    You must have an ODBC ODBC Driver installed (you 
//    wouldn´t have guessed..). And there has to be database support,
//    so including <afxdb.h> is really not a bad idea. Last but
//    not least, if you want to determine the full name of that
//    ODBC driver automagically (like I did in GetODBCDriver() )
//    you need "odbcinst.h" to be included also.
//
// And now for the drawbacks: 
//    Feature 1) only works with ODBC Admin V3.51 and higher. 
//    Earlier versions will not be able to use a DSN that actually
//    isn´t installed. 
//    Feature 2) needs to be a readonly, foreward only recset.
//    So any attempts to change the data or to move back will 
//    fail horribly. If you need to do something like that you´re
//    bound to use CRecordset the "usual" way. Another drawback is
//    that the tremendous overhead of CRecordset does in fact make
//    it rather slow. A solution to this would be using the class
//    CSQLDirect contributed by Dave Merner at codeguru´s
//    http://www.codeguru.com/mfc_database/direct_sql_with_odbc.shtml
//
// Corresponding articles:
//    For more stuff about writing into an ODBC file or using a not
//    registered DSN please refer my article
//    http://www.codeguru.com/mfc_database/ODBC_sheets_using_odbc.shtml		
//
// There´s still work to do:
//    One unsolved mystery in reading those files is how to get the
//    data WITHOUT having a name defined for it. That means
//    how can the structure of the data be retrieved, how many 
//    "tables" are in there, and so on. If you have any idea about 
//    that I´d be glad to read it under almikula@EUnet.at (please 
//    make a CC to alexander.mikula@siemens.at)
//
//
// After my article at CodeGuru´s concerning how to write into an ODBC 
// file I got tons of requests about how to read from such a file. 
// Well in fact I do hope this - however enhancable - example sorts 
// out the basic questions.
//
//	Have fun!
//			Alexander Mikula - The Famous CyberRat	
//******************************************************************
char *parser_utf8toansi(const char* str)
{
    int len = 0;
    char *strOutput = NULL;
    if(!IsUTF8(str))
    {
        len = strlen(str)+1;
        strOutput = (char *)malloc(len);
        memcpy(strOutput, str, len);

    }
    else
    {
        char * tempStr=UTF8ToANSI(str);
        len = strlen(tempStr)+1;
        strOutput = (char *)malloc(len);
        memcpy(strOutput, tempStr, len);
        free(tempStr);
        tempStr = NULL;
    }
    return strOutput;	
}

char * parser_ansitoutf8(char* wText)
{
    char * utf8RetStr = NULL;
    int tmpLen = 0;
    if(!wText)
        return utf8RetStr;
    if(!IsUTF8(wText))
    {
        utf8RetStr = ANSIToUTF8(wText);
        tmpLen = !utf8RetStr ? 0 : strlen(utf8RetStr);
        if(tmpLen == 1)
        {
	  if(utf8RetStr) free(utf8RetStr);
	  utf8RetStr = UnicodeToUTF8((wchar_t *)wText);
        }
    }
    else
    {
        tmpLen = strlen(wText)+1;
        utf8RetStr = (char *)malloc(tmpLen);
        memcpy(utf8RetStr, wText, tmpLen);
    }
    return utf8RetStr;
}


// Get the name of the ODBC-ODBC driver
CString CReadODBCDlg::GetODBCDriver(char *ODBC_Driver)
{
    char szBuf[1024];
    WORD cbBufMax = 1000;
    WORD cbBufOut;
    char *pszBuf = szBuf;
    CString sDriver;

    // Get the names of the installed drivers ("odbcinst.h" has to be included )
    if(!SQLGetInstalledDrivers(szBuf,cbBufMax,& cbBufOut))
        return "";

    // Search for the driver...
    do
    {
        //printf("%s\n",pszBuf);
        if( strstr( pszBuf, ODBC_Driver ) != 0 )
        {
	  // Found !
	  sDriver = CString( pszBuf );
	  break;
        }
        pszBuf = strchr( pszBuf, '\0' ) + 1;
    }
    while( pszBuf[1] != '\0' );

    return sDriver;
}

bool CReadODBCDlg::Start(INI_context_t INI_Context)
{
    CString sSql;
    CString sItem;
    CString *sAItem=NULL;
    CString sItem_tmp;
    CString sDriver;
    CString sDsn;
    CString sFile;		// the file name. Could also be something like C:\\Sheets\\WhatDoIKnow.xls

    char *szBuf=NULL;
    char *strContent=NULL;
    char tmp_str[TABLE_NAME_LENGTH];	//presume max size of a table name = 50
    char tmp_sql[SQL_LENGTH];	//presume max size of a sql = 256
    int vir_table_num=0;
    int act_table_num=0;
    int act_index=0;
    int field_num=0;
    int i=0,j=0;

    char moudlePath[MAX_PATH] = {0};

    memset(moudlePath, 0 , sizeof(moudlePath));
    util_module_path_get(moudlePath);
    g_loghandle = InitLog(moudlePath);

    CReadODBCLog(g_loghandle, Normal, "CReadODBCDlg - Start");

    sFile.Format(_T(INI_Context.File_Path));

    // Retrieve the name of the ODBC driver. This is 
    // necessary because Microsoft tends to use language
    // specific names like "Microsoft ODBC Driver (*.xls)" versus
    // "Microsoft ODBC Treiber (*.xls)"
    sDriver = GetODBCDriver(INI_Context.ODBC_Driver);
    if( sDriver.IsEmpty() )
    {
        CReadODBCLog(g_loghandle, Error, "CReadODBCDlg - No Access ODBC driver (%s) found!!",sDriver);
        //AfxMessageBox("CReadODBCDlg - No Access ODBC driver found!!");	//This function will crash agent
        return false;
    }
    else
    {
        CReadODBCLog(g_loghandle, Normal, "CReadODBCDlg - Access ODBC driver (%s) found!!",sDriver);	
    }

    if(strstr(INI_Context.ODBC_Driver,"Excel")!=NULL)
        CEdb.driver_type=EXCEL;
    else if(strstr(INI_Context.ODBC_Driver,"Access")!=NULL)
        CEdb.driver_type=ACCESS;
    else if(strstr(INI_Context.ODBC_Driver,"MySQL")!=NULL)
        CEdb.driver_type=MYSQL;
    else if(strstr(INI_Context.ODBC_Driver,"PostgreSQL")!=NULL)
        CEdb.driver_type=POSTGRESQL;
    else if(strstr(INI_Context.ODBC_Driver,"MongoDB")!=NULL)
        CEdb.driver_type=MONGODB;
    else if(strstr(INI_Context.ODBC_Driver,"Text")!=NULL)
        CEdb.driver_type=TEXT;

    // Create a pseudo DSN including the name of the Driver and the ODBC file
    // so we don´t have to have an explicit DSN installed in our ODBC admin
    // sDsn.Format("ODBC;DRIVER={%s};DSN='';DBQ=%s",sDriver,sFile);
    //sDsn.Format("ODBC;DRIVER={%s}",sDriver); // To enable editing function, add ReadOnly=0 for ODBC Connection.
    switch(CEdb.driver_type)
    {	//ReadOnly only works on EXCEL 
    case EXCEL:
        if(!INI_Context.bRead_Only)
	  sDsn.Format("ODBC;DRIVER={%s};DSN='';DBQ=%s;ReadOnly=0",sDriver,sFile); // To enable editing function, add ReadOnly=0 for ODBC Connection.
        //sDsn.Format("ODBC;DSN=readexcel;DBQ=%s;ReadOnly=0",sFile);	// will load DBQ not the DSN one.
        else
	  sDsn.Format("ODBC;DRIVER={%s};DSN='';DBQ=%s",sDriver,sFile);
        break;
    case ACCESS:
        if(!INI_Context.bRead_Only)
	  sDsn.Format("ODBC;DRIVER={%s};DSN='';DBQ=%s;Uid=%s;Pwd=%s;ReadOnly=0",sDriver,sFile,INI_Context.Uid,INI_Context.Pwd); // To enable editing function, add ReadOnly=0 for ODBC Connection.
        //sDsn.Format("ODBC;DSN=Error;Uid=%s;Pwd=%s;ReadOnly=0",INI_Context.Uid,INI_Context.Pwd);
        else
	  sDsn.Format("ODBC;DRIVER={%s};DSN='';DBQ=%s;Uid=%s;Pwd=%s",sDriver,sFile,INI_Context.Uid,INI_Context.Pwd);
        break;
    case MYSQL:
        if(!INI_Context.bRead_Only)	
	  //Option = 16 for MySQL to stop prompting a window when encountering some problems but may not works on others.
	  sDsn.Format("ODBC;DRIVER={%s};Server=%s;Option=16;Port=%s;DataBase=%s;Uid=%s;Pwd=%s;ReadOnly=0",sDriver,INI_Context.Server,INI_Context.Port,INI_Context.DataBase,INI_Context.Uid,INI_Context.Pwd);
        //sDsn.Format("ODBC;DSN=mydsn;Option=16;DataBase=%s;Uid=%s;Pwd=%s;ReadOnly=0",sDriver,INI_Context.DataBase,INI_Context.Uid,INI_Context.Pwd);
        else
	  sDsn.Format("ODBC;DRIVER={%s};Server=%s;Option=16;Port=%s;DataBase=%s;Uid=%s;Pwd=%s",sDriver,INI_Context.Server,INI_Context.Port,INI_Context.DataBase,INI_Context.Uid,INI_Context.Pwd);
        break;	
    case POSTGRESQL:
        if(!INI_Context.bRead_Only)	
	  sDsn.Format("ODBC;DRIVER={%s};Server=%s;Port=%s;DataBase=%s;Uid=%s;Pwd=%s;ReadOnly=0",sDriver,INI_Context.Server,INI_Context.Port,INI_Context.DataBase,INI_Context.Uid,INI_Context.Pwd);
        //sDsn.Format("ODBC;DSN=pgdsn;Server=%s;Port=%s;DataBase=%s;Uid=%s;Pwd=%s;ReadOnly=0",INI_Context.Server,INI_Context.Port,INI_Context.DataBase,INI_Context.Uid,INI_Context.Pwd);
        //sDsn.Format("ODBC;DSN=pgdsn;ReadOnly=0");
        else
	  sDsn.Format("ODBC;DRIVER={%s};Server=%s;Port=%s;DataBase=%s;Uid=%s;Pwd=%s",sDriver,INI_Context.Server,INI_Context.Port,INI_Context.DataBase,INI_Context.Uid,INI_Context.Pwd);
        break;
    case MONGODB:
        if(!INI_Context.bRead_Only)	
	  //sDsn.Format("ODBC;DRIVER={%s};HostName=%s;Port=%s;DataBase=%s;Uid=%s;Pwd=%s;ReadOnly=0",sDriver,INI_Context.Server,INI_Context.Port,INI_Context.DataBase,INI_Context.Uid,INI_Context.Pwd);
	  //sDsn.Format("ODBC;DRIVER={%s};Server=%s;Port=%s;DataBase=%s;Uid=%s;Pwd=%s;ReadOnly=0",sDriver,INI_Context.Server,INI_Context.Port,INI_Context.DataBase,INI_Context.Uid,INI_Context.Pwd);
	  ;
        else
	  //sDsn.Format("ODBC;DRIVER={%s};HostName=%s;Port=%s;DataBase=%s;Uid=%s;Pwd=%s",sDriver,INI_Context.Server,INI_Context.Port,INI_Context.DataBase,INI_Context.Uid,INI_Context.Pwd);
	  ;
        break;
    case TEXT:
        if(!INI_Context.bRead_Only)	
	  sDsn.Format("ODBC;DRIVER={%s};DBQ=%s;ReadOnly=0;Extensions=asc,csv,tab,txt",sDriver,sFile);
        else
	  sDsn.Format("ODBC;DRIVER={%s};DBQ=%s;Extensions=asc,csv,tab,txt",sDriver,sFile);;
        break;
    default :
        break;
    }


    TRY
    {
        // Open the database using the former created pseudo DSN
        //CString strCmd = "UPDATE Demo_Table SET Field_2='GGGG' WHERE Field_1='aaa'";
        //CString strCmd = "INSERT INTO Demo_Table VALUES ('123','123','123')";														//EXCEL
        //CString strCmd = "INSERT INTO ErrorMessage VALUES (8000,1,1,1,1,1,1,1,1,1,1,1,1,1,1)";																//ACCESS
        //CString strCmd = "INSERT INTO Test VALUES (5,5)";	
        //CString strCmd = "INSERT INTO `world`.`city` (`ID`, `Name`, `CountryCode`, `District`, `Population`) VALUES ('4080', 'kkk', 'AFG', 'kkk', '123')";	//MYSQL
        //CString strCmd = "INSERT INTO mytable VALUES ('5', 'five', 'five_com', '1')";																			//POSTGRESQL
        //CEdb.Open(NULL,false,false,sDsn);	

        printf("-------------------------------------\n");
        //printf("CReadODBCDlg - sDsn : %s\n",sDsn);
        CReadODBCLog(g_loghandle, Normal, "CReadODBCDlg - sDsn : %s",sDsn);
        printf("-------------------------------------\n");
        if(!INI_Context.bRead_Only)
	  CEdb.Open(NULL,false,false,sDsn);
        else
	  CEdb.Open(NULL,false,true,sDsn);	//only works on Access

#pragma region CEdb_IsOpen
        if(CEdb.IsOpen())
        {
	  //printf("CReadODBCDlg - Database Opened Successsful!!\n");
	  CReadODBCLog(g_loghandle, Normal, "CReadODBCDlg - Database Opened Successsful!!");
	  //database.Open("ErrorMessage",false,false,sDsn);	
	  //CEdb.ExecuteSQL(strCmd);

	  CEdb.table_list.RemoveAll();
#pragma region CEdb_GetTableNames					
	  if(CEdb.GetTableNames())
	  {
	      //printf("CReadODBCDlg - Database Getting Tables Successful!!\n");
	      CReadODBCLog(g_loghandle, Normal, "CReadODBCDlg - Database Getting Tables Successful!!");
	      vir_table_num=CEdb.table_list.GetCount();
	      act_table_num=vir_table_num;
	      act_index=0;
	      for(i=0;i<vir_table_num;i++)
	      {
		strcpy(tmp_str,CEdb.table_list.GetAt(i));
		//avoid counting system table.
		if(strstr(INI_Context.ODBC_Driver,"Excel")!=NULL && strstr(tmp_str,"$")!=NULL)
		    act_table_num--;
		if(strstr(INI_Context.ODBC_Driver,"Access")!=NULL && strstr(tmp_str,"MSys")!=NULL)
		    act_table_num--;
		if(	(strstr(INI_Context.ODBC_Driver,"MongoDB")!=NULL) && \
		    (strstr(tmp_str,"SYSTEM")!=NULL || strstr(tmp_str,"BLOCKS")!=NULL || strstr(tmp_str,"LOB")!=NULL || strstr(tmp_str,"PARTS")!=NULL || strstr(tmp_str,"STARTUP")!=NULL) )	//This is not fully proved
		    act_table_num--;

	      }
	      CEdb.table_num=act_table_num;
	      CEdb.table_info_list=(struct Table_Info *)calloc(act_table_num,sizeof(struct Table_Info));
	      for(i=0;i<vir_table_num;i++)
	      {
		strcpy(tmp_str,CEdb.table_list.GetAt(i));
		//avoid counting system table.
		if(strstr(INI_Context.ODBC_Driver,"Excel")!=NULL && strstr(tmp_str,"$")!=NULL)
		    continue;
		if(strstr(INI_Context.ODBC_Driver,"Access")!=NULL && strstr(tmp_str,"MSys")!=NULL)
		    continue;
		if(	(strstr(INI_Context.ODBC_Driver,"MongoDB")!=NULL) && \
		    (strstr(tmp_str,"SYSTEM")!=NULL || strstr(tmp_str,"BLOCKS")!=NULL || strstr(tmp_str,"LOB")!=NULL || strstr(tmp_str,"PARTS")!=NULL || strstr(tmp_str,"STARTUP")!=NULL) )
		    continue;
		CEdb.table_info_list[act_index].table_name=(char *)calloc(1,strlen(CEdb.table_list.GetAt(i))+1);
		strcpy(CEdb.table_info_list[act_index].table_name,CEdb.table_list.GetAt(i));
		CEdb.table_info_list[act_index].field_num=0;
		CEdb.table_info_list[act_index].new_record_num=0;
		CEdb.table_info_list[act_index].ori_record_num=0;
		CEdb.table_info_list[act_index].diff_num=0;
		CEdb.table_info_list[act_index].diff_flag=false;

		//printf("%s\n",CEdb.table_info_list[act_index].table_name);
		act_index++;
	      }
	      // Allocate the recordset
	      CRecordset recset(&CEdb);

	      // Build the SQL string
	      // Remember to name a section of data in the ODBC sheet using "Insert->Names" to be
	      // able to work with the data like you would with a table in a "real" database. There
	      // may be more than one table contained in a worksheet.
	      for(i=0;i<act_table_num;i++)
	      {
		//Determine whether to send original data or not.
		//Comment out = (REPORT_ORI = true): send original data
		//Instead = (REPORT_ORI = false): not send original data
		if(!REPORT_ORI)
		{
#pragma region GET_ORI_COUNT
		    sprintf(tmp_sql,"SELECT COUNT(*) FROM %s;",CEdb.table_info_list[i].table_name);
		    sSql.Format(_T(tmp_sql));
		    recset.Open(CRecordset::forwardOnly,sSql,CRecordset::readOnly);
#pragma region recset_IsOpen
		    if(recset.IsOpen())
		    {
		        //printf("CReadODBCDlg - Counting CRecordset (%s) Opened Successsful!!\n",CEdb.table_info_list[i].table_name);
		        CReadODBCLog(g_loghandle,Normal, "CReadODBCDlg - Counting CRecordset (%s) Opened Successsful!!",CEdb.table_info_list[i].table_name);
		        CEdb.GetOriRecordCounts(recset,i);
		        recset.Close();
		    }
		    else
		    {	
		        //printf("CReadODBCDlg - Counting CRecordset (%s) Opened Failed!!\n",CEdb.table_info_list[i].table_name);
		        CReadODBCLog(g_loghandle,Error, "CReadODBCDlg - Counting CRecordset (%s) Opened Failed!!",CEdb.table_info_list[i].table_name);
		        //return false;
		    }
#pragma endregion recset_IsOpen
#pragma endregion GET_ORI_COUNT
		}


#pragma region GET_FIELD
		//sprintf(tmp_sql,"SELECT * FROM %s;",CEdb.table_info_list[i].table_name);
		sprintf(tmp_sql,"select * from %s;",CEdb.table_info_list[i].table_name);
		printf("%s\n",tmp_sql);				
		//sprintf(tmp_sql,"SELECT TOP 1 * FROM %s ORDER BY 1 DESC;",CEdb.table_info_list[i].table_name);
		sSql.Format(_T(tmp_sql));

		//sSql = "SELECT * FROM Demo_Table ORDER BY Field_1";
		//sSql = "SELECT * FROM Demo_Table WHERE Field_1='aaa'";	// Must be Field_1='aaa' not Field_1=aaa or Field_1=\"aaa\"!! 
		//sSql = "INSERT INTO demo_table (Field_1,Field_2,Field_3) VALUES (123,123,123)"; // it's ok

		// Execute that query (implicitly by opening the recordset)
		recset.Open(CRecordset::forwardOnly,sSql,CRecordset::readOnly);
#pragma region recset_IsOpen
		// Browse the result
		if(recset.IsOpen())
		{
		    //printf("CReadODBCDlg - Retrieving CRecordset (%s) Opened Successsful!!\n",CEdb.table_info_list[i].table_name);
		    CReadODBCLog(g_loghandle,Normal, "CReadODBCDlg - Retrieving CRecordset (%s) Opened Successsful!!",CEdb.table_info_list[i].table_name);

		    CEdb.GetFieldNames(recset,i);
		    field_num=CEdb.table_info_list[i].field_num;
		    sAItem=new CString[field_num];

		    while( !recset.IsEOF() )
		    {
		        // Read the result line
		        //char str[4096];
		        //strcpy(str,"");

		        //printf("%d\n",sItem[4].GetLength());
		        //szBuf = new char[sItem[4].GetLength()+1];
		        //strcpy(szBuf, sItem[4].GetString());
		        //strContent=parser_ansitoutf8(szBuf);

		        for(j=0;j<field_num;j++)
		        {	
			  recset.GetFieldValue(CEdb.table_info_list[i].field_list[j],sAItem[j]);
			  printf("%s ",sAItem[j]);
			  //strcat(str,sAItem[j]);
			  //strcat(str," ");
		        }

		        //if(strContent)
		        //	free(strContent);

		        printf("\n");
		        //if(szBuf)
		        //	delete []szBuf;
		        // Skip to the next resultline
		        recset.MoveNext();
		    }

		    if(sAItem)
		        delete []sAItem;
		    recset.Close();
		    //return true;
		}
		else
		{	
		    //printf("CReadODBCDlg - Retrieving CRecordset (%s) Opened Failed!!\n",CEdb.table_info_list[i].table_name);
		    CReadODBCLog(g_loghandle,Error, "CReadODBCDlg - Retrieving CRecordset (%s) Opened Failed!!",CEdb.table_info_list[i].table_name);
		    //return false;
		}
#pragma endregion recset_IsOpen
#pragma endregion GET_FIELD
	      }
	      return true;
	  }
	  else
	  {
	      //printf("CReadODBCDlg - Database Getting Tables Failed!!\n");
	      CReadODBCLog(g_loghandle,Error, "CReadODBCDlg - Database Getting Tables Failed!!");
	      return false;
	  }
#pragma endregion CEdb_GetTableNames


        }
        else
        {
	  //printf("CReadODBCDlg - Database Opened Failed!!\n");
	  CReadODBCLog(g_loghandle, Error, "CReadODBCDlg -Database Opened Failed!!");
	  return false;
        }
#pragma endregion CEdb_IsOpen


    }
    CATCH(CDBException, e)
    {
        CReadODBCLog(g_loghandle, Error,"CReadODBCDlg - Database error: %s",e->m_strError);
        if(CEdb.IsOpen())
	  CEdb.Close();
        //AfxMessageBox("CReadODBCDlg - Database error: "+e->m_strError);	//This function will crash agent
        return false;
    }
    END_CATCH;
}



void CReadODBCDlg::Stop()
{
    if(g_loghandle)
        UninitLog(g_loghandle);

    if(CEdb.IsOpen())
        CEdb.Close();// Close the database
}


void CReadODBCDlg::CheckDiff(RecContext_t *RCt_list, bool bFirstCheck = false)
{
    int i=0,j=0,k=0;
    int new_record_num;
    char tmp_sql[SQL_LENGTH];	//presume max size of a sql = 256
    CString sSql;
    CString sItem;
    CString *sAItem=NULL;
    CRecordset recset(&CEdb);

    CODBCFieldInfo fieldinfo;
    char tmp_str[VALUE_LENGTH];
    short n=0;

    for(i=0;i<CEdb.table_num;i++)
    {	
        //----------------------------------------------- determine differences
        TRY{
	  sprintf(tmp_sql,"SELECT COUNT(*) FROM %s;",CEdb.table_info_list[i].table_name);	
	  sSql.Format(_T(tmp_sql));
	  recset.Open(CRecordset::forwardOnly,sSql,CRecordset::readOnly);
	  if(recset.IsOpen())
	  {
	      while( !recset.IsEOF() )
	      {
		recset.GetODBCFieldInfo(n,fieldinfo);
		recset.GetFieldValue(fieldinfo.m_strName,sItem);
		new_record_num = _ttoi(sItem);
		recset.MoveNext();
	      }
	      recset.Close();			
	  }

	  if(bFirstCheck == true)
	  {
	      if(RCt_list[i].index > new_record_num || RCt_list[i].index == 0)  //<rec>
	      {
		CEdb.table_info_list[i].ori_record_num = 0;

	      }
	      else if(RCt_list[i].index <= new_record_num)
	      {
		sprintf(tmp_sql,"SELECT TOP 1 * FROM ( SELECT TOP %d * FROM %s ORDER BY 1) ORDER BY 1 DESC;",RCt_list[i].index,CEdb.table_info_list[i].table_name);
		sSql.Format(_T(tmp_sql));
		recset.Open(CRecordset::forwardOnly,sSql,CRecordset::readOnly);
		if(recset.IsOpen())
		{
		    sAItem=new CString[1];
		    recset.GetFieldValue(CEdb.table_info_list[i].field_list[0],sAItem[0]);
		    strcpy(tmp_str,sAItem[0].GetString());

		    if(sAItem)
		        delete []sAItem;

		    recset.Close();
		}
		if(!strcmp(tmp_str,  RCt_list[i].time))
		{
		    CEdb.table_info_list[i].ori_record_num = RCt_list[i].index;
		}
		else
		{
		    CEdb.table_info_list[i].ori_record_num == 0;
		}
	      }                                                            //<rec>
	  } // End of if(bFirstCheck == true)

	  CEdb.table_info_list[i].new_record_num=new_record_num;

	  if(new_record_num >= CEdb.table_info_list[i].ori_record_num)
	      CEdb.table_info_list[i].diff_num= new_record_num-CEdb.table_info_list[i].ori_record_num;
	  else
	      CEdb.table_info_list[i].diff_num= new_record_num;

	  if(CEdb.table_info_list[i].diff_num > 0 && CEdb.table_info_list[i].diff_num < 65535)
	  {
	      CEdb.table_info_list[i].diff_record=(struct Diff_Record_Info *)calloc(CEdb.table_info_list[i].diff_num,sizeof(struct Diff_Record_Info));				
	      CEdb.table_info_list[i].diff_flag=true;
	  }

	  //----------------------------------------------- initialzie CEdb.table_info_list[i].diff_record
	  for(j=0;j<CEdb.table_info_list[i].diff_num;j++)
	  {
	      CEdb.table_info_list[i].diff_record[j].field_name=(char **)calloc(CEdb.table_info_list[i].field_num,sizeof(char*));
	      CEdb.table_info_list[i].diff_record[j].value=(char **)calloc(CEdb.table_info_list[i].field_num,sizeof(char*));
	      for(k=0;k<CEdb.table_info_list[i].field_num;k++)
	      {
		CEdb.table_info_list[i].diff_record[j].field_name[k]=(char *)calloc(FIELD_NAME_LENGTH,sizeof(char));
		CEdb.table_info_list[i].diff_record[j].value[k]=(char *)calloc(VALUE_LENGTH,sizeof(char));
		strcpy(CEdb.table_info_list[i].diff_record[j].field_name[k],CEdb.table_info_list[i].field_list[k]);
	      }
	  }
        }
        CATCH (CDBException, e){
	  CReadODBCLog(g_loghandle, Error, "CReadODBCDlg - %s",e->m_strError);
	  printf("\n CReadODBCDlg - %s \n", e->m_strError);	
        }
        END_CATCH;
    }

    //----------------------------------------------- get differences
    for(i=0;i<CEdb.table_num;i++)
    {
        if(CEdb.table_info_list[i].diff_flag)
        {
	  TRY{
	      // data with same id will be selected at same time....
	      if(CEdb.driver_type == MYSQL || CEdb.driver_type == POSTGRESQL)
		sprintf(tmp_sql,"SELECT * FROM %s ORDER BY 1 DESC LIMIT %d;",CEdb.table_info_list[i].table_name,CEdb.table_info_list[i].diff_num);
	      else
		//sprintf(tmp_sql,"SELECT TOP 1 * FROM ( SELECT TOP %d * FROM %s ORDER BY 1) ORDER BY 1 DESC;",10,CEdb.table_info_list[i].table_name);
		sprintf(tmp_sql,"SELECT TOP %d * FROM %s ORDER BY 1 DESC;",CEdb.table_info_list[i].diff_num,CEdb.table_info_list[i].table_name);	//cant be used in MySQL
	      sSql.Format(_T(tmp_sql));
	      recset.Open(CRecordset::forwardOnly,sSql,CRecordset::readOnly);
	      if(recset.IsOpen())
	      {
		sAItem=new CString[CEdb.table_info_list[i].field_num];
		//while( !recset.IsEOF() )
		//{			
		for(j=0;j<CEdb.table_info_list[i].diff_num;j++)
		{
		    //char str[4096];
		    //strcpy(str,"");
		    /*sprintf(str,"%s - ",CEdb.table_info_list[i].table_name);*/
			printf("%s - ",CEdb.table_info_list[i].table_name);
		    for(k=0;k<CEdb.table_info_list[i].field_num;k++)
		    {	
		        recset.GetFieldValue(CEdb.table_info_list[i].field_list[k],sAItem[k]);
		        strcpy(tmp_str,sAItem[k].GetString());
		        strcpy(CEdb.table_info_list[i].diff_record[j].value[k],tmp_str);
		        //strcat(str,sAItem[k]);
		        //strcat(str," ");
				printf("%s ",tmp_str);
		    }
		    printf("\n");

		    if(!recset.IsEOF()) 
		        recset.MoveNext();
		}
		if(sAItem)
		    delete []sAItem;

		recset.Close();
	      }

	  }
	  CATCH (CDBException, e){
	      CReadODBCLog(g_loghandle, Error, "CReadODBCDlg - %s",e->m_strError);
	      printf("\n CReadODBCDlg - %s \n", e->m_strError);	
	  }
	  END_CATCH;						        
        }
    }


}
void CReadODBCDlg::UpdateRecordNum()
{
    int i=0,j=0,k=0;
    for(i=0;i<CEdb.table_num;i++)
    {
        if(CEdb.table_info_list[i].diff_flag)
        {
	  if(CEdb.table_info_list[i].diff_record)
	  {
	      for(j=0;j<CEdb.table_info_list[i].diff_num;j++)
	      {
		for(k=0;k<CEdb.table_info_list[i].field_num;k++)
		{
		    if(CEdb.table_info_list[i].diff_record[j].field_name)
		        if(CEdb.table_info_list[i].diff_record[j].field_name[k])
			  free(CEdb.table_info_list[i].diff_record[j].field_name[k]);

		    if(CEdb.table_info_list[i].diff_record[j].value)
		        if(CEdb.table_info_list[i].diff_record[j].value[k])
			  free(CEdb.table_info_list[i].diff_record[j].value[k]);											
		}
		if(CEdb.table_info_list[i].diff_record[j].field_name)
		    free(CEdb.table_info_list[i].diff_record[j].field_name);
		if(CEdb.table_info_list[i].diff_record[j].value)
		    free(CEdb.table_info_list[i].diff_record[j].value);
	      }
	      free(CEdb.table_info_list[i].diff_record);	
	  }
	  CEdb.table_info_list[i].ori_record_num=CEdb.table_info_list[i].new_record_num;
	  CEdb.table_info_list[i].diff_num=0;
	  CEdb.table_info_list[i].diff_flag=false;
        }

    }

}