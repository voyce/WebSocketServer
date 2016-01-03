//
//  connection.hpp
//  WebSocketServer
//
//  Created by Ian Voyce on 26/10/2015.
//  Copyright © 2015 Ian Voyce. All rights reserved.
//

#ifndef connection_h
#define connection_h

#include "boost/algorithm/string.hpp"
#include "boost/archive/iterators/base64_from_binary.hpp"
#include "boost/archive/iterators/transform_width.hpp"
#include "boost/endian/conversion.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/uuid/sha1.hpp"
#include "boost/shared_ptr.hpp"
#include <algorithm>

using boost::asio::ip::tcp;
using namespace boost::algorithm;
namespace posix = boost::asio::posix;

namespace websocket {
    
    class connection : public std::enable_shared_from_this<connection>{
    private:
        struct header {
#ifdef BOOST_BIG_ENDIAN
            bool FIN : 1;
            bool : 3;
            unsigned char OPCODE : 4;
            
            bool MASK : 1;
            unsigned char PAYLOAD_LEN : 7;
#else
            unsigned char OPCODE : 4;
            bool : 3;
            bool FIN : 1;
            
            unsigned char PAYLOAD_LEN : 7;
            bool MASK : 1;
#endif
        } __attribute__((packed));
        
        const std::string _key = "Sec-WebSocket-Key: ";
        
        enum { max_length = 1024 };
        // Buffer for writing data
        char _data[max_length];
        // Buffer for reading data
        char _readData[max_length];
        
        tcp::socket _socket;
        
    public:
        connection(boost::asio::io_service &io):
        _socket(io)
        {
        }
        
        ~connection(){
            std::cout << "connection destroyed" << std::endl;
        }
        
        tcp::socket &socket(){
            return _socket;
        }
        
        void do_handshake(){
            auto self(shared_from_this());
            _socket.async_read_some(boost::asio::buffer(_data, max_length),
                                   [this, self](boost::system::error_code ec, std::size_t length)
                                   {
                                       if (!ec) {
                                           std::cout << "read" << std::endl;
                                           
                                           std::vector<std::string> lines;
                                           std::string s(_data);
                                           boost::algorithm::split(lines, s, [](wchar_t c){ return c == '\n';});
                                           
                                           for(auto &l : lines) trim_right(l);
                                           
                                           if (lines.size() > 0 && boost::starts_with(lines[0], "GET ")){
                                               // Look for key
                                               auto keyit = std::find_if(lines.begin(), lines.end(), [this](std::string &line){
                                                   return boost::starts_with(line, _key);
                                               });
                                               if (keyit == lines.end())
                                                   return;
                                               auto key = keyit->substr(_key.size()) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                                               
                                               // Create response data
                                               std::string s("HTTP/1.1 101 Switching Protocols\r\n"
                                                             "Connection: Upgrade\r\n"
                                                             "Upgrade: websocket\r\n"
                                                             "Sec-WebSocket-Accept: ");
                                               boost::uuids::detail::sha1 sha1;
                                               sha1.process_bytes(static_cast<void const *>(key.c_str()), key.length());
                                               unsigned int hash[5];
                                               sha1.get_digest(hash);
                                               if (boost::endian::order::native == boost::endian::order::little)
                                                   for(auto &hb : hash)
                                                       hb = boost::endian::endian_reverse(hb);
                                               
                                               
                                               std::vector<unsigned char> os;
                                               auto p = reinterpret_cast<unsigned char *>(hash);
                                               os.assign(p, p+(sizeof(hash)/sizeof(unsigned char)));
                                               
                                               s += encode64(os);
                                               s += "\r\n\r\n";
                                               
                                               boost::asio::async_write(_socket, boost::asio::buffer(s, s.length()),
                                                                        [this, self, s](boost::system::error_code ec, std::size_t length)
                                                                        {
                                                                            if (!ec){
                                                                                std::cout << "handshake: wrote " << length << std::endl;
                                                                                do_read();
                                                                            }
                                                                        });
                                           }
                                       }
                                       else if (ec == boost::asio::error::eof ||
                                                ec == boost::system::errc::broken_pipe){
                                           _socket.close();
                                       }
                                       else {
                                           throw boost::system::system_error(ec);
                                       }
                                   });
        }
        
