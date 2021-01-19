#include <stdio.h>  // for printf
#include <SQLAPI.h> // main SQLAPI++ header

int main(int argc, char* argv[])
{
    SAConnection con; // connection object
    SACommand cmd(
        &con,
        _TSA("Select fid, fvarchar20 from test_tbl"));    // command object
    
    try
    {
        // connect to database (Oracle in our example)
        con.Connect(_TSA("test"), _TSA("tester"), _TSA("tester"), SA_Oracle_Client);

        // Select from our test table
        cmd.Execute();
        // fetch results row by row and print results
        while(cmd.FetchNext())
        {
            printf("Row fetched: fid = %ld, fvarchar20 = '%s'\n", 
                cmd.Field(_TSA("fid")).asLong(),
                cmd.Field(_TSA("fvarchar20")).asString().GetMultiByteChars());
        }

        // commit changes on success
        con.Commit();

        printf("Rows selected!\n");
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
        printf("%s\n", x.ErrText().GetMultiByteChars());
    }
    
    return 0;
}
