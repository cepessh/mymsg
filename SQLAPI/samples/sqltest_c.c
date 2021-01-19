// sqltest_c.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <stdio.h>
#include <SQLAPI.h>

void showError(SAException* err)
{    
    const SAException* next = err;
    while (next)
    {
        SAString* str = sqlapi_error_text(err);
        printf("ERROR: %s\n", sqlapi_string_mb(str));
        sqlapi_free_string(&str);
        next = sqlapi_error_nested(next);
    }
}

int main()
{
    int retCode = 0;
    SAPI* api = sqlapi_alloc_api();
    SAException* err = sqlapi_alloc_error();
    SAConnection* conn = sqlapi_alloc_connection(api);
    SACommand* cmd = sqlapi_alloc_command(conn);
    SAString* str = 0;
    const SAValueRead* val = 0;

    if (SA_No_Error != sqlapi_init(api, SA_Oracle_Client, err))
    {
        showError(err);
        retCode = -1;
        goto end;
    }

    if (SA_No_Error != sqlapi_connect(conn, _TSA("kdx"), _TSA("yas"), _TSA("java"), err))
    {
        showError(err);
        retCode = -1;
        goto end;
    }

    if (SA_No_Error != sqlapi_execute(cmd, _TSA("select f1,fdatetime from t1"), err))
    {
        showError(err);
        retCode = -1;
        goto end;
    }

    if (SA_True == sqlapi_is_result_set(cmd, err))
    {
        while (SA_False == sqlapi_has_error(err) &&
            SA_True == sqlapi_fetch_next(cmd, err))
        {          
            val = sqlapi_field_value(cmd, 1, err);
            if (SA_False == sqlapi_has_error(err))
                printf("VAL1 = %d, ", sqlapi_value_as_int32(val));

            val = sqlapi_field_value(cmd, 2, err);                         
            if (SA_False == sqlapi_has_error(err))
            {
                str = sqlapi_value_as_string(val);
                printf("VAL2 = %s\n", sqlapi_string_mb(str));
            }
            sqlapi_free_string(&str);
        }
    }
    
    if (SA_True == sqlapi_has_error(err))
    {
        showError(err);
        retCode = -1;
        goto end;
    }

end:

    sqlapi_free_string(&str);
    sqlapi_free_command(&cmd);
    sqlapi_free_connection(&conn);
    sqlapi_free_api(&api);
    sqlapi_free_error(&err);

    return retCode;
}


