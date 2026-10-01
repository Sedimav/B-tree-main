#include "dbms/core/dbms.h"
#include "dbms/sql/cli.h"
#include "dbms/sql/sql_api.h"

int main(int argc, char** argv)
{
    dbms::Dbms dbms_engine;
    dbms::SqlApi api(dbms_engine);
    return dbms::run_cli(api, argc, argv);
}
