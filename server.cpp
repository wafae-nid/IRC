#include "Server.hpp"

const std::string Server::SERVER_PASSWORD = "1234";
const std::string Server::SERVER_NAME = "IRC";

volatile sig_atomic_t   g_running = 1;

Server::Server()
{
   
    server_fd = -1;
    max_fd = -1;
    signal(SIGINT,  Server::signal_handler);
    signal(SIGQUIT, Server::signal_handler);
    FD_ZERO(&original_set);

}

Server::~Server()
{
    if (server_fd != -1)
        close(server_fd);
}

void Server::signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

void Server::reply(Client *client, const std::string &code, const std::string &command, const std::string &message)
{
    std::string msg = ":" + SERVER_NAME + " " + code;

    std::string target;

    if (client->registered && !client->nickname.empty())
        target = client->nickname;
    else
        target = "*";

    msg += " " + target;

    if (!command.empty())
        msg += " " + command;

    msg += " :" + message;

    if (!send_to_client(client->fd, msg))
        remove_client(client->fd);
}

bool Server::send_to_client(int fd, std::string msg)
{
    msg += "\r\n";
    if(send(fd, msg.c_str(), msg.size(), 0) == -1)
    {
         return false;
    }
    return true;
}



void Server::recompute_max_fd()
{
    max_fd = server_fd;
    for (size_t i = 0; i < clients.size(); i++)
    {
        if (clients[i].fd > max_fd)
            max_fd = clients[i].fd;
    }
}

/* ---------------- CLIENT CLEANUP ---------------- */

void Server::remove_client(int fd)
{
    for (size_t i = 0; i < clients.size(); i++)
    {
        if (clients[i].fd == fd)
        {
            clients.erase(clients.begin() + i);
            break;
        }
    }
    FD_CLR(fd, &original_set);
    close(fd);
    recompute_max_fd();
}

/*--------------- REGISTER ---------------- */

void Server::try_register(Client *client)
{
    if (client->pass_ok && client->nick_set && client->user_set && !client->registered)
    {
        client->registered = true;
        reply(client, "001", "", "Welcome to the IRC network");
        reply(client, "002", "", "Your host is IRC, running version 1.0");
        reply(client, "003", "", "This server was created today");
        reply(client, "004", "", "IRC 1.0");
    }
}


/* ---------------- COMMANDS (logic ) ---------------- */

bool Server::nickname_exists(const std::string &nick)
{
    for (size_t i = 0; i < clients.size(); i++)
    {
        if (clients[i].nickname == nick)
            return true;
    }
    return false;
}

void Server::nick_command(Client *client, std::string param)
{

    if (param.empty())
    {
        reply(client, "461", "NICK", "Not enough parameters");
        return;
    }
    if(nickname_exists(param))
    {
        reply(client, "433", "NICK", param + " :Nickname is already in use");
        return;
    }
    else if(client->registered)
    {
        std::string old_nick = client->nickname;
        std::string msg = ":" + old_nick + " NICK :" + param;
        send_to_client(client->fd, msg);
    }
    client->nickname = param;
    client->nick_set = true;
    try_register(client);
}

void Server::pass_command(Client *client, std::string param)
{
    if (client->pass_ok)
    {
        reply(client, "462", "PASS", "You may not reregister");
        return;
    }

    if (param.empty())
    {
        reply(client, "461", "PASS", "Not enough parameters");
        return;
    }

    if (param != SERVER_PASSWORD)
    {
        reply(client, "464", "PASS", "Wrong password");
        return;
    }
    client->pass_ok = true;
}

void Server::user_command(Client *client, std::string param)
{
    if (client->user_set)
    {
        reply(client, "462", "USER", "You may not reregister"); // user can be set only once and only if not registered before unlike nickname
        return;
    }

    if (param.empty())
    {
        reply(client, "461", "USER", "Not enough parameters");
        return;
    }
    client->username = param;
    client->user_set = true;
    try_register(client);
}

/* ---------------- COMMAND HANDLER ---------------- */

