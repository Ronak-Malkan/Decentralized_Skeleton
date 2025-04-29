#include "server.h"
#include <iostream>
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
    std::cout << "["<< node_id_ <<"] Listener thread started\n";
    while (running_) {
        // TODO: block on gRPC server->Wait() or async queue → wrap in Message → inbound_.push()
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "["<< node_id_ <<"] Listener thread exiting\n";
}

void Server::processorLoop() {
    std::cout << "["<< node_id_ <<"] Processor thread started\n";
    while (running_) {
        // Block until a Message arrives
        Message msg = inbound_.wait_and_pop();
        // TODO: demux on msg.type → updatePeerInfo(msg) or processLocal/forward(msg)
        std::cout << "["<< node_id_ <<"] Processor got a message of type "
                  << (msg.type == MessageType::HEARTBEAT ? "HEARTBEAT" : "TASK_REQUEST")
                  << " from " << msg.from << "\n";
    }
    std::cout << "["<< node_id_ <<"] Processor thread exiting\n";
}

void Server::heartbeatLoop() {
    std::cout << "["<< node_id_ <<"] Heartbeat thread started\n";
    while (running_) {
        // TODO: sample metrics, compute score, build Heartbeat proto,
        //       send via gRPC stubs to neighbors_
        std::cout << "["<< node_id_ <<"] Sending heartbeat to " 
                  << neighbors_.size() << " neighbors\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "["<< node_id_ <<"] Heartbeat thread exiting\n";
}

void Server::routingLoop() {
    std::cout << "["<< node_id_ <<"] Routing thread started\n";
    while (running_) {
        // TODO: walk peerInfo → rebuild nextHop routing table
        std::cout << "["<< node_id_ <<"] Rebuilding routing table\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "["<< node_id_ <<"] Routing thread exiting\n";
}
