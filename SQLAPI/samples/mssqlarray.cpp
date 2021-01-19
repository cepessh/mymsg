// https://docs.microsoft.com/en-us/sql/odbc/reference/develop-app/binding-arrays-of-parameters?view=sql-server-ver15

/*
CREATE TABLE[dbo].[t1](
	[id][int] IDENTITY(1, 1) NOT NULL,
	[f1][int] NULL,
	[f2][varchar](50) NULL
	) ON[PRIMARY]
	GO
*/

#include <stdio.h>
#include <stdlib.h>

#include <SQLAPI.h>
#include <samisc.h>

#include <ssNcliAPI.h>


void PrepareArrayBinding(SACommand& cmd, int arraySize, SQLULEN* paramsProcessed)
{
	cmd.Prepare();

	ssNCliAPI* pAPI = (ssNCliAPI*)cmd.Connection()->NativeAPI();
	ssNCliCommandHandles* pCmdH = (ssNCliCommandHandles*)cmd.NativeHandles();

	pAPI->Check(
		pAPI->SQLSetStmtAttr(pCmdH->m_hstmt, SQL_ATTR_PARAM_BIND_TYPE, SQL_PARAM_BIND_BY_COLUMN, 0),
		SQL_HANDLE_STMT, pCmdH->m_hstmt);

	// Specify the number of elements in each parameter array.
	pAPI->Check(
		pAPI->SQLSetStmtAttr(pCmdH->m_hstmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER)arraySize, 0),
		SQL_HANDLE_STMT, pCmdH->m_hstmt);

	// Specify an array in which to return the status of each set of  
	// parameters.  
	// SQLSetStmtAttr(hstmt, SQL_ATTR_PARAM_STATUS_PTR, ParamStatusArray, 0);

	// Specify an SQLUINTEGER value in which to return the number of sets of parameters processed.  
	pAPI->Check(pAPI->SQLSetStmtAttr(pCmdH->m_hstmt, SQL_ATTR_PARAMS_PROCESSED_PTR, paramsProcessed, 0),
		SQL_HANDLE_STMT, pCmdH->m_hstmt);
}

void BindInputArray(SACommand& cmd, SQLPOINTER params,
	SQLUSMALLINT nParNum, 
	SQLSMALLINT valueType,
	SQLSMALLINT parameterType,
	SQLULEN columnSize,
	SQLSMALLINT decimalDigits,
	SQLLEN bufferLength,
	SQLLEN indicators[])
{
	ssNCliAPI* pAPI = (ssNCliAPI*)cmd.Connection()->NativeAPI();
	ssNCliCommandHandles* pCmdH = (ssNCliCommandHandles*)cmd.NativeHandles();

	pAPI->Check(
		pAPI->SQLBindParameter(pCmdH->m_hstmt, nParNum, SQL_PARAM_INPUT, valueType, parameterType, columnSize, decimalDigits,params, bufferLength, indicators),
		SQL_HANDLE_STMT, pCmdH->m_hstmt);
}

#define ARRAY_SIZE 5

int main(int argc, char** argv)
{
	SAConnection con;

	try
	{
		con.Connect(_TSA("bedlam-m\\sql2017@test"), _TSA(""), _TSA(""), SA_SQLServer_Client);

		SACommand cmd(&con, _TSA("insert into t1(f1,f2) values(?, ?)"), SA_CmdSQLStmtRaw);
		
		SQLULEN paramsProcessed;

		PrepareArrayBinding(cmd, ARRAY_SIZE, &paramsProcessed);

		SQLINTEGER  f1Array[ARRAY_SIZE];
		SQLCHAR f2Array[ARRAY_SIZE][50 + 1];
		SQLLEN f1IndArray[ARRAY_SIZE], f2IndArray[ARRAY_SIZE];


		memset(f1IndArray, 0, sizeof(f1IndArray));
		memset(f2IndArray, 0, sizeof(f2IndArray));
		for (int i = 0; i < ARRAY_SIZE; ++i)
		{
			f1Array[i] = i + 1;
			sprintf((char*)f2Array[i], "row %d", i + 1);
			f2IndArray[i] = SQL_NTS;
		}

		BindInputArray(cmd, f1Array, 1, SQL_C_LONG, SQL_INTEGER, 7, 0, 0, f1IndArray);
		BindInputArray(cmd, f2Array, 2, SQL_C_CHAR, SQL_CHAR, 50, 0, 51, f2IndArray);

		cmd.Execute();
		con.Commit();
	}
	catch (SAException& x)
	{
		printf("\nERROR: %s\n", x.ErrText().GetMultiByteChars());
	}

	return 0;
}