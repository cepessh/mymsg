#include <SQLAPI.h>
#include <ibAPI.h>

int main(int argc, char **argv)
{
    SAConnection con;
    con.setClient(SA_InterBase_Client);
    ibAPI *pibAPI = (ibAPI *)con.NativeAPI();

    ISC_STATUS status_vector[20];
    isc_db_handle db_handle = NULL;
    isc_tr_handle dummy_handle = NULL;

    pibAPI->isc_dsql_execute_immediate(
        status_vector,
        &db_handle,
        &dummy_handle,
        0,
        "create database 'localhost:c:/test.gdb' user 'SYSDBA' password 'masterkey'",
        1,
        NULL);
    if (status_vector[0] == 1 && status_vector[1])
    {
        /* Process error */
        pibAPI->isc_print_status(status_vector);
        return 1;
    }

    return 0;
}
