/*
CREATE TABLE BT1(F1 INTEGER, F2 VARCHAR(50));
*/

#include <stdio.h>
#include <stdlib.h>

#include <SQLAPI.h>
#include <samisc.h>

#include <oraAPI.h>

void BindInputArray(SACommand& cmd, const SAString& paramName, ub2 dty, dvoid* values, ub4 valuesLen, sb4 maxDataLen, ub2* alenp, sb2* indp)
{
	oraAPI* pAPI = (oraAPI*)cmd.Connection()->NativeAPI();
	oraCommandHandles* pCmdH = (oraCommandHandles*)cmd.NativeHandles();
	OCIBind* pOCIBind = NULL;

	pAPI->Check(pAPI->OCIBindByName(pCmdH->m_pOCIStmt, &pOCIBind,
			pCmdH->m_pOCIError,
			(CONST text*)(const SAChar*)paramName,
			(ub4)paramName.GetBinaryLength(), values, maxDataLen,
			dty, indp, alenp, NULL, 0, NULL /*&valuesLen*/, OCI_DEFAULT),
		pCmdH->m_pOCIError, OCI_HTYPE_ERROR);
}

void ExecuteArray(SACommand& cmd, ub4 valuesLen)
{
	oraAPI* pAPI = (oraAPI*)cmd.Connection()->NativeAPI();
	oraConnectionHandles* pConH = (oraConnectionHandles*)cmd.Connection()->NativeHandles();
	oraCommandHandles* pCmdH = (oraCommandHandles*)cmd.NativeHandles();

	pAPI->Check(pAPI->OCIStmtExecute(
		pConH->m_pOCISvcCtx, pCmdH->m_pOCIStmt, pCmdH->m_pOCIError,
		valuesLen, 0, NULL, NULL, OCI_DEFAULT),
		pCmdH->m_pOCIError, OCI_HTYPE_ERROR, pCmdH->m_pOCIStmt);
}

#define ARRAY_SIZE 5

int main(int argc, char** argv)
{
	SAConnection con;

	try
	{
		con.Connect(_TSA("KDX"), _TSA("yas"), _TSA("java"), SA_Oracle_Client);

		SACommand cmd(&con, _TSA("delete from bt1"));
		cmd.Execute();

		con.Commit();
			
		cmd.setCommandText(_TSA("insert into bt1(f1,f2) values(:1, :2)"), SA_CmdSQLStmtRaw);
		cmd.Prepare();

		int f1Array[ARRAY_SIZE];
		oratext f2Array[ARRAY_SIZE][(50 + 1) * sizeof(SAChar)];
		ub2 alenp1[ARRAY_SIZE], alenp2[ARRAY_SIZE];
		sb2 indp1[ARRAY_SIZE], indp2[ARRAY_SIZE];

		for (int i = 0; i < ARRAY_SIZE; ++i)
		{
			f1Array[i] = i + 1;
			indp1[i] = alenp1[i] = sizeof(int);
			
			SAString s; s.Format(_TSA("> %d row %d <"), i + 1, i + 1);
			indp2[i] = alenp2[i] = s.GetBinaryLength() + sizeof(SAChar);			
			memcpy(f2Array[i], (const void*)s, alenp2[i]);
		}

		BindInputArray(cmd, _TSA("1"), SQLT_INT, f1Array, ARRAY_SIZE, 4, alenp1, indp1);
		BindInputArray(cmd, _TSA("2"), SQLT_STR, f2Array, ARRAY_SIZE, (50 + 1) * sizeof(SAChar), alenp2, indp2);

		ExecuteArray(cmd, ARRAY_SIZE);

		con.Commit();
	}
	catch (SAException& x)
	{
		printf("\nERROR: %s\n", x.ErrText().GetMultiByteChars());
	}

	return 0;
}