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
#include <signal.h>



struct Client
{
    int fd;
    std::string buffer;

    bool pass_ok;
    bool nick_set;
    bool user_set;
    bool registered;

    std::string nickname;
    std::string username;
};




#endif