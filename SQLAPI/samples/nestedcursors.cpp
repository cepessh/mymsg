/*

This example demonstrates how to use Oracle nested cursors
with SQLAPI++ Library.

*/

#include <SQLAPI.h> // main SQLAPI++ header

#include <iostream>
using namespace std;

void OracleNestedCursors();

int main()
{
	OracleNestedCursors();
	return 0;
}

void OracleNestedCursors()
{
	SAConnection con;

	try
	{
		SACommand cmd;
		cmd.setConnection(&con);

		con.Connect(
			_TSA("prok"), _TSA("dwadm"), _TSA("dwadm"), SA_Oracle_Client);

		// this select includes nested cursor
		cmd.setCommandText(
			_TSA("select 'nested cursor key', CURSOR(select * from dual) as nested_cursor from dual"));

		cmd.Execute();

		while(cmd.FetchNext())
		{
			cout << "Resul set for '" << cmd[1].asString().GetMultiByteChars() << "':\n";
			// get nested select and fetch from it
			SACommand *pNestedCursor = cmd[_TSA("nested_cursor")];
			while(pNestedCursor->FetchNext())
			{
				cout << "\t" << pNestedCursor->Field(1).asString().GetMultiByteChars() << "\n";
			}
		}
	}
    catch(SAException &x)
    {
        // SAConnection::Rollback()
        // can also throw an exception
        // (if a network error for example),
        // we will be ready
        try
        {
            // on error rollback changes
            con.Rollback();
        }
        catch(SAException &)
        {
        }
        // print error message
        cout << x.ErrText().GetMultiByteChars() << "\n";
    }
}
