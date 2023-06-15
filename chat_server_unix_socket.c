#include <stdio.h>      // for perror, puts
#include <string.h>     // for memset, strcpy
#include <unistd.h>     // for sleep()
#include <sys/un.h>     // for sockaddr_un
#include <sys/socket.h> // for socket, AF_UNIX, SOCK_STREAM, socklen_t
#include <sys/stat.h>   // for S_IRWXU, S_IRWXG, S_IROTH, S_IXOTH
#include <stdlib.h>     // for EXIT_SUCCESS, EXIT_FAILURE, strtol()
#include <stdbool.h>    // for bool, true, false
#include <ctype.h>      // for isdigit()
#include <limits.h>     // for INT_MAX
#include <signal.h>     // for signal(), SIGINT
#include <pthread.h>    // for pthread_create(), pthread_join()

#define SOCKET_PATH "/tmp/socket_repo"
#define SOCKET_FILE "/tmp/socket_repo/my_unix_socket"
/**
 * Tamanho do buffer de entrada de dados do usuário
 *
 * O valor definido no BUFFER_SIZE deve ser o mesmo do BUFFER_SIZE
 * definido no arquivo chat_client_unix_socket.c
 *
 * O tamanho máximo que o usuário pode digitar é BUFFER_SIZE - 2
 */
#define BUFFER_SIZE 256
char keyboardInput[BUFFER_SIZE];

struct SocketState
{
  int socketFileDescriptor;
  int connectionFileDescriptor;
  int maxConnections;
  bool stopOperation;
  bool deregistrationExecuted;
  bool fullConnectionEstablished;
  pthread_t messageListenerThread;
  pthread_t userInputListenerThread;
  socklen_t addressSize;
  struct sockaddr_un socketAddress;
  char buffer[BUFFER_SIZE];
};

struct SocketState socketState;

void initSocketState(struct SocketState *socketState);
void unregisterSocket();                                                                              // método somente para o criador do socket (server)
void setMaxConnetions(int argumentsCounter, char *argumentValues[], struct SocketState *socketState); // método somente para o criador do socket (server)
void createSocket(struct SocketState *socketState);
void configureSocketAddress(struct SocketState *socketState);
void bindSocket(struct SocketState *socketState);           // método somente para o criador do socket (server)
void listenForConnections(struct SocketState *socketState); // método somente para o criador do socket (server)
void acceptConnection(struct SocketState *socketState);     // método somente para o criador do socket (server)
void messageListener(struct SocketState *socketState);
void userInputListener(struct SocketState *socketState);
void createThreads(struct SocketState *socketState);
void waitForThreads(struct SocketState *socketState);

int main(int argumentsCounter, char *argumentValues[])
{
  /**
   * Esse sequência de funções é a ordem que deve ser seguida para a criação de um socket
   * e foi logicamente pensada para preparar o ambiente do socket.
   *
   * Caso for alterada a ordem de alguma das funções, o programa pode falhar.
   */
  puts("[server]");
  initSocketState(&socketState);
  signal(SIGINT, unregisterSocket);
  setMaxConnetions(argumentsCounter, argumentValues, &socketState);
  createSocket(&socketState);
  configureSocketAddress(&socketState);
  bindSocket(&socketState);
  listenForConnections(&socketState);
  acceptConnection(&socketState);
  createThreads(&socketState);
  waitForThreads(&socketState);
  return EXIT_SUCCESS;
}

void initSocketState(struct SocketState *socketState) // ok
{
  /**
   * Cuidado ao alterar esses valores iniciais, pois muitos
   * deles fazem parte da lógica do programa.
   */
  socketState->socketFileDescriptor = -1;
  socketState->connectionFileDescriptor = -1;
  socketState->maxConnections = 1;
  socketState->stopOperation = false;
  socketState->deregistrationExecuted = false;
  socketState->fullConnectionEstablished = false;
  socketState->messageListenerThread = -1;
  socketState->userInputListenerThread = -1;
  socketState->addressSize = 0;
  memset(socketState->buffer, 0, BUFFER_SIZE);
  memset(socketState->socketAddress.sun_path, 0, sizeof(socketState->socketAddress.sun_path));
}

void unregisterSocket() // ok
{
  if (socketState.deregistrationExecuted == true)
    return;

  puts("\ndesregistrando o socket...");
  bool opSucess = true;
  if (socketState.fullConnectionEstablished == true && close(socketState.connectionFileDescriptor) == -1)
  {
    perror("falha ao desregistrar descritor de arquivo da conexão");
    opSucess = false;
  }
  if (close(socketState.socketFileDescriptor) == -1)
  {
    perror("falha ao desregistrar descritor de arquivo do socket");
    opSucess = false;
  }
  if (unlink(socketState.socketAddress.sun_path) == -1)
  {
    perror("falha ao desvincular o arquivo socket, provavelmente já foi desvinculado pelo outro processo ou é inexistente");
    opSucess = false;
  }

  socketState.deregistrationExecuted = true;
  socketState.stopOperation = true;

  puts(opSucess ? "socket desregistrado!" : "socket desregistrado, porem algumas etapas falharam!");

  if (socketState.fullConnectionEstablished == true)
  {
    if (socketState.messageListenerThread != -1 && pthread_kill(socketState.messageListenerThread, SIGKILL) != 0)
      perror("falha ao matar thread de ouvinte de mensagens");
    if (socketState.userInputListenerThread != -1 && pthread_kill(socketState.userInputListenerThread, SIGKILL) != 0)
      perror("falha ao matar thread de ouvinte de entrada de usuário");
  }
}

