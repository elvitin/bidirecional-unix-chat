#include <stdio.h>      // for puts, perror
#include <stdlib.h>     // for EXIT_SUCCESS, EXIT_FAILURE
#include <stdbool.h>    // for bool, true, false
#include <string.h>     // for memset, strcpy
#include <signal.h>     // for signal, SIGINT, SIGKILL
#include <unistd.h>     // for close, unlink, getpid, sleep
#include <sys/un.h>     // for sockaddr_un
#include <sys/socket.h> // for socket, AF_UNIX, SOCK_STREAM, socklen_t
#include <sys/stat.h>   // for S_IRWXU, S_IRWXG, S_IROTH, S_IXOTH
#include <pthread.h>    // for pthread_create, pthread_join

#define SOCKET_PATH "/tmp/socket_repo"
#define SOCKET_FILE "/tmp/socket_repo/my_unix_socket"
#define BUFFER_SIZE 256
char keyboardInput[BUFFER_SIZE];

struct SocketState
{
  int socketFileDescriptor;
  socklen_t addressSize;
  pthread_t messageListenerThread;
  pthread_t userInputListenerThread;
  bool stopOperation;
  bool deregistrationExecuted;
  bool fullConnectionEstablished;
  struct sockaddr_un socketAddress;
  char buffer[BUFFER_SIZE];
};

struct SocketState socketState;

void initSocketState(struct SocketState *socketState);
void unregisterSocket();
void createSocket(struct SocketState *socketState);
void configureSocketAddress(struct SocketState *socketState);
void establishConnection(struct SocketState *socketState);
void messageListener(struct SocketState *socketState);
void userInputListener(struct SocketState *socketState);
void createThreads(struct SocketState *socketState);
void waitForThreads(struct SocketState *socketState);

int main(void)
{
  /**
   * Esse sequência de funções é a ordem que deve ser seguida para a criação de um socket
   * e foi logicamente pensada para preparar o ambiente do socket.
   *
   * Caso for alterada a ordem de alguma das funções, o programa pode falhar.
   */
  puts("[client]");
  initSocketState(&socketState);
  signal(SIGINT, unregisterSocket);
  createSocket(&socketState);
  configureSocketAddress(&socketState);
  establishConnection(&socketState);
  createThreads(&socketState);
  waitForThreads(&socketState);
  return EXIT_SUCCESS;
}

void initSocketState(struct SocketState *socketState)
{
  /**
   * Cuidado ao alterar esses valores iniciais, pois muitos
   * deles fazem parte da lógica do programa.
   */
  socketState->socketFileDescriptor = -1;
  socketState->addressSize = 0;
  socketState->stopOperation = false;
  socketState->deregistrationExecuted = false;
  socketState->fullConnectionEstablished = false;
  socketState->messageListenerThread = -1;
  socketState->userInputListenerThread = -1;
  memset(socketState->buffer, 0, BUFFER_SIZE);
  memset(socketState->socketAddress.sun_path, 0, sizeof(socketState->socketAddress.sun_path));
}

void unregisterSocket()
{
  if (socketState.deregistrationExecuted == true)
    return;

  puts("\ndesregistrando o socket...");
  bool opSucess = true;
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
  puts(opSucess ? "socket desregistrado!" : "socket desregistrado, porem algumas etapas falharam");
  if (socketState.fullConnectionEstablished == true)
  {
    pthread_kill(socketState.messageListenerThread, SIGKILL);
    pthread_kill(socketState.userInputListenerThread, SIGKILL);
  }
  else if (socketState.fullConnectionEstablished == false)
  {
    kill(getpid(), SIGKILL);
  }
}

void createSocket(struct SocketState *socketState)
{
  puts("criando o socket unix domain...");
  socketState->socketFileDescriptor = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socketState->socketFileDescriptor == -1)
  {
    perror("falha ao criar o socket");
    exit(EXIT_FAILURE);
  }
  puts("socket criado!");
}

void configureSocketAddress(struct SocketState *socketState)
{
  puts("configurando o endereço do socket...");
  socketState->socketAddress.sun_family = AF_UNIX;
  mkdir(SOCKET_PATH, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  strcpy(socketState->socketAddress.sun_path, SOCKET_FILE);
  socketState->addressSize = sizeof(socketState->socketAddress);
}

void establishConnection(struct SocketState *socketState)
{
  puts("estabelecendo conexão...");
  if (connect(socketState->socketFileDescriptor, (struct sockaddr *)&socketState->socketAddress, socketState->addressSize) == -1)
  {
    perror("falha ao estabelecer conexão");
    sleep(2);
    puts("tentando novamente...");
    if (socketState->stopOperation == false)
      establishConnection(socketState);
  }
  else
  {
    socketState->fullConnectionEstablished = true;
    puts("conexão estabelecida!");
  }
}

void messageListener(struct SocketState *socketState)
{
  ssize_t status = 1;
  while (status != 0 && status != -1 && socketState->stopOperation == false)
  {
    status = recv(socketState->socketFileDescriptor, socketState->buffer, BUFFER_SIZE, 0);
    if (status == -1)
      perror("falha ao receber a mensagem");
    else if (status == 0)
      perror("conexão encerrada");
    else
      printf("%s", socketState->buffer);
  }
  unregisterSocket();
}

void userInputListener(struct SocketState *socketState)
{
  char msgString[BUFFER_SIZE];
  bool opSucess = true;
  while (opSucess && socketState->stopOperation == false)
  {
    memset(msgString, 0, BUFFER_SIZE);
    memset(keyboardInput, 0, BUFFER_SIZE);
    setbuf(stdin, keyboardInput);

    if (fgets(msgString, BUFFER_SIZE, stdin) == NULL)
    {
      perror("falha ao ler a mensagem do input");
      opSucess = false;
    }
    else if (send(socketState->socketFileDescriptor, msgString, strlen(msgString) + 1, 0) == -1)
    {
      perror("falha ao enviar mensagem");
      opSucess = false;
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
  puts("[client@localhost]:\n");
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