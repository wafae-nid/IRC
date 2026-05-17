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
bool Server::is_special_char(char c)
{
    return (c == '[' ||c == ']' ||c == '\\' ||c == '`' ||
            c == '_' ||c == '^' || c == '{' ||c == '|' ||
            c == '}');
}
bool Server::is_valid_nick(const std::string &nick)
{
    if (nick.empty())
        return false;

    if (nick.size() > 9)
        return false;

    if (!std::isalpha(static_cast<unsigned char>(nick[0])) &&
        !is_special_char(nick[0]))
        return false;

    for (size_t i = 0; i < nick.size(); i++)
    {
        if (!std::isalpha(static_cast<unsigned char>(nick[i])) &&
            !std::isdigit(static_cast<unsigned char>(nick[i])) &&
            !is_special_char(nick[i]))
            return false;
    }

    return true;
}

void Server::nick_command(Client *client, Command command)
{

    if (command.params.size() != 1)
    {
        reply(client, "431", "", "No nickname given");
        return;
    }

    if (!is_valid_nick(command.params[0]))
    {
        reply(client, "432", command.params[0], "Erroneous nickname");
        return;
    }

    if(nickname_exists(command.params[0]) && client->nickname != command.params[0])
    {
        reply(client, "433", command.params[0], "Nickname is already in use");
        return;
    }
    if(client->registered && client->nickname != command.params[0])
    {
        std::string old_nick = client->nickname;
        std::string msg = prefix(*client)+ " NICK :" + command.params[0];
        send_to_client(client->fd, msg);
    }
    client->nickname = command.params[0];
    client->nick_set = true;
    try_register(client);
}

void Server::pass_command(Client *client, Command command)
{
    // if (client->pass_ok)
    // {
    //     reply(client, "462", "PASS", "You may not reregister");
    //     return;
    // }

    if (command.params.size() != 1)
    {
        reply(client, "461", "PASS", "Not enough parameters");
        return;
    }

    if (command.params[0] != SERVER_PASSWORD)
    {
        reply(client, "464", "PASS", "Wrong password");
        return;
    }
    client->pass_ok = true;
}

void Server::user_command(Client *client, Command command)
{
    if (client->user_set)
    {
        reply(client, "462", "USER", "You may not reregister"); // user can be set only once and only if not registered before unlike nickname
        return;
    }

    if (command.params.size() != 4)
    {
        reply(client, "461", "USER", "Not enough parameters");
        return;
    }
    client->username = command.params[0];
    client->realname = command.params[3];
    std::cout << client->realname << "\n";
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

    std::cout<< command.cmd <<"\n";
    if (!client->pass_ok)
    {
        if (command.cmd == "PASS")
        {
            pass_command(client, command);
            return;
        }
        reply(client, "451", "*", "You have not registered");
        return;
    }
    if (command.cmd == "PASS")
    {
        if(client->registered)
            reply(client, "462", "PASS", "You may not reregister");
        else
           pass_command(client, command); 
    }

    else if (command.cmd == "NICK")
        nick_command(client, command);
    else if (command.cmd == "USER")
        user_command(client, command);
    else
        reply(client, "421", command.cmd, "Unknown command");
}


/* ---------------- BUFFER ---------------- */
Command Server::dispatch_user(tmp_cmd tmp)
{
    Command command;
    size_t pos;
    std::string param;

    std::string before_colon;
    std::string after_colon;



    command.cmd = tmp.cmd;
    size_t colon_index = tmp.arg.find(":");

    if(colon_index != std::string::npos)
    {
        before_colon = tmp.arg.substr(0, colon_index);
        after_colon = tmp.arg.substr(colon_index + 1); 
    }
    else
       before_colon = tmp.arg;

    while ((pos = before_colon.find_first_not_of(" \t")) != std::string::npos)
    {
        before_colon = before_colon.substr(pos);

        size_t space = before_colon.find(' ');

        if (space == std::string::npos)
        {
            command.params.push_back(before_colon);
            break;
        }
        command.params.push_back(before_colon.substr(0, space));
        before_colon = before_colon.substr(space + 1);
    }
    if(!after_colon.empty())
        command.params.push_back(after_colon);

    return(command);
}

Command Server::dispatch_pass_nick(tmp_cmd tmp)
{

    Command command;

    std::string rest;

    command.cmd = tmp.cmd;

    size_t space = tmp.arg.find_first_not_of(" \t");

    
    if (space == std::string::npos)
        command.params.push_back(tmp.arg);
    else
    {  
        
        rest  = tmp.arg.substr(space);
        size_t index = rest.find(' ');
        command.params.push_back(rest.substr(0, index));

    }
    return command;

}
tmp_cmd Server::command_name(std::string command_)
{
    tmp_cmd tmp;

    std::string rest;
    
    size_t index = command_.find_first_not_of(" \t");

    if(index == std::string::npos)
        return(tmp);

    rest = command_.substr(index); // the first not space char pos

    size_t space_pos = rest.find(' ');
    if(space_pos == std::string::npos)
    {
        tmp.cmd = rest;
        return(tmp);
    }
    tmp.cmd = rest.substr(0, space_pos);
    tmp.arg = rest.substr(space_pos + 1) ;
    return(tmp);
}

Command Server::parse_command(std::string command_)
{
    tmp_cmd tmp;
    Command command;

    tmp = command_name(command_);
    capitalize_command(tmp.cmd);

    if(tmp.cmd == "PASS" || tmp.cmd == "NICK")
        command = dispatch_pass_nick(tmp);
    else if(tmp.cmd == "USER")
        command = dispatch_user(tmp);

    return(command);
}

void Server::check_buffer(Client *client)
{
    size_t pos;
    std::string command_;
    Command command;

    while ((pos = client->buffer.find("\n")) != std::string::npos)
    {
        command_ = client->buffer.substr(0, pos);
        if (!command_.empty() && (command_[command_.size() - 1] =='\r'))
            command_.erase(command_.size() - 1);
        //add your command code here\n
        command = parse_command(command_);

        handle_command(client, command);
        client->buffer.erase(0, pos + 1);
    }
}
