/****************************************************************************/
/* Copyright(C) : Advantech Technologies, Inc.														 */
/* Create Date  : 2016 by Zach Chih															     */
/* Modified Date: 2016/8/15 by Zach Chih															 */
/* Abstract     : ODBC Handler                                   													*/
/* Reference    : None																									 */
/****************************************************************************/
#ifndef CE_Database_H
#define CE_Database_H

#include <afxdb.h>
#include "util_path.h"



enum Driver_Type
{
	EXCEL=0,
	ACCESS,
	MYSQL,
	POSTGRESQL,
	MONGODB,
	TEXT,
};

struct Diff_Record_Info
{
	char **field_name;
	char **value;
};

struct Table_Info
{
	char *table_name;
	int field_num;
	int ori_record_num;
	int new_record_num;
	bool diff_flag;
	int diff_num;
    Diff_Record_Info *diff_record;
	char **field_list;
};

class CEDatabase : public CDatabase
{
// Konstruktion
public:

	int table_num;
	CStringArray table_list;
	Table_Info *table_info_list;
	Driver_Type driver_type;
	CEDatabase();
	bool GetTableNames();
	bool GetFieldNames(CRecordset &,int);
	bool GetOriRecordCounts(CRecordset &,int);
	~CEDatabase();
};

#endif