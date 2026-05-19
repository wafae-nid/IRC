#include "Server.hpp"


volatile sig_atomic_t   g_running = 1;

Server::Server(int port,const std::string Password):SERVER_PASSWORD(Password), SERVER_NAME("IRC")
{
   
    server_fd = -1;
    if (port <= 0 || port > 65535)
    {
        std::cout << "Invalid port range\n";
        return;
    }
    s_port = port; 
    signal(SIGINT,  Server::signal_handler);
    signal(SIGQUIT, Server::signal_handler);

}

Server::~Server()
{
    for (size_t i = 0; i < fds.size(); i++)
        close(fds[i].fd);
}

void Server::signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

int parse_port(std::string port)
{

    for (size_t i = 0; i < port.size(); i++)
    {
        if (!std::isdigit(port[i]))
        {
            std::cout << "Invalid port\n";
            return (0);
        }
    }
    return(1);
}

int main(int argc, char **argv)
{

   if(argc != 3)
   {
     std::cout << "you should enter port and password \n";
     return(1);
   }
   signal(SIGPIPE, SIG_IGN);
   if(!parse_port(argv[1]))
    return(1);
   int port = std::atoi(argv[1]);
   Server server(port,argv[2]);
   server.run(); 

}
