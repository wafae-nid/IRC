#ifndef SERVER_HPP
#define SERVER_HPP


#include "server_client.hpp"
#include <vector>
#include <string>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>

class Server
{
    private:
        int server_fd;
        int max_fd;
        fd_set original_set;

        std::vector<Client> clients;

        static const std::string SERVER_PASSWORD;

    public:
        Server();
        ~Server();

        void run();

    private:
        void server_setup();
        void server_core();

        void handle_client(int client_fd);
        void recompute_max_fd();

        bool send_to_client(int fd, std::string msg);

        void try_register(Client *client);

        void nick_command(Client *client, std::string command);
        void pass_command(Client *client, std::string command);
        void user_command(Client *client, std::string command);

        void handle_command(Client *client, std::string command);
        void check_buffer(Client *client);

        void remove_client(int fd);
        static void signal_handler(int sig); // so i dond need an object to call it i sued static
};

#endif