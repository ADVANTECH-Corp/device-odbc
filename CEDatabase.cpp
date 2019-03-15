/****************************************************************************/
/* Copyright(C) : Advantech Technologies, Inc.														 */
/* Create Date  : 2016 by Zach Chih															     */
/* Modified Date: 2016/8/15 by Zach Chih															 */
/* Abstract     : ODBC Handler                                   													*/
/* Reference    : None																									 */
/****************************************************************************/
#include "CEDatabase.h"

#include <Log.h>


#define FIELD_NAME_LENGTH 50

#define DEF_LOG_NAME    "CEDatabase.txt"   //default log file name
#define LOG_ENABLE
//#define DEF_LOG_MODE    (LOG_MODE_NULL_OUT)
//#define DEF_LOG_MODE    (LOG_MODE_FILE_OUT)
#define DEF_LOG_MODE    (LOG_MODE_CONSOLE_OUT|LOG_MODE_FILE_OUT)

#ifdef LOG_ENABLE
#define CEDatabaseLog(handle, level, fmt, ...)  do { if (handle != NULL)   \
	WriteLog(handle, DEF_LOG_MODE, level, fmt, ##__VA_ARGS__); } while(0)
#else
#define CEDatabaseLog(level, fmt, ...)
#endif

static void* g_loghandle = NULL;

CEDatabase::CEDatabase()
{
	char moudlePath[MAX_PATH] = {0};

	memset(moudlePath, 0 , sizeof(moudlePath));
	util_module_path_get(moudlePath);
	g_loghandle = InitLog(moudlePath);

	table_info_list=NULL;
	table_num=NULL;

}


bool CEDatabase::GetTableNames()
{
	ASSERT(IsOpen());
    SQLHSTMT hst = NULL;
    SQLRETURN nSqlResult = ::SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &hst);
    if (!SQL_SUCCEEDED(nSqlResult))
        TRACE1("Error %d in SQLAllocHandle()\n", nSqlResult);
    else
    {
        nSqlResult = ::SQLTables(hst, NULL, 0, NULL, 0, NULL, 0, NULL, 0);
        if (!SQL_SUCCEEDED(nSqlResult))
            TRACE1("Error %d in SQLTables()\n", nSqlResult);
        else 
        {
            // Col 1, TABLE_CAT:   File name
            // Col 2, TABLE_SCHEM: Not used with Excel
            // Col 3, TABLE_NAME:  Table name
            // Col 4, TABLE_TYPE:  Table type (e.g. TABLE, SYSTEM TABLE)
            // Col 5, REMARKS:     Not used with Excel
            // Max. sizes may be retrieved using SQLGetInfo().
            // 64 for Excel ODBC driver
            SQLTCHAR szName[64];
            //SQLTCHAR szType[64];
            SQLINTEGER cbName;
            //SQLINTEGER cbType;
            ::SQLBindCol(hst, 3, SQL_C_TCHAR, szName, sizeof(szName), &cbName);
            //::SQLBindCol(hst, 4, SQL_C_TCHAR, szType, sizeof(szType), &cbType);
            while ((nSqlResult = ::SQLFetch(hst)) != SQL_NO_DATA) 
            {
                if (SQL_SUCCEEDED(nSqlResult))
					this->table_list.Add(szName);
                else // if (SQL_ERROR == nSqlResult)
                {
                    TRACE1("Error %d fetching SQLTables\n", nSqlResult);
                    break;
                }
            }
        }
        ::SQLFreeHandle(SQL_HANDLE_STMT, hst);
    }
    return SQL_NO_DATA == nSqlResult && this->table_list.GetSize();
}


bool CEDatabase::GetFieldNames(CRecordset &recset,int index)
{
	this->table_info_list[index].field_num=recset.GetODBCFieldCount();
	CODBCFieldInfo fieldinfo;
	short i;

	this->table_info_list[index].field_list=(char **)calloc(this->table_info_list[index].field_num,sizeof(char *));

	for(i=0;i<this->table_info_list[index].field_num;i++)
	{	
		recset.GetODBCFieldInfo(i,fieldinfo);
		this->table_info_list[index].field_list[i]=(char *)calloc(FIELD_NAME_LENGTH,sizeof(char));	//presume max size of a field name = 50
		strcpy(this->table_info_list[index].field_list[i],fieldinfo.m_strName);

		printf("CEDatabase - GetFieldNames...\n");
		CEDatabaseLog(g_loghandle, Normal, "CEDatabase - %d. %s field_list[%d] = %s",index,this->table_info_list[index].table_name,i,this->table_info_list[index].field_list[i]);
		//printf("%d. %s field_list[%d] = %s\n",index,this->table_info_list[index].table_name,i,this->table_info_list[index].field_list[i]);
	}
	return true;
}

bool CEDatabase::GetOriRecordCounts(CRecordset &recset,int index)
{
	CODBCFieldInfo fieldinfo;
	CString sItem;
	short i=0;
	
	while( !recset.IsEOF() )
	{
		recset.GetODBCFieldInfo(i,fieldinfo);
		recset.GetFieldValue(fieldinfo.m_strName,sItem);
		this->table_info_list[index].ori_record_num = _ttoi(sItem);
	
		printf("CEDatabase - GetOriRecordCounts...\n");
		CEDatabaseLog(g_loghandle, Normal, "CEDatabase - %d. (%s) field_list[%d] = %s , num = %d",index,this->table_info_list[index].table_name,i,fieldinfo.m_strName,this->table_info_list[index].ori_record_num);
		//printf("%d. %s field_list[%d] = %s , num = %d\n",index,this->table_info_list[index].table_name,i,fieldinfo.m_strName,this->table_info_list[index].ori_record_num);

		recset.MoveNext();
	}

	return true;

}

CEDatabase::~CEDatabase()
{
	int i=0,j=0,k=0;
	
	if(g_loghandle)
		UninitLog(g_loghandle);

	if(this->table_info_list)
	{
			for(i=0;i<table_num;i++)
			{
					if(table_info_list[i].diff_flag)
					{
							if(table_info_list[i].diff_record)
							{
									for(j=0;j<table_info_list[i].diff_num;j++)
									{
											for(k=0;k<table_info_list[i].field_num;k++)
											{
												if(table_info_list[i].diff_record[j].field_name)
													if(table_info_list[i].diff_record[j].field_name[k])
														free(table_info_list[i].diff_record[j].field_name[k]);
							
												if(table_info_list[i].diff_record[j].value)
													if(table_info_list[i].diff_record[j].value[k])
														free(table_info_list[i].diff_record[j].value[k]);											
											}
											if(table_info_list[i].diff_record[j].field_name)
												free(table_info_list[i].diff_record[j].field_name);
											if(table_info_list[i].diff_record[j].value)
												free(table_info_list[i].diff_record[j].value);
									}
									free(table_info_list[i].diff_record);	
							}
							table_info_list[i].ori_record_num=table_info_list[i].new_record_num;
							table_info_list[i].diff_num=0;
							table_info_list[i].diff_flag=false;
					}

			}

			for(i=0;i<this->table_num;i++)
			{
				for(j=0;j<this->table_info_list[i].field_num;j++)
					if(this->table_info_list[i].field_list[j])
						free(this->table_info_list[i].field_list[j]);
				if(this->table_info_list[i].table_name)
					free(this->table_info_list[i].table_name);
			}
			free(this->table_info_list);
	}

}