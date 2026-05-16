CC = c++
CFLAGS = -Wall -Wextra -Werror -std=c++98

CLIENTSRS = client.cpp
SERVERSRS = server.cpp server_core.cpp handle_client.cpp handle_commads.cpp


HEADERS = server_client.hpp Server.hpp 

NAME_1 = client
NAME_2 = ircserv

CLIENTOBJS = $(CLIENTSRS:.cpp=.o)
SERVEROBJS = $(SERVERSRS:.cpp=.o)

all: $(NAME_1) $(NAME_2)

$(NAME_1): $(CLIENTOBJS)
	$(CC) $(CFLAGS) $(CLIENTOBJS) -o $(NAME_1)

$(NAME_2): $(SERVEROBJS)
	$(CC) $(CFLAGS) $(SERVEROBJS) -o $(NAME_2)

%.o: %.cpp $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CLIENTOBJS) $(SERVEROBJS) 

fclean: clean
	rm -f $(NAME_1) $(NAME_2)

re: fclean all
