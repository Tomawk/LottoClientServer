 
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

uint16_t lmsg; //lunghezza messaggio da inviare/ricevere
int sd;
char sessionID[10]; //stringa casuale di 10 caratteri che identifica la sessione, ricevuta dal server
int isLogged = 0; //variabile che se =1 significa che l'utente è loggato (serve per riconoscere i comandi dove mandare il sessionID)
int connesso = 0; //variabile per la connessione, se il client è connesso è a 1 altrimenti a 0
//viene passata al while del client e se a 0 provoca l'uscita dal while e la chiusura del socket

void inviaMessaggio(char* buffer){ //funzione di invio messaggio al server
	int ret;
	int len = strlen(buffer) + 1; // Voglio inviare anche il carattere di fine stringa
    lmsg = htons(len); //endianess
	ret = send(sd, (void*) &lmsg, sizeof(uint16_t), 0);
    ret = send(sd, (void*) buffer, len, 0);
    if(ret < 0){ //gestione errori
        perror("**** CLIENT: Errore in fase di invio ****\n");
        exit(-1);
    }
}

void riceviMessaggio(char* buffer){ //funzione per ricevere un messaggio dal server
	int len;
	int ret;
	ret = recv(sd, (void*)&lmsg, sizeof(uint16_t), 0);
 	len = ntohs(lmsg); // Rinconverto in formato host
	ret = recv(sd, (void*)buffer, len, 0);
	if(ret < 0){ //gestione errori
        perror("**** CLIENT: Errore in fase di ricezione ****\n");
        exit(-1);
    }
}

//Funzione che mi verifica se la stringa "a" inizia come specificato nella stringa "b"

int iniziaCon(const char *a, const char *b) 
{
   if(strncmp(a, b, strlen(b)) == 0) return 1; //true
   return 0; //false
}

void messaggioBenvenuto(){ //manda un messaggio di benvenuto a connessione avvenuta
	printf("\n\n***************************** GIOCO DEL LOTTO *****************************\n"
		   "Sono disponibili i seguenti comandi: \n"
		   "1)!help <comando> --> mostra i dettagli di un comando\n"
		   "2)!signup <username> <password> --> crea un nuovo utente\n"
		   "3)!login <username> <password> --> autentica un utente\n"
		   "4)!invia_giocata g --> invia una giocata g al server\n"
		   "5)!vedi_giocate tipo --> visualizza le giocate precedenti dove tipo = {0,1}\n"
		   "                         e permette di visualizzare le giocate passate ‘0’\n"
 		   "                         oppure le giocate attive ‘1’ (ancora non estratte)\n"
 		   "6)!vedi_estrazione <n> <ruota> --> mostra i numeri delle ultime n estrazioni\n"
 		   "                                    sulla ruota specificata\n"
 		   "7)!esci --> termina il client\n\n"
		   );
}

//Funzione di gestione del comando help
//la stringa passata indica il comando di cui voglio sapere ulteriori informazioni
//se la stringa specificata in comando è "0" significa che voglio avere una sintesi
//di tutti i comandi

void comandoHelp(char* comando){

	if(strcmp(comando,"0") == 0) printf("Eccoti una breve sintesi di alcuni comandi . . .\n");
	if(strcmp(comando,"signup\n") == 0) printf("Signup . . .\n");
	if(strcmp(comando,"login\n") == 0)	printf("Login . . .\n");
	if(strcmp(comando,"invia_giocata\n") == 0) printf("Invia_Giocata . . .\n");
	if(strcmp(comando,"vedi_giocate\n") == 0) printf("Vedi_Giocate . . .\n");
	if(strcmp(comando,"vedi_estrazione\n") == 0) printf("Vedi_Estrazione . . .\n");
	if(strcmp(comando,"esci\n") == 0) printf("Esci . . .\n");
}

int verificaCorrettezzaRuota(char* ruota){
	//ritorna -1 se la stringa non è una ruota, altrimenti un valore da 0 a 10 a seconda della ruota specificata
	//Bari,	Cagliari,	Firenze,	Genova,	Milano,	Napoli,	Palermo,	Roma,	Torino,	Venezia	e	Nazionale.
	if(strcmp(ruota,"bari") == 0) return 0;
	if(strcmp(ruota,"cagliari") == 0) return 1;
	if(strcmp(ruota,"firenze") == 0) return 2;
	if(strcmp(ruota,"genova") == 0) return 3;
	if(strcmp(ruota,"milano") == 0) return 4;
	if(strcmp(ruota,"napoli") == 0) return 5;
	if(strcmp(ruota,"palermo") == 0) return 6;
	if(strcmp(ruota,"roma") == 0) return 7;
	if(strcmp(ruota,"torino") == 0) return 8;
	if(strcmp(ruota,"venezia") == 0) return 9;
	if(strcmp(ruota,"nazionale") == 0) return 10;
	return -1;
}

int analisiComando(char* buffer){ 

	/* Funzione che analizza se un comando è stato inserito in forma corretta e ritorna i seguenti valori
		0 se il comando è errato
		1 se il comando è !help
		>1 se il comando è da inviare al server
		2 se il comando è signup
		3 se il comando è login . . . etc */

	//Variabili per spezzettare il buffer in substrings
	int numeroParole = 0;
	char delimiter[2] = " ";
	char* token = strtok(buffer, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
	char parole[20][1024]; //Array di stringhe per le substring ottenute tramite strtok

	int i;
	for(i=0; i<20; i++) strcpy(parole[i],""); //inizializzazione array parole[]

	while (token != NULL) { //while per riempire l'array parole[]
		strcpy(parole[numeroParole],token);
		numeroParole++;
        token = strtok(NULL,delimiter); 
    } 

    //parole[0] conterrà il comando tipo !help !signup
    //parole[1] .. conterranno argomenti a seconda del comando

	if(strcmp(parole[0],"!help\n") == 0){ //è il comando !help senza argomenti
		comandoHelp("0");
		return 1;
	} else if(strcmp(parole[0],"!help") == 0){ //è il comando !help con argomenti
	 	if(strcmp(parole[1],"signup\n") == 0 || strcmp(parole[1],"login\n") == 0 || strcmp(parole[1],"invia_giocata\n") == 0
		    || strcmp(parole[1],"vedi_giocate\n") == 0 || strcmp(parole[1],"vedi_estrazione\n") == 0 || strcmp(parole[1],"esci\n") == 0){
			comandoHelp(parole[1]);
			return 1;
		}
	}

	if(strcmp(parole[0],"!signup") == 0 && strcmp(parole[1],"") != 0 && strcmp(parole[2],"") != 0 && strcmp(parole[3],"") == 0 ) { 
		//comando !signup username password
		return 2;
	}

	if(strcmp(parole[0],"!login") == 0 && strcmp(parole[1],"") != 0 && strcmp(parole[2],"") != 0 && strcmp(parole[3],"") == 0){
		//comando !login username passwordrtr r rrr
		return 3;
	}

	if(strcmp(parole[0],"!invia_giocata") == 0 && strcmp(parole[1],"-r") == 0){ //comando !invia_giocata  

		//Devo controllare che il formato sia esatto ovvero
		//controllare che vi siano i separatori "-n" "-i" nella posizione corretta
		//Controllare che abbia inserito delle ruote e che siano effettivamente corrette
		//Controllare che le ruote non siano duplicate
		//Verificare che i numeri siano nel range 1-90 e che non siano duplicati
		//Verificare che sia specificato almeno un numero ma non più di 10
		//Verificare che abbia inserito almeno un importo e che non sia minore di 0

		//Variabili binarie, se a 0 significa che sto controllando le ruote mentre se a 1 sono inattive
		//se trovo ad esempio -n e quindi ho finito con le ruote, imposto specificoRuota=1 e specificoNumeri = 0
		//sto passando a specificare i numeri e cosi via finchè non sono tutte a 1 (e ho finito)

		int specificoRuota = 0;
		int specificoNumeri = 1;
		int specificoImporto = 1;

		int i = 2; //parole[0] e parole[1] non mi interessano piu

		//variabili per verificare che abbia inserito almeno 1 ruota, 1 numero e un importo
		//e che non abbia specificato troppe ruote, numeri o importi
		int quanteRuote = 0;
		int quantiNumeri = 0;
		int quantiImporti = 0;

		int controlloImporto = 0;

		//array per il controllo sui duplicati (li inserisco prima qua e verifico se li ho già inseriti o meno)
		//se sono già stati settati a 1 significa che ho un duplicato

		int ruote[11] = {0}; //di base tutte a 0, vengono settate a 1 se inserite nel comando
		//Bari,Cagliari,Firenze,Genova,Milano,Napoli,Palermo,Roma,Torino,Venezia e Nazionale.
		int numeri[90] = {0};

		//ESEMPIO DI INVIA_GIOCATa
		//> !invia_giocata –r roma milano –n 15 19 33  –i 0 5 10

		while(strcmp(parole[i],"") != 0){ //Scorro tutto parole[] finchè non ho più substrings

			if(specificoRuota == 0){
				if(strcmp(parole[i],"-n") != 0) { //la ruota non è -n (quindi è una ruota vera e propria)

					int numeroRuota; //indice per riempire l'array ruote
					quanteRuote++; 

					//non posso specificare altre ruote se le specifico tutte
					if(strcmp(parole[i],"tutte") == 0) goto finewhile;

					numeroRuota = verificaCorrettezzaRuota(parole[i]); //verifica che la ruota sia tra quelle disponibili

					if(numeroRuota == -1) return 0; //ruote non corrette

					if(ruote[numeroRuota] == 0) ruote[numeroRuota] = 1;
						else return 0; //ruota duplicata

					if(quanteRuote > 11) return 0; //se ho più di 11 ruote specificate
				} 
				else { // se la ruota è -n ho finito di verificare le ruote
					specificoRuota = 1;
					specificoNumeri = 0;
					if(quanteRuote == 0) return 0; //se non ho specificato alcuna ruota
					goto finewhile;
				}
			}
			if(specificoNumeri == 0){ //sto specificando i numeri (sono appena dopo il "-n")

				if(strcmp(parole[i],"-i") != 0){ //se la stringa non è "-i" e quindi è un numero

					int numero = atoi(parole[i]); //cast da stringa a int

					if(numero < 1 || numero > 90) return 0; //numero fuori dal range

					if(numeri[numero-1] == 0) numeri[numero-1] = 1; 
						else return 0;	//numero duplicato

					quantiNumeri++;

					if(quantiNumeri > 10) return 0; //troppi numeri specificati
				}
				else { //è "-i"
					specificoNumeri = 1; //ho finito di specificare i numeri
					specificoImporto = 0; //inizio a specificare gli importi
					if(quantiNumeri == 0) return 0; //non ho specificato numeri
					goto finewhile;
				}
			}

			if(specificoImporto == 0){ //sono appena dopo il -i (specifico gli importi)

				float importo = atof(parole[i]); //cast da stringa a float

				if(importo > 0) controlloImporto = 1; //controlla che non siano tutti impostati a 0
				//Questo controllo viene fatto perchè l'utente potrebbe specificare ad esempio -i 0 0 0
				//quantiImporti verrebbe incrementato ma non avrei un valore >0 su cui effettuare effettivamente la puntata

				quantiImporti++;

				if(strcmp(parole[i+1],"") == 0) specificoImporto = 1; //sono all'ultimo importo

				if(importo < 0) return 0; //importo negativo

				if(quantiImporti > 5) return 0; //se ho più di 5 importi (il massimo è la cinquina)
			}

			finewhile:
			i++;

		}
		if(controlloImporto == 0) return 0; //Ho specificato importi ma tutti a 0
		if(quantiImporti == 0) return 0; //Non ho specificato importi
		return 4;
	}

	if(strcmp(parole[0],"!vedi_giocate") == 0 && strcmp(parole[1],"") != 0){
		//comando !vedi_giocate <tipo>
		if(strcmp(parole[1],"1\n") == 0 || strcmp(parole[1],"0\n") == 0) { //tipo può essere solo o 0 o 1 
			return 5;
		} else return 0;
	}

	if(strcmp(parole[0],"!vedi_estrazione") == 0 && strcmp(parole[1],"") != 0 && strcmp(parole[3],"") == 0){

		int quante; //numero di estrazioni richieste
		int indiceRuota; //ritorno per la funzione verificaCorrettezzaRuota()

		if(strcmp(parole[2],"") == 0) strtok(parole[1],"\n"); // rimuovo il \n
		quante = atoi(parole[1]);

		if(quante < 1 || quante > 10) return 0; //Se specifico un numero non corretto (10 è il limite al numero di estrazioni richieste)

		if(strcmp(parole[2],"") != 0){ //se ho specificato una ruota su cui vedere l'estrazione deve essere controllata
			strtok(parole[2],"\n");
			indiceRuota = verificaCorrettezzaRuota(parole[2]);
			if(indiceRuota == -1) return 0;
		}
		return 6;
	}

	if(strcmp(parole[0],"!vedi_vincite\n") == 0){
		return 7;
	}

	if(strcmp(parole[0],"!esci\n") == 0) {
		return 8;
	}

	return 0;
}

int main(int argc, char* argv[]){

	//devo attivare il client come segue
	// ./lotto_client <IP server> <porta server>
	//argc deve valere 3 con argv[1]=<IP server>  e argv[2]=<porta server>

	int ret;
	int portaServer; //argomento passato al main
    struct sockaddr_in srv_addr;
    char buffer[4096]; //buffer con dati da input da inviare al server
    char tempbuff[4096]; //buffer temporaneo per il controllo del formato dei comandi
    char* IPServer; //argomento passato al main
    int comandoRiconosciuto = 0; //valore di ritorno per analisiComando()    

    if(argc != 3) {
    	printf("**** ERRORE: per poter avviare il client devi specificare i seguenti parametri <IP server> e <porta server> ****\n");
    	exit(-1);
    }

    IPServer = argv[1]; //ip del server passato come argomento
    portaServer = atoi(argv[2]); //passaggio argomenti del main, richiesto cast ad int

    /* Creazione socket */
    sd = socket(AF_INET, SOCK_STREAM, 0);

    /* Creazione indirizzo del server */
    memset(&srv_addr, 0, sizeof(srv_addr)); // Pulizia 
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(portaServer);
    inet_pton(AF_INET, IPServer , &srv_addr.sin_addr);

    ret = connect(sd, (struct sockaddr*)&srv_addr, sizeof(srv_addr)); //connessione

    if(ret < 0){
        perror("**** CLIENT: Errore in fase di connessione ****\n");
        exit(-1);
    } else connesso=1;

    riceviMessaggio(buffer); //ricevo un messaggio che mi comunica se la connessione è andata a buon fine
    printf("%s",buffer);
    if(iniziaCon(buffer,"ErrorCode-0x001:") == 1) connesso = 0; //se la connessione non è andata chiudo il socket

    if(connesso == 1) messaggioBenvenuto(); //Manda il messaggio di benvenuto al client

    while(connesso == 1){

 
        fgets(buffer, 1024, stdin); //attendo input da tastiera

        strcpy(tempbuff,buffer); //ottengo una copia del buffer che sarà soggetta a spezzettamento in analisiComando()

        comandoRiconosciuto = analisiComando(tempbuff); //Controllo il formato del comando
        												//Restituisce 0 se in formato errato
        												//Restituisce 1 se è il comando help (non richiede invio al server)
        												//Se >1 il comando va inviato al server in particolare (2 è il cmd signup etc..)

        if(comandoRiconosciuto == 0) {
			printf("**** CLIENT: Siamo spiacenti, il comando non è stato riconosciuto. Controlla che la sintassi sia esatta e riprova. ****\n");
		} 

		if(comandoRiconosciuto > 1 ) { //è un comando da inviare al server

			strtok(buffer,"\n"); //rimuovo il \n

			if(isLogged == 1){ //se è loggato devo inviare dopo ciascun comando il sessionID (non influenza i comandi help/signup e login)
				strcat(buffer," ");
				strcat(buffer,sessionID);
			}

			inviaMessaggio(buffer); //invio il comando con il formato corretto al server

      		riceviMessaggio(buffer); //attendo una risposta dal server

        	if(comandoRiconosciuto != 6) printf("%s",buffer); //stampo la risposta del server
        		else{

        			//comando !vedi_estrazione (devo stampare nell'ordine corretto)
        			int i;
        			int j = 0;
        			int count = strlen(buffer);
        			for(i= count - 1; i >= 0 && count > 0; i--){
        				printf("%c",buffer[i]);
        				/*if(i%35 == 0) { 
        					if(j==10) { printf("\n"); j=-1; } 
        					printf("\n");
        					j++;
        				}*/
        			}
        		}

        	if(comandoRiconosciuto == 2){ //se è il comando signup devo controllare la risposta in caso di username duplicato
        		while(iniziaCon(buffer,"ErrorCode-0x800") == 1){ //in caso di username duplicato il server mi manderà questo errore
        			fgets(buffer, 1024, stdin); //inserisco un nuovo username
        			inviaMessaggio(buffer); //lo invio al server
        			riceviMessaggio(buffer); 
        			//il server mi risponderà o nuovamente con il messaggio di errore  (e quindi continuerò il loop sul while)
        			//oppure con un messaggio di registrazione avvenuta
        			printf("%s",buffer); //stampo la risposta
        		}
        	}

        	if(comandoRiconosciuto == 3){ //Se il comando è login
        		if(strcmp(buffer,"**** SERVER: Login effettuato correttamente ****\n") == 0){ //login andato a buon fine
        			riceviMessaggio(buffer); //mi preparo a ricevere il sessionID
        			strcpy(sessionID,buffer);
        			isLogged = 1; //l'utente è stato loggato con successo
        			printf("**** CLIENT: SessionID ricevuto con successo: %s ****\n", sessionID);
        		}
        		//in caso di 3 tentativi di login falliti il server mi manderà questo messaggio e devo chiudere la connessione
     			if(strcmp(buffer,"**** SERVER: Disconnessione . . . ****\n") == 0){ 
     				connesso = 0;
     			}
        	}

        	if(comandoRiconosciuto == 8){ //comando !esci
        		if(strcmp(buffer,"**** SERVER: Disconnessione avvenuta con successo  ****\n") == 0) connesso = 0;
        	}

		}

    }
    printf("**** CLIENT: Sei stato disconnesso ****\n");
    close(sd);
    return 0;
        
}