void Server::capitalize_command(std::string &command)
{
    for(size_t i = 0 ; i < command.size(); i++)
    {
       command[i] =  std::toupper(static_cast<unsigned char>(command[i]));
    }
}

void Server::handle_command(Client *client, Command command)
{
    if (command.cmd.empty())
        return;

    capitalize_command(command.cmd);// so nick and NICK WORK ;

    if (!client->pass_ok)
    {
        if (command.cmd == "PASS")
        {
            pass_command(client, command.param);
            return;
        }

        reply(client, "464", "", "Password required");
        return;
    }

    if (command.cmd == "PASS")
    {
        reply(client, "462", "PASS", "You may not reregister");
    }
    else if (command.cmd == "NICK")
        nick_command(client, command.param);
    else if (command.cmd == "USER")
        user_command(client, command.param);
    else
        reply(client, "421", command.cmd, "Unknown command");
}


/* ---------------- BUFFER ---------------- */

Command Server::parse_command(std::string command_)
{
    Command command;
    std::string rest;
    std::string param;
    

    size_t index = command_.find_first_not_of(" \t");

    if(index == std::string::npos)
        return(command);

    rest = command_.substr(index); // the first not space char pos

    size_t space_pos = rest.find(' ');
    if(space_pos == std::string::npos)
    {
        command.cmd = rest;
        return(command);
    }
    command.cmd = rest.substr(0, space_pos);

    index = rest.find_first_not_of(" \t", space_pos);// fisrt non space char from that last space 
    if(index != std::string::npos) // if there is a parameter assign it 
        command.param = rest.substr(index);
    return(command);

}

void Server::check_buffer(Client *client)
{
    size_t pos;
    std::string command_;
    Command command;

    while ((pos = client->buffer.find("\r\n")) != std::string::npos)
    {
        command_ = client->buffer.substr(0, pos);
        command = parse_command(command_);
        handle_command(client, command);
        client->buffer.erase(0, pos + 2);
    }
}

/* ---------------- CLIENT HANDLING ---------------- */

void Server::handle_client(int client_fd)
{
    char buff[1024];
    ssize_t bytes = recv(client_fd, buff, sizeof(buff) - 1, 0);

    if (bytes <= 0)
    {
        remove_client(client_fd);
        return;
    }

    buff[bytes] = '\0';

    for (size_t i = 0; i < clients.size(); i++)
    {
        if (clients[i].fd == client_fd)
        {
            clients[i].buffer.append(buff, bytes);
            check_buffer(&clients[i]);
            break;
        }
    }
}

void Server::server_setup()
{
    struct sockaddr_in addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        std::cout << "socket failed \n";
        return;
    }

    int opt = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        std::cout << "setsockopt failed \n";
        return;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(6667);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        std::cout << "binding failed \n";
        return;
    }

    if (listen(server_fd, 10) == -1)
    {
        std::cout << "listening failed \n";
        return;
    }

    FD_SET(server_fd, &original_set);
    max_fd = server_fd;

    std::cout << "server ready\n";
}

/* ---------------- MAIN LOOP ---------------- */

void Server::server_core()
{
    fd_set readfds;

    while (g_running)
    {
        readfds = original_set;

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            std::cout << "select failed \n";
            break;
        }

        for (int fd = 0; fd <= max_fd; fd++)
        {
            if (!FD_ISSET(fd, &readfds))
                continue;
            if (fd == server_fd)
            {
                struct sockaddr_in addr;
                socklen_t len = sizeof(addr);

                int client_fd = accept(fd, (struct sockaddr *)&addr, &len);
                if (client_fd < 0)
                {
                    std::cout<<"accept failed\n";
                    continue;
                }

                FD_SET(client_fd, &original_set);

                Client c;
                c.fd = client_fd;
                c.pass_ok = false;
                c.nick_set = false;
                c.user_set = false;
                c.registered = false;

                clients.push_back(c);

                recompute_max_fd();
            }
            else
                handle_client(fd);
        }
    }
}

/* ---------------- RUN ---------------- */

void Server::run()
{
    server_setup();
    server_core();
}

int main()
{

   signal(SIGPIPE, SIG_IGN);
   Server server;
   server.run(); 

}