void createSocket(struct SocketState *socketState) // ok
{
  puts("criando o socket unix domain...");
  socketState->socketFileDescriptor = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socketState->socketFileDescriptor == -1)
  {
    perror("falha ao criar o socket");
    exit(EXIT_FAILURE);
  }
  if (unlink(SOCKET_FILE) == -1)
  {
    perror("falha ao desvincular o arquivo socket, provavelmente já foi desvinculado pelo outro processo ou é inexistente");
  }
  puts("socket criado!");
}

void configureSocketAddress(struct SocketState *socketState) // ok
{
  puts("configurando o endereço do socket...");
  socketState->socketAddress.sun_family = AF_UNIX;
  // esse peguei na net, confesso que não entendi esses pipes, somente as constantes que são definidas no header sys/stat.h
  mkdir(SOCKET_PATH, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  strcpy(socketState->socketAddress.sun_path, SOCKET_FILE);
  socketState->addressSize = sizeof(socketState->socketAddress);
}

void bindSocket(struct SocketState *socketState) // ok
{
  puts("ligando o socket...");
  if (bind(socketState->socketFileDescriptor, (struct sockaddr *)&socketState->socketAddress, socketState->addressSize) == -1)
  {
    perror("falha ao ligar o socket");
    exit(EXIT_FAILURE);
  }
  puts("socket ligado!");
}

void listenForConnections(struct SocketState *socketState) // ok
{
  puts("registrando ouvintes de conexão...");
  if (listen(socketState->socketFileDescriptor, socketState->maxConnections) == -1)
  {
    perror("falha ao registrar ouvintes de conexão");
    exit(EXIT_FAILURE);
  }
  puts("ouvintes registrados!");
}

void acceptConnection(struct SocketState *socketState) // ok
{
  puts("aceitando conexão...");
  socketState->connectionFileDescriptor = accept(socketState->socketFileDescriptor, (struct sockaddr *)&socketState->socketAddress, &socketState->addressSize);
  if (socketState->connectionFileDescriptor == -1)
  {
    perror("falha ao aceitar conexão");
    exit(EXIT_FAILURE);
  }
  socketState->fullConnectionEstablished = true;
  puts("conexão aceita!");
}

void setMaxConnetions(int argumentsCounter, char *argumentValues[], struct SocketState *socketState) // ok
{
  socketState->maxConnections = 1;
  if (argumentsCounter > 1)
  {
    char *endptr;
    long int number = strtol((argumentValues[1]), &endptr, 10);
    if (number > INT_MAX)
    {
      printf("o valor passado é muito grande, valor máximo é de [%d], usando 1 conexões como padrão!\n", INT_MAX);
      return;
    }

    if (*endptr == '\0')
    {
      printf("socket configurado para aceitar [%ld] conexões simultâneas\n", number);
      socketState->maxConnections = (int)number;
    }

    else
      puts("argumento inválido, usando 1 conexões como padrão!");
  }
  else
    puts("sem argumentos, usando 1 conexões como padrão!");
}

void messageListener(struct SocketState *socketState) // ok
{
  ssize_t status = 1;
  while (status != 0 && status != -1 && socketState->stopOperation == false)
  {
    status = recv(socketState->connectionFileDescriptor, socketState->buffer, BUFFER_SIZE, 0);
    if (status == -1)
      perror("falha ao receber a mensagem");
    else if (status == 0)
      perror("conexão encerrada");
    else
      printf("%s", socketState->buffer);
  }
  unregisterSocket();
}

void userInputListener(struct SocketState *socketState) // ok
{
  char msgString[BUFFER_SIZE];
  bool opSucess = true;
  while (opSucess == true && socketState->stopOperation == false)
  {
    memset(msgString, 0, BUFFER_SIZE);
    memset(keyboardInput, 0, BUFFER_SIZE);
    setbuf(stdin, keyboardInput);

    if (fgets(msgString, BUFFER_SIZE, stdin) == NULL)
    {
      perror("falha ao ler a mensagem do input");
      opSucess == false;
    }
    else if (send(socketState->connectionFileDescriptor, msgString, strlen(msgString) + 1, 0) == -1)
    {
      perror("falha ao enviar a mensagem");
      opSucess == false;
    }
  }
  unregisterSocket();
}

void createThreads(struct SocketState *socketState) // ok
{
  if (pthread_create(&socketState->messageListenerThread, NULL, (void *)messageListener, (void *)socketState) != 0)
  {
    perror("falha ao criar thread de ouvinte de mensagens");
    unregisterSocket();
    exit(EXIT_FAILURE);
  }

  if (pthread_create(&socketState->userInputListenerThread, NULL, (void *)userInputListener, (void *)socketState) != 0)
  {
    perror("falha ao criar thread de ouvinte de entrada de usuário");
    unregisterSocket();
    exit(EXIT_FAILURE);
  }
  sleep(2);
  system("clear");
  puts("para encerrar o programa pressione [CTRL+C]");
  puts("digite um texto de no máximo (BUFFER_SIZE - 2), e pressione [ENTER]");
  puts("[server@localhost]:\n");
}

void waitForThreads(struct SocketState *socketState) // ok
{
  if (pthread_join(socketState->messageListenerThread, NULL) != 0)
  {
    perror("falha ao esperar thread de ouvinte de mensagens");
    unregisterSocket();
    exit(EXIT_FAILURE);
  }

  if (pthread_join(socketState->userInputListenerThread, NULL) != 0)
  {
    perror("falha ao esperar thread de ouvinte de entrada de usuário");
    unregisterSocket();
    exit(EXIT_FAILURE);
  }
}