        std::string encode64(const std::vector<unsigned char> &val) {
            using namespace boost::archive::iterators;
            std::stringstream os;
            typedef base64_from_binary<transform_width<const unsigned char *, 6, 8>> base64_text;
            std::copy(
                      base64_text(val.data()),
                      base64_text(val.data() + val.size()),
                      std::ostream_iterator<char>(os)
                      );
            return os.str().append((3 - val.size() % 3) % 3, '=');
        }
        
        
        void do_send(const std::string &msg){
            if (!_socket.is_open())
                return;
            // TODO: break up into 127-byte blocks if reqd
            if (msg.length() < 127) {
                header hdr = {};
                hdr.FIN = true;
                hdr.OPCODE = 0x1;
                hdr.PAYLOAD_LEN = msg.length();
                memcpy(_data, &hdr, sizeof(hdr));
                strncpy(&_data[2], msg.c_str(), msg.length());
                auto self(shared_from_this());
                boost::asio::async_write(_socket, boost::asio::buffer(_data, msg.length() + sizeof(hdr)),
                                         [this, self](boost::system::error_code ec, std::size_t length){
                                             if (!ec){
                                                 std::cout << "send: wrote " << length << std::endl;
                                             }
                                             else if (ec == boost::asio::error::eof ||
                                                      ec == boost::system::errc::broken_pipe){
                                                 _socket.close();
                                             }
                                             else {
                                                 std::cout << boost::system::system_error(ec).what() << std::endl;
                                                 throw boost::system::system_error(ec);
                                             }
                                         });
            }
        }
        
        void do_ping(){
            if (!_socket.is_open())
                return;
            header hdr = {};
            hdr.FIN = true;
            hdr.OPCODE = 0x9;
            auto self(shared_from_this());
            boost::asio::async_write(_socket, boost::asio::buffer(&hdr, sizeof(header)),
                                     [this, self](boost::system::error_code ec, std::size_t length){
                                         if (!ec){
                                             std::cout << "ping: wrote " << length << std::endl;
                                             // TODO: make pong read async
                                             auto read = _socket.read_some(boost::asio::buffer(_data, max_length));
                                             if (read >= sizeof(header)){
                                                 header pong = {};
                                                 memcpy(&pong, _data, sizeof(pong));
                                                 if (pong.OPCODE == 0xa){
                                                     std::cout << "pong!" << std::endl;
                                                     return;
                                                 }
                                             }
                                             // Close connection if ping gets no response
                                             do_close();
                                         }
                                         else if (ec == boost::asio::error::eof ||
                                                  ec == boost::system::errc::broken_pipe){
                                             _socket.close();
                                         }
                                         else {
                                             throw boost::system::system_error(ec);
                                         }
                                     });
        }
        
        void do_read(){
            if (!_socket.is_open())
                return;
            auto self(shared_from_this());
            memset(_readData, 0, max_length);
            _socket.async_read_some(boost::asio::buffer(_readData, max_length),
                                     [this, self](boost::system::error_code ec, std::size_t length){
                                         if (!ec){
                                             std::cout << "read: read " << length << std::endl;
                                             
                                             std::string s;
                                             header *hdr = reinterpret_cast<header *>(&_readData);
                                             if (hdr->MASK){
                                                 unsigned char mask[4] = {};
                                                 memcpy(mask, _readData + sizeof(header), sizeof(mask));
                                                 unsigned char *data = reinterpret_cast<unsigned char *>(_readData + sizeof(header) + sizeof(mask));
                                                 s.reserve(hdr->PAYLOAD_LEN);
                                                 for(int i=0; i < hdr->PAYLOAD_LEN; i++){
                                                     auto maskByte = i % 4;
                                                     char c = *data ^ mask[maskByte];
                                                     s += c;
                                                     data++;
                                                 }
                                                 std::cout << "recvd: " << s << std::endl;
                                             }
                                             else{
                                                 std::cout << "unmasked data recvd from client" << std::endl;
                                             }
                                             if (s == "close")
                                                 do_close();
                                             else {
                                                if (s.substr(0, 5) == "echo:")
                                                    do_send(s.substr(5));
                                                do_read();
                                             }
                                         }
                                         else {
                                             std::cout << (boost::system::system_error(ec).what()) << std::endl;
                                         }
                                     });
        }
        
        void do_close(){
            if (!_socket.is_open())
                return;
            header hdr = {};
            hdr.FIN = true;
            hdr.OPCODE = 0x8;
            auto self(shared_from_this());
            boost::asio::async_write(_socket, boost::asio::buffer(&hdr, sizeof(header)),
                                     [this, self](boost::system::error_code ec, std::size_t length){
                                         if (!ec){
                                             std::cout << "close: wrote " << length << std::endl;
                                             _socket.close();
                                         }
                                         else if (ec == boost::asio::error::eof ||
                                                  ec == boost::system::errc::broken_pipe){
                                             std::cout << (boost::system::system_error(ec).what()) << std::endl;
                                             _socket.close();
                                         }
                                         else {
                                             throw boost::system::system_error(ec);
                                         }
                                     });
        }
    };
}

#endif /* connection_h */
