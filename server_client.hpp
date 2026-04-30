#ifndef SERVER_CLIENT_HPP
#define SERVER_CLIENT_HPP

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <string>
#include <vector>

struct Client
{
    int fd;
    std::string buffer;
};




#endif