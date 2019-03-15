/****************************************************************************/
/* Copyright(C) : Advantech Technologies, Inc.														 */
/* Create Date  : 2016 by Zach Chih															     */
/* Modified Date: 2016/8/15 by Zach Chih															 */
/* Abstract     : ODBC Handler                                   													*/
/* Reference    : None																									 */
/****************************************************************************/
// ReadODBCDlg.h : Header-Datei
//

#include "CEDatabase.h"
#include "ODBC_Handler.h"

#if !defined(AFX_READODBCDLG_H__660FFF83_053E_11D3_A579_00105A59FE2F__INCLUDED_)
#define AFX_READODBCDLG_H__660FFF83_053E_11D3_A579_00105A59FE2F__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

/////////////////////////////////////////////////////////////////////////////
// CReadODBCDlg Dialogfeld
#define MaxIniStr 4096

typedef struct{
	//----------------------------------
	//INI-Setting
	//-----------------------------------
	char ODBC_Driver[100];
	bool bRead_Only;
	int update_interval;
	//----------------------------------
	//INI-Excel,Access
	//-----------------------------------
	char File_Path[256];
	char Field_Name[MaxIniStr];
	char Field_DataType[MaxIniStr];
	//----------------------------------
	//INI-MySQL
	//-----------------------------------
	char Server[256];
	char Port[6];
	char DataBase[50];
	char Uid[50];
	char Pwd[50];
}INI_context_t;


class CReadODBCDlg
{
// Konstruktion
public:
	CEDatabase CEdb;
	CString GetODBCDriver(char *);
	bool Start(INI_context_t);
	void CheckDiff(RecContext_t *RCt_list, bool bFirstCheck);
	void UpdateRecordNum();
	void Stop();
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ fügt unmittelbar vor der vorhergehenden Zeile zusätzliche Deklarationen ein.

#endif // !defined(AFX_READODBCDLG_H__660FFF83_053E_11D3_A579_00105A59FE2F__INCLUDED_)
