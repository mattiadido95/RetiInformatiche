#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define FILE_BUFFER_SIZE 512
#define PACKET_SIZE 516
#define REQ_SIZE 516
#define ACK_SIZE 4
#define IP_SIZE 16

int create_error(uint16_t, char* , const char*);
int create_text_pack(uint16_t, char*, FILE*, unsigned int);
int create_bin_pack(uint16_t, char*, FILE*, unsigned int);

struct read_request {
	int sd;
	struct sockaddr_in client_addr;
	FILE* fp;
	int remaining_packs;
	char* mode;
	int block;
	struct read_request* next;
} req_list;

void init_req_list();
void insert_req(int, struct sockaddr_in, FILE*, int, char*, int);
void delete_req(int);
struct read_request* findRequest(int);


int main(int argc, char** argv)
{

	if(argc != 3)
	{
		//ERRORE->se avvio il server senza aver spercificato porta e directory dei files
		printf("\nDigitare ./tftp_server <porta> <directory files> per avviare il programma.\n\n"); 
		return 0;
	}
	
	int port = atoi(argv[1]); //porta del server
	char* dir = argv[2]; //directory dei file

	//set di descrittori
	fd_set read_fds;
	fd_set master;
	int fdmax, newfd;	

	int listener, ret, buffer_index, i;
	unsigned int addrlen;
	struct sockaddr_in my_addr, client_addr;	

	//Creazione socket di ascolto per richieste da client
	listener = socket(AF_INET, SOCK_DGRAM,0);

	//Creazione indirizzo per il socket di ascolto
	memset(&my_addr, 0, sizeof(my_addr)); //pulisco la struttura
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = INADDR_ANY;
	printf("\nIl socket listener è stato creato.\n"); 

	ret = bind(listener, (struct sockaddr*)&my_addr, sizeof(my_addr)); //bind su listener
	if(ret < 0)
	{
		perror("[ERRORE] -> Bind non riuscita");
		exit(0);
	}

	//reset FD
	FD_ZERO(&master);
	FD_ZERO(&read_fds);

	//aggiungo il socket listener al set dei descrittori
	FD_SET(listener, &master);
	fdmax = listener;

	char fileName[BUFFER_SIZE]; //nome file
	char mode[BUFFER_SIZE]; //modalita di trasferimento
	char buffer_pack[PACKET_SIZE]; //messaggi ricevuti
	char buffer_error[BUFFER_SIZE]; //messaggi di errore
	char ip_client[IP_SIZE];

	init_req_list(); //lista richieste inizializzata

	while(1)
	{
		read_fds = master;
		//mi metto in attesa di descrittori pronti
		select(fdmax+1, &read_fds, NULL, NULL, NULL);
		
		for(i = 0; i <= fdmax; i++) //scorro il set di descrittori
		{			
			if(FD_ISSET(i, &read_fds)) //controllo se c'è qualche descrittore nel set read_fds
			{
				if(i == listener) //se è il socket di ascolto allora
				{
					addrlen = sizeof(client_addr);
					memset(buffer_pack, 0, BUFFER_SIZE);

					ret = recvfrom(i, buffer_pack, REQ_SIZE, 0, (struct sockaddr*)&client_addr, &addrlen);					
					if(ret < 0)
					{
						perror("[ERRORE] -> problema nella recvfrom().\n");
						exit(0);
					}

					uint16_t opcode, err_code;
					memcpy(&opcode, buffer_pack, 2); //estraggo l'opcode dal messaggio ricevuto				
					opcode = ntohs(opcode);
					
					//se non ho ricevuto un RRQ (opcode=1) o ACK (opcode=4) 
					if(opcode != 1 && opcode != 4)
					{
						buffer_index = 0;
						char *err_msg = "Operazione non consentita.\n"; //messaggio di errore
						err_code = htons(2); //2 codice errore

						memset(buffer_error, 0, BUFFER_SIZE);
						buffer_index = create_error(err_code, buffer_error, err_msg);
						//buffer_index = lunghezza del messaggio di errore da spedire

						newfd = socket(AF_INET, SOCK_DGRAM, 0); //socket per inviare il messaggio di errore

						ret = sendto(newfd, buffer_error, buffer_index, 0,(struct sockaddr*)&client_addr, sizeof(client_addr));
						if(ret < 0)
						{
							perror("\n[ERRORE] -> Invio del messaggio di errore non riuscito\n");
							exit(0);
						}
						close(newfd);
						
						continue; //salto al for 
					}
					//se ho una richiesta RRQ (opcode=1)
					if(opcode == 1) 
					{
						newfd = socket(AF_INET, SOCK_DGRAM, 0); //nuovo socket per gestire la richiesta

						buffer_index = 0;
				
						memset(fileName, 0, BUFFER_SIZE);
						strcpy(fileName, buffer_pack+2); //prendo il nome del file dal messaggio ricevuto a partire dal secondo byte
						strcpy(mode, buffer_pack + (int)strlen(fileName) + 3); //prendo la modalita dal messaggio ricevuto dopo una lunghezza fileName, opcode e 0x00

						memset(ip_client, 0, IP_SIZE);
						inet_ntop(AF_INET, &client_addr, ip_client, IP_SIZE);

						printf("\nC'è una richiesta di download del file %s in modalità %s da %s\n", fileName, mode, ip_client);

						//costruisco il percorso dove si trova il file richiesto
						char* path = malloc(strlen(dir)+strlen(fileName)+2); 
						strcpy(path, dir);
						strcat(path, "/");
						strcat(path, fileName); 
						
						FILE* fp;
						if(!strcmp(mode, "netascii\0"))
							fp = fopen(path, "r"); //apertura file in lettura per trasferimento testuale
						else
							fp = fopen(path, "rb"); //apertura file in lettura per trasferimento binario
						
						free(path);

						//se file non trovato
						if(fp == NULL)
						{							
							char *err_msg = "File richiesto non trovato";
							buffer_index = 0;
							err_code = htons(1);

							memset(buffer_error, 0, BUFFER_SIZE);
							buffer_index = create_error(err_code, buffer_error, err_msg);

							//trasmetto al client il messaggio di errore per file non trovato
							newfd = socket(AF_INET, SOCK_DGRAM, 0);
			
							ret = sendto(newfd, buffer_error, buffer_index, 0,(struct sockaddr*)&client_addr, sizeof(client_addr));
							if(ret < 0)
							{
								perror("\n[ERRORE] -> Invio del messaggio di errore non riuscito\n");
								exit(0);
							}

							close(newfd);
							continue;
						}
						//se file trovato
						else 						
						{
							printf("\nLa lettura del file %s è riuscita\n", fileName);

							//modalita txt
							if(!strcmp(mode, "netascii\0")) 
							{	
								// Lettura della lunghezza del contenuto del file	
								unsigned int length = 0;
								while(fgetc(fp) != EOF) //calcolo lunghezza del file
									length++;

								//Reimposta l'indicatore di posizione del file all'inizio
								fseek(fp, 0 , SEEK_SET);
								
								//controllo se segmentare l'invio del file
								unsigned int dim_pack = 0;								
								if(length > FILE_BUFFER_SIZE)
									{
										dim_pack = FILE_BUFFER_SIZE;
									}
								else	
									{
										dim_pack = length;
									}

								insert_req(newfd, client_addr, fp, length-dim_pack, mode, 0);//aggiungo in lista la richiesta

								// Lettura ed invio di un blocco							
								uint16_t block_num = htons(1);
								buffer_index = create_text_pack(block_num, buffer_pack, fp, dim_pack);

								ret = sendto(newfd, buffer_pack, buffer_index, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
								if(ret < 0) 
								{
									perror("[ERRORE] -> durante la send del blocco al client");
									exit(0);
								}
							}
							//modalità bin
							else 
							{								
								//Imposta l'indicatore di posizione del file alla fine dello stesso
								fseek(fp, 0 , SEEK_END);
								//Ritorna la posizione corrente nel file
								unsigned int length = ftell(fp);
								//Resetto l'indicatore
								fseek(fp, 0 , SEEK_SET);

								//controllo se segmentare l'invio del file
								unsigned int dim_pack = 0;								
								if(length > FILE_BUFFER_SIZE)
									{
										dim_pack = FILE_BUFFER_SIZE;
									}
								else	
									{
										dim_pack = length;
									}
								
								insert_req(newfd, client_addr, fp, length-dim_pack, mode, 0);//aggiungo in lista la richiesta

								uint16_t block_num = htons(1);
								buffer_index = create_bin_pack(block_num,buffer_pack, fp, dim_pack);

								ret = sendto(newfd, buffer_pack, buffer_index, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
								if(ret < 0)
								{
									perror("[ERRORE] -> durante la send del blocco al client");
									exit(0);
								}
						
							}
							//aggiungo la richiesta al set
							FD_SET(newfd, &master); 							
							if(newfd > fdmax) 
								fdmax = newfd;	
						}
					}

				} 
				//non sono il listener ma un socket di comunicazione
				else 
				{
					addrlen = sizeof(client_addr);
					memset(buffer_pack, 0, BUFFER_SIZE);

					//dimensione ACK fissa (ACK_SIZE)
					ret = recvfrom(i, buffer_pack, ACK_SIZE, 0, (struct sockaddr*)&client_addr, &addrlen);					
					if(ret < 0)
					{
						perror("[ERRORE] -> problema nella recvfrom()\n");
						exit(0);
					}

					uint16_t opcode;
					memcpy(&opcode, buffer_pack, 2);				
					opcode = ntohs(opcode);

					//se ho un ACK (opcode=4)
					if(opcode == 4) 
					{						
						struct read_request* req = findRequest(i); //cerco la richiesta di indice i
						
						//ho pacchetti non ancora trasmessi
						if(req->remaining_packs > 0)
						{ 
							unsigned int dim_pack = (req->remaining_packs > FILE_BUFFER_SIZE)?FILE_BUFFER_SIZE:req->remaining_packs;
							req->block++;

							if(req->remaining_packs == FILE_BUFFER_SIZE)
								req->remaining_packs = 1; //invio un pacchetto vuoto quando è multiplo di 512 per segnalare la fine trasmissione
							else
								req->remaining_packs -= dim_pack;				
							
							// Lettura ed invio di un blocco
							uint16_t block_num = htons(req->block);

							//modalità testo
							if(!strcmp(req->mode, "netascii\0")) 
							{
								buffer_index = create_text_pack(block_num, buffer_pack, req->fp, dim_pack);
							}
							//modalità bin
							else 
							{
								buffer_index = create_bin_pack(block_num, buffer_pack, req->fp, dim_pack);
							}

							ret = sendto(i, buffer_pack, buffer_index, 0, (struct sockaddr*)&req->client_addr, sizeof(req->client_addr));
							memset(buffer_pack, 0, FILE_BUFFER_SIZE);

						}
						//tutto il file è stato trasmesso
						else 
						{
							memset(ip_client, 0, IP_SIZE);
							inet_ntop(AF_INET, &req->client_addr, ip_client, IP_SIZE);
							printf("\nL'intero file è stato trasferito con successo al client %s\n", ip_client);
							delete_req(i);
							close(i);
							FD_CLR(i, &master);
						}	
					}
				}
			}
		}
	}
	close(listener);

}


int create_error(uint16_t error_code, char* buffer_error, const char* error_msg)
{
	printf("\n%s\n", error_msg);

	uint16_t opcode = htons(5); //opcode di errore da specifica
	int buffer_index = 0; //inizio pack errore
	
	memcpy(buffer_error, (uint16_t*)&opcode, 2);//copio l'opcode su due bytes
	buffer_index += 2; //mi sposto di due in avanti

	memcpy(buffer_error+buffer_index, (uint16_t*)&error_code, 2);
	buffer_index += 2; //mi sposto di due in avanti

	strcpy(buffer_error+buffer_index, error_msg);
	buffer_index += strlen(error_msg)+1;
	buffer_index++; //0x00 finale

	return buffer_index;
}


int create_text_pack(uint16_t block_num, char* buffer_pack, FILE* fp, unsigned int dim_pack)
{
	uint16_t opcode = htons(3);
	int buffer_index = 0;
	char buffer_file[FILE_BUFFER_SIZE];
	memset(buffer_file, 0, FILE_BUFFER_SIZE);

	fread(buffer_file, dim_pack, 1, fp);

	memcpy(buffer_pack + buffer_index, (uint16_t*)&opcode, 2);
	buffer_index += 2;
	memcpy(buffer_pack + buffer_index, (uint16_t*)&block_num, 2);
	buffer_index += 2;
	strcpy(buffer_pack + buffer_index, buffer_file);
	buffer_index += dim_pack;

	return buffer_index;
}


int create_bin_pack(uint16_t block_num, char* buffer_pack, FILE* fp, unsigned int dim_pack)
{
	u_int16_t opcode = htons(3);
	int buffer_index = 0;
	char buffer_file[FILE_BUFFER_SIZE];
	memset(buffer_file, 0, FILE_BUFFER_SIZE);	
	fread(buffer_file, dim_pack, 1, fp);

	memcpy(buffer_pack, (uint16_t*)&opcode, 2);
	buffer_index += 2;
	memcpy(buffer_pack + buffer_index, (uint16_t*)&block_num, 2);
	buffer_index += 2;

	memcpy(buffer_pack + buffer_index, buffer_file, dim_pack);
	buffer_index += dim_pack;

	return buffer_index;
}



void init_req_list()
{
	req_list.sd = -1;
	req_list.fp = NULL;
	req_list.remaining_packs = 0;
	req_list.next = NULL;
}

void insert_req(int sock, struct sockaddr_in client_addr, FILE* fp, int packets,char* mode, int block)
{
	struct read_request *req = malloc(sizeof(struct read_request));
	req->sd = sock;
	req->client_addr = client_addr;
	req->fp = fp;
	req->remaining_packs = packets;
	req->mode = malloc(sizeof(mode)+1);
	strcpy(req->mode, mode);
	req->block = block;
	req->next = NULL;

	if(req_list.next == NULL)
	{
		req_list.next = req;
		return;
	}

	struct read_request* prec = req_list.next;
	struct read_request* tmp = NULL;
	while(prec != NULL)
	{
		tmp = prec;
		prec = prec->next;
	}

	tmp->next = req;
	return;
}



void delete_req(int sock)
{
	struct read_request *req = req_list.next;
	struct read_request* prec = NULL;

	while(req)
	{
		if(sock == req->sd)
			break;
		prec = req;
		req = req-> next;
	}

	fclose(req->fp);

	if(req)
	{
		if(req == req_list.next)
			req_list.next = req->next;
		else
			prec->next = req->next;
		
	}
}

struct read_request* findRequest(int sock)
{
	struct read_request* req = &req_list;
	while(req)
	{
		if(req->sd == sock)
			break;
		req = req->next;
	}
 return req;
}
