#include "server.h"
#include "logger.h"
#include <chrono>
#include <thread>

Server::Server(std::string node_id,
               std::vector<std::string> neighbors)
  : node_id_(std::move(node_id)),
    neighbors_(std::move(neighbors))
{}

void Server::run() {
    running_ = true;

    // Spawn each worker thread
    t_listener_  = std::thread(&Server::listenerLoop,   this);
    t_processor_ = std::thread(&Server::processorLoop,  this);
    t_heartbeat_ = std::thread(&Server::heartbeatLoop,  this);
    t_routing_   = std::thread(&Server::routingLoop,    this);

    // Wait for them to finish (they never do, for now)
    t_listener_.join();
    t_processor_.join();
    t_heartbeat_.join();
    t_routing_.join();
}

void Server::shutdown() {
    running_ = false;
    // In a real implementation you'd also notify condition variables,  
    // shut down gRPC server, etc.
}

void Server::listenerLoop() {
    log_line("[" + node_id_ + "] Listener thread started");
    while (running_) {
        // TODO: block on gRPC server → wrap in Message → inbound_.push()
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    log_line("[" + node_id_ + "] Listener thread exiting");
}

void Server::processorLoop() {
    log_line("[" + node_id_ + "] Processor thread started");
    while (running_) {
        // Block until a Message arrives
        Message msg = inbound_.wait_and_pop();
        // TODO: demux on msg.type → updatePeerInfo(msg) or processLocal/forward(msg)
        std::string type_str = (msg.type == MessageType::HEARTBEAT
                                ? "HEARTBEAT"
                                : "TASK_REQUEST");
        log_line("[" + node_id_ + "] Processor got a message of type "
                 + type_str + " from " + msg.from);
    }
    log_line("[" + node_id_ + "] Processor thread exiting");
}

void Server::heartbeatLoop() {
    log_line("[" + node_id_ + "] Heartbeat thread started");
    while (running_) {
        // TODO: sample metrics, compute score, build Heartbeat proto,
        //       send via gRPC stubs to neighbors_
        log_line("[" + node_id_ + "] Sending heartbeat to "
                 + std::to_string(neighbors_.size()) + " neighbors");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    log_line("[" + node_id_ + "] Heartbeat thread exiting");
}

void Server::routingLoop() {
    log_line("[" + node_id_ + "] Routing thread started");
    while (running_) {
        // TODO: walk peerInfo → rebuild nextHop routing table
        log_line("[" + node_id_ + "] Rebuilding routing table");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    log_line("[" + node_id_ + "] Routing thread exiting");
}
