#ifndef COURSEWORK_DBMS_SQL_CLI_H
#define COURSEWORK_DBMS_SQL_CLI_H

#include "dbms/sql/sql_api.h"

namespace dbms
{

int run_cli(SqlApi& api, int argc, char** argv);

}

#endif
