#ifndef CLIENT_H
#define CLIENT_H

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "mini3.grpc.pb.h"

class Client {
public:
  Client(std::string server_addr);

  // Send a query string; returns the server’s response text
  std::string sendQuery(const std::string& task_id, const std::string& query);

private:
  std::unique_ptr<mini3::Mini3Service::Stub> stub_;
};

#endif // CLIENT_H
