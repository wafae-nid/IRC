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
#include <poll.h>  
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <arpa/inet.h>
#include <cctype>

extern volatile sig_atomic_t g_running;
struct Command
{
    std::string cmd;
    std::string param;
};

class Server
{
    private:
        int server_fd;
        int s_port;

        std::vector<pollfd> fds;

        std::vector<Client> clients;


        const std::string SERVER_PASSWORD;
        const std::string SERVER_NAME;
        

    public:
        Server(int port,const std::string Password);
        ~Server();

        void run();

    private:
        void server_setup();
        void server_core();

        void handle_client(int client_fd);
        void handle_new_client(void);

        bool send_to_client(int fd, std::string msg);

        void try_register(Client *client);
        bool is_special_char(char c);
        bool nickname_exists(const std::string &nick);
        bool is_valid_nick(const std::string &nick);
        void nick_command(Client *client, std::string command);
        void pass_command(Client *client, std::string command);
        void user_command(Client *client, std::string command);

        std::string prefix(Client &c);
        Command parse_command(std::string command_);
        void capitalize_command(std::string &command);
        void handle_command(Client *client,Command command);
        void check_buffer(Client *client);
        int parse_port(std::string port);
        void remove_client(int fd);
        static void signal_handler(int sig); // so i dond need an object to call it i sued static
        void reply(Client *client, const std::string &code, const std::string &command, const std::string &message);

};

#endif