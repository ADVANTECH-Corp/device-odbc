/****************************************************************************/
/* Copyright(C) : Advantech Technologies, Inc.														 */
/* Create Date  : 2016 by Zach Chih															     */
/* Modified Date: 2016/8/15 by Zach Chih															 */
/* Abstract     : ODBC Handler                                   													*/
/* Reference    : None																									 */
/****************************************************************************/
#ifndef ODBC_HANDLER_H
#define ODBC_HANDLER_H

#define MAX_TABLE 256
#define MAX_NAME 128
#define MAX_TIME 64
#define MAX_FILE_PATH 256

struct RecContext_t{
    char table_name[MAX_NAME];
    int index;
    char time[MAX_TIME];
};

#endif