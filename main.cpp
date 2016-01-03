//
//  main.cpp
//  WebSocketServer
//
//  Created by Ian Voyce on 20/10/2015.
//  Copyright © 2015 Ian Voyce. All rights reserved.
//

#include <iostream>
#include "boost/asio.hpp"
#include "boost/thread.hpp"
#include "server.hpp"

int main(int argc, const char * argv[]) {
    try {
        boost::asio::io_service io;
        websocket::server server(io);
        server.run();
    } catch (boost::exception &e) {
        std::cout << diagnostic_information(e);
    }
    return 0;
}
