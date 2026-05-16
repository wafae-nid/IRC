#include "Server.hpp"

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

bool Server::is_valid_nick(const std::string &nick)
{
    if (nick.empty())
        return false;

    if (nick.size() > 15)
        return false;

    for (size_t i = 0; i < nick.size(); i++)
    {
        if (!isalnum(nick[i]) && nick[i] != '_' && nick[i] != '-')
            return false;
    }
    return true;
}

void Server::nick_command(Client *client, std::string param)
{

    if (param.empty())
    {
        reply(client, "461", "NICK", "Not enough parameters");
        return;
    }

    if (!is_valid_nick(param))
    {
        reply(client, "432", "NICK", "Erroneous nickname");
        return;
    }

    if(nickname_exists(param) && client->nickname != param)
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
        reply(client, "464", "PASS", "Password required");
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
