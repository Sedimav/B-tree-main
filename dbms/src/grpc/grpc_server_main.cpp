#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "dbms/grpc/sql_service_impl.h"

int main(int argc, char** argv)
{
    std::string address = "0.0.0.0:50051";
    std::filesystem::path data_root = dbms::Dbms::default_data_root();

    if (argc >= 2) {
        address = argv[1];
    }
    if (argc >= 3) {
        data_root = argv[2];
    }
    if (argc > 3) {
        std::cerr << "Usage: ./dbms_grpc_server [address] [data_root]\n";
        return 1;
    }

    dbms::grpc_api::SqlServiceImpl service(data_root);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    if (!server) {
        std::cerr << "ERROR: failed to start gRPC server on " << address << '\n';
        return 1;
    }

    std::cout << "gRPC server listening on " << address << '\n';
    std::cout << "data root: " << data_root << '\n';
    server->Wait();
    return 0;
}
