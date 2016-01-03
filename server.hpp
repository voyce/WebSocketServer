//
//  server.hpp
//  WebSocketServer
//
//  Created by Ian Voyce on 20/10/2015.
//  Copyright © 2015 Ian Voyce. All rights reserved.
//

#ifndef server_h
#define server_h

#include "boost/algorithm/string.hpp"
#include "boost/archive/iterators/base64_from_binary.hpp"
#include "boost/archive/iterators/transform_width.hpp"
#include "boost/endian/conversion.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/uuid/sha1.hpp"
#include "boost/shared_ptr.hpp"
#include <algorithm>

#include "connection.hpp"

using boost::asio::ip::tcp;
using namespace boost::algorithm;
namespace posix = boost::asio::posix;

namespace websocket {
    
    class server {
        boost::asio::io_service &_io;
        tcp::acceptor _acceptor;
        posix::stream_descriptor _input;
        boost::asio::streambuf _input_buffer;
        std::vector<std::shared_ptr<connection>> _connections;
        
        enum { port = 1030 };
    public:
        server(boost::asio::io_service &io):
        _io(io),
        _acceptor(io, tcp::endpoint(tcp::v4(), port)),
        _input(io, ::dup(STDIN_FILENO)){
        }
        
        // Start accepting connections and reading input, and kick-off
        // a thread to process the io queue.
        void run(){
            do_accept();
            do_inputloop();
            
            boost::thread thread([this]{_io.run();});
            thread.join();
        }
        
    private:
        // Accept new connections and handshake.
        void do_accept(){
            std::shared_ptr<connection> conn(new connection(_io));
            _acceptor.async_accept(conn->socket(),
                                   [this, conn](boost::system::error_code ec){
                                       if (!ec){
                                           std::cout << "accept" << std::endl;
                                           _connections.push_back((conn));
                                           conn->do_handshake();
                                           cleanup_connections();
                                           do_accept();
                                       }
                                       else {
                                           throw boost::system::system_error(ec);
                                       }
                                   });
        }
        
        // Remove any connections with a closed socket.
        void cleanup_connections(){
            decltype(_connections.begin()) closed;
            while (closed = std::find_if_not(_connections.begin(), _connections.end(), [](const std::shared_ptr<connection> &conn){
                return conn->socket().is_open();
            }), closed != _connections.end()){
                _connections.erase(closed);
            }
            std::cout << _connections.size() << " connections" << std::endl;
        }
        
        // Start an async function to listen for newline-delimited strings on stdin.
        // When recvd, handle the command by passing it to each connection.
        void do_inputloop(){
            boost::asio::async_read_until(_input, _input_buffer, '\n',
                                          [this](boost::system::error_code ec, std::size_t length){
                                              if (!ec){
                                                  char commandbuffer[128] = {};
                                                  _input_buffer.sgetn(commandbuffer, sizeof(commandbuffer) - 1);
                                                  
                                                  std::string command(commandbuffer);
                                                  trim_right(command);
                                                  if (command == "close"){
                                                      for(auto &conn : _connections)
                                                          conn->do_close();
                                                      cleanup_connections();
                                                  }
                                                  else if (command == "ping")
                                                      for(auto &conn : _connections)
                                                          conn->do_ping();
                                                  else
                                                      for(auto &conn : _connections)
                                                          conn->do_send(command);
                                                  
                                                  do_inputloop();
                                              } else {
                                                  std::cout << "exiting input read loop" << ec << std::endl;
                                              }
                                          });
        }
    };
}

#endif /* server_h */
