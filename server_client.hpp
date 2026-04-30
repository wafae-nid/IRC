#ifndef SERVER_CLIENT_HPP
#define SERVER_CLIENT_HPP

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

struct Client
{
    int fd;
    std::string buffer;
};




#endif