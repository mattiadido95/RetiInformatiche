#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define BUFFER_SIZE 1024
#define FILE_BUFFER_SIZE 512
#define PACKET_SIZE 516

#define CMD_SIZE 50

void initMessage(int, const char*, int, struct sockaddr_in);
void help();
void get(int, char*, char*, char*, struct sockaddr_in);
void mode(char*, char*);
void quit();


int main(int argc, char** argv)
{
	if(argc != 3)
	{
		printf("\nDigitare ./tftp_client <IP_server> <porta_server> per avviare correttamente il programma\n");
		return 0;
	}

	int sd, port;
	struct sockaddr_in server_addr; //server

	char input_cmd[CMD_SIZE];	

	port = atoi(argv[2]); //porta server passata da terminale

	//creazione socket
	sd = socket(AF_INET, SOCK_DGRAM, 0);

	//creazione indirizzo del server
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	inet_pton(AF_INET, argv[1], &server_addr.sin_addr);

	char transfer_mode[CMD_SIZE]; 
	//di default il modo di trasferimento binario (octet = binary)
	strcpy(transfer_mode,"octet\0");

	initMessage(sd, argv[1], port, server_addr); //successo

	while(1)
	{
		
		memset(&input_cmd, 0, CMD_SIZE); 
		printf("\nInserisci il comando: -->");

		fgets(input_cmd, CMD_SIZE, stdin);
		//strtok mi splitta l'input sul delimitatore \n poi " "
		char *cmd = strtok(input_cmd, "\n");  
		cmd = strtok(cmd, " "); 

		if(!strcmp(cmd, "!help\0"))
		{
			help();
		} 
		else if(!strcmp(cmd, "!mode\0"))
		{
			//pulisco il \n
			char *new_mode = strtok(NULL, "\n");
			
			if(new_mode == NULL) //dopo il comando !mode non ho specificato altro quindi rimane quello di defoult
			{
				printf("[ERRORE] -> modalita di trasferimento non specificata, inserire {txt|bin}.\n");
				continue;
			}
			mode(new_mode, transfer_mode); //setto la modalita scelta
		} 
		else if(!strcmp(cmd, "!get\0"))
		{
			char *file_name = strtok(NULL, " ");
			char *local_name = strtok(NULL, "\n");
			if(local_name == NULL)
			{
				printf("[ERRORE] -> filename o nome_locale non specificato, inserire {filename} e {nome_locale}.\n");
				continue;
			}
			get(sd, file_name, local_name, transfer_mode, server_addr); //invio richiesta
		}
		else if(!strcmp(cmd, "!quit\0"))
		{
			quit(sd);
		} 
		else
		{
			printf("\nOperazione non prevista, digita !help per la lista dei comandi\n");	
		}
	}
 return 0;
}



void initMessage(int sd, const char* server, int port, struct sockaddr_in server_addr)
{
	printf("Sei in comunicazione con %s sulla porta %d\n", server,port);
	help();
}

void help()
{
	printf("\nSono disponibili i seguenti comandi:\n"
				"!help --> mostra l'elenco dei comandi disponibili\n"
				"!mode {txt|bin} --> imposta il modo di trasferimento dei files (testo o binario)\n"
				"!get filename nome_locale --> richiede al server il nome del file <filename> e lo salva localmente con il nome <nome_locale>\n"
				"!quit --> termina il client\n");
}

void mode(char* new_mode, char* current_mode){
	if(!strcmp(new_mode, "txt"))
	{	
		printf("Modalita di trasferimento txt configurata.\n");
		strcpy(current_mode,"netascii\0");
	} 
		else if(!strcmp(new_mode, "bin"))
	{
		printf("Modalita di trasferimento bin configurata.\n");
		strcpy(current_mode,"octet\0");
	} 
	else 
	{
		printf("[ERRORE] -> Modalita di trasferimento non prevista, inserire {txt|bin}.\n");
	}
	return;	
}

void get(int sd, char* file_name, char* local_name, char* transfer_mode, struct sockaddr_in server_addr)
{
	FILE* fp;
	if(!strcmp(transfer_mode, "netascii\0"))
	{
		fp = fopen(local_name, "w+"); //trasferimento txt
	} else {
		fp = fopen(local_name, "wb+"); //trasferimento bin
	}

	if(fp == NULL)
	{
		printf("\n [ERRORE] -> apertura del file non riuscita.\n");
		return;
	}

	printf("\nRichiesta file %s al server in corso.\n", file_name);

	unsigned int addrlen = sizeof(server_addr);

	char rrq[BUFFER_SIZE];//buffer per il messaggio RRQ
	memset(&rrq, 0, BUFFER_SIZE); 
	
	uint16_t opcode = htons(1); //OPCODE 1=RRQ

	uint16_t file_name_len = strlen(file_name);

	int buffer_index = 0; //indice del pacchetto
	long long transfers = 0;  //numero di blocchi trasferiti
	
	//creo richiesta RRQ
	memcpy(rrq, (uint16_t*)&opcode, 2);
	buffer_index += 2;

	strcpy(rrq + buffer_index, file_name);
	buffer_index += file_name_len + 1; 

	strcpy(rrq + buffer_index, transfer_mode);
	buffer_index += strlen(transfer_mode)+1;
	
	//invio la richiesta RRQ
	int ret = sendto(sd, rrq, buffer_index, 0,(struct sockaddr*)&server_addr, sizeof(server_addr));
	if(ret < 0)
	{
		perror("[ERRORE] -> nella send di RRQ.\n");
		exit(0);
	}

	char file[BUFFER_SIZE];
	char ack_packet[BUFFER_SIZE];

	while(1)
	{			
		char buffer_pack[BUFFER_SIZE];
		memset(&buffer_pack, 0, BUFFER_SIZE);	
	
		//ricezione del blocchi	
		buffer_index = 0;	
		memset(&file, 0, FILE_BUFFER_SIZE);

		int byte_rcv = recvfrom(sd, buffer_pack, PACKET_SIZE, 0, (struct sockaddr*)&server_addr, &addrlen);
		if(byte_rcv < 0) 
		{
			perror("[ERRORE] -> ricezione dei dati non riuscita");
			exit(0);
		}

		memcpy(&opcode, buffer_pack, 2);
		opcode = ntohs(opcode);
		buffer_index += 2;
		//ho ricevuto un messaggio di errore
		if(opcode == 5)
		{ 
			uint16_t error_number;
			memcpy(&error_number, buffer_pack+buffer_index, 2); //copio il codice errore prensente nel messaggio ricevuto			
			error_number = ntohs(error_number);
			buffer_index += 2;

			char error_msg[BUFFER_SIZE];
			memset(&error_msg, 0, BUFFER_SIZE);
			strcpy(error_msg, buffer_pack+buffer_index); //copio il messaggio di errore prensente 

			printf("\n[ERRORE] -> (%d) %s\n",error_number, error_msg);
			remove(local_name);  //elimino il file prima creato 
			return;
		}
		//non ho ricevuto un errore quindi ho dati del file
		if(transfers == 0)
			printf("\nTrasferimento del file in corso.\n");
		
		uint16_t block_number;
		memcpy(&block_number, buffer_pack + buffer_index, 2);
		
		block_number = ntohs(block_number);
		buffer_index += 2;
		
		//protocollo txt
		if(!strcmp(transfer_mode, "netascii\0"))
		{			
			strcpy(file, buffer_pack+buffer_index); //copio i dati del file ricevuti
			fprintf(fp, "%s", file);
		} 
		//protocollo bin
		else 
		{
			memcpy(file, buffer_pack+buffer_index, FILE_BUFFER_SIZE);
			fwrite(&file, byte_rcv-4, 1 ,fp); // -4 per eliminare opcode e block_number
		}
	
		//invio ACK	del pacchetto ricevuto
		buffer_index = 0;		
		memset(ack_packet, 0, BUFFER_SIZE);		
		//setto opcode=4 perche ACK
		opcode = htons(4);
		memcpy(ack_packet, (uint16_t*)&opcode, 2);
		buffer_index += 2;
		//setto numero del blocco ricevuto
		block_number = htons(block_number);
		memcpy(ack_packet + buffer_index, (uint16_t*)&block_number, 2);
		buffer_index += 2;
		//invio ACK
		ret = sendto(sd, ack_packet, buffer_index, 0,(struct sockaddr*)&server_addr, sizeof(server_addr));
		transfers++;
			
		//se ho ricevuto un pacchetto piu piccolo di 512 allora la trasmissione è finita
		if(byte_rcv < PACKET_SIZE)
		{
			printf("\nTrasferimento del file completato (%llu/%llu blocchi).\n", transfers, transfers);
			printf("\nSalvataggio %s completato.\n", file_name);
			fclose(fp);
			break;
		}
	}
 
}

void quit(int sd)
{
	close(sd);
	printf("\nDisconnessione effettuata.\n");
	exit(0);
}



