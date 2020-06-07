#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

/* VARIABILI GLOBALI */
 
struct Schedina {

	char utente[1024]; //significativa solo nella verifica di una vincita, contiene l'utente che ha fatto la giocata
	int ruote[11];

	//a partire da 0 l'ordine è il seguente (Bari,Cagliari,Firenze,Genova,Milano,Napoli,Palermo,Roma,Torino,Venezia,Nazionale)
	//quando un giocatore seleziona una ruota essa viene settata a 1 altrimenti a 0

	int numeri[90]; //quando punto su un numero la casella numero-1 viene settata, il resto rimane a 0
	float importi[5]; //[0] è la puntata effettuata sull'estratto, [1] sull'ambo e così via
};

struct Schedina s; //Schedina per l'invio della giocata
struct Schedina schedinaInAttesa; //Schedina in attesa per il controllo vincite
int sd; //descrittore socket di ascolto
int periodo; //argomento passato tramite main, indica il tempo con cui verranno svolte le estrazioni
int new_sd; //descrittore socket client
struct sockaddr_in my_addr, cl_addr; //indirizzo server, indirizzo client
uint16_t lmsg; //per ricevere la lunghezza del buffer
int tentativiLogin = 3; //variabile per i tentativi di login
char sessionID[10] = "0x00000000"; //stringa che memorizzerà la sessionID
char loggedUsr[1024]; //variabile in cui viene salvato l'username dell'utente attualmente loggato
int numeriGiocati; //viene resettata e incrementata ad ogni creazione di una schedina in attesa, conterrà il n° di numeri giocati
int ruoteSelezionate; //come numeriGiocati ma conta il numero di ruote selezionate

int connesso; //variabile passata come condizione al while del figlio 
//finchè è a 1 mantiene attiva la connessione, se va a 0 provoca l'uscita dal while del figlio e la chiusura del socket

int iniziaCon(const char *a, const char *b) //funzione che mi verifica se una determinata stringa inizia in un certo modo
{
   if(strncmp(a, b, strlen(b)) == 0) return 1; //true
   return 0; //false
}

void inviaMessaggio(char* buffer){ //funzione di invio al client del messaggio contenuto in buffer
	int ret;
	int len = strlen(buffer) + 1; // Voglio inviare anche il carattere di fine stringa
    lmsg = htons(len); //endianess
	ret = send(new_sd, (void*) &lmsg, sizeof(uint16_t), 0);
    ret = send(new_sd, (void*) buffer, len, 0);
    if(ret < 0){ //gestione errori
        perror("**** SERVER: Errore in fase di invio ****\n");
        exit(-1);
    }
}

void riceviMessaggio(char* buffer){ //funzione di ricezione dal client di un messaggio che verrà salvato in buffer
	int len;
	int ret;
	ret = recv(new_sd, (void*)&lmsg, sizeof(uint16_t), 0); //ricevo prima la lunghezza del messaggio
 	len = ntohs(lmsg); // Rinconverto in formato host
	ret = recv(new_sd, (void*)buffer, len, 0); //poi ricevo il messaggio vero e proprio
	if(ret < 0){ //gestione errori
        perror("**** SERVER: Errore in fase di ricezione ****\n");
        exit(-1);
    }
    if(ret == 0){
    	printf("**** SERVER: Chiusura client ****\n");
    	exit(-1);
    }
}

//Quando l'utente viene registrato, viene creata una sua scheda utente nel path ./schedeUtenti/<username>.txt

void inserisciSchedaUtente(char* username, char* password){ 
	int usrlen = strlen(username);
	char* folderpath = "./schedeUtenti/";
	int pathlen = strlen(folderpath);
	char filepath[usrlen + pathlen + 5]; // folderpath + username + .txt + \0
	sprintf(filepath,"%s%s.txt",folderpath,username);
	FILE* fd;
	if ((fd=fopen(filepath, "w"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n");
	// contenuto che verrà inserito in tale file
	fprintf(fd,"**** SCHEDA DELL'UTENTE: %s ****\n\n",username);
	fprintf(fd,"username: %s\n",username);
	fprintf(fd,"password: %s\n\n",password);
	fprintf(fd,"ELENCO DI TUTTE LE GIOCATE:\n\n");
	fclose(fd);
}

//Quando l'utente viene registrato, viene inserita la seguente riga <username>/<password> in users.txt
//Tale file verrà utilizzato per il controllo delle credenziali al login
//Questa funzione viene invocata dalla comandoSignup() non appena l'utente specifica un username non presente e una password

void scriviUsernamePsw(char* username, char* password){
	int usrlen = strlen(username);
	int pswlen = strlen(password); 
	char strConcatenata[usrlen + pswlen + 3]; //stringa formattata così: username/password + \n + \0
	sprintf(strConcatenata, "%s/%s", username, password);
	FILE* fd;
	if ((fd=fopen("users.txt", "a"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n");
	else printf("**** SERVER: Registrazione Utente avvenuta con successo! *****\n");
	fprintf(fd, "%s\n", strConcatenata); //inserisce la stringa nel formato impostato nel file
	fclose(fd);
	inserisciSchedaUtente(username,password); //crea la scheda relativa a tale utente in /schedeUtenti/
}

//Funzione per il controllo sugli username, al momento della registrazione viene controllato se l'username inserito è presente
//Nel caso sia presente, devo richiedere al client di specificarne uno nuovo finchè non sarà disponibile

int controllaUsername(char* username){ 

	//Le seguenti variabili sono utilizzate per effettuare la lettura delle righe di un file
	char *line_buf = NULL;
  	size_t line_buf_size = 0;
  	ssize_t line_size;

  	int usrlen = strlen(username);
  	char tmpstr[usrlen + 2]; //stringa temporanea conterrà username + "/" e \0
  	sprintf(tmpstr,"%s/",username);

  	FILE* fd;

  	//apro il file users.txt in lettura, mi serve solo leggerci per vedere se l'username è già presente

  	if ((fd=fopen("users.txt", "r"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n"); 

  	line_size = getline(&line_buf, &line_buf_size, fd); //prende la prima linea del file fd

  	while (line_size >= 0) { // Loop in cui analizza tutte le linee del file users.txt

    	if(iniziaCon(line_buf,tmpstr) == 1) { //controllo se la riga appena letta dal file inizia con username/
    		//nel caso lo sia, l'username è già presente e devo chiederne uno nuovo al client, ritorno 0
    		printf("**** SERVER: Username già presente, notifico il client! ****\n");
    		return 0;
    	}
    
   
    	line_size = getline(&line_buf, &line_buf_size, fd); //prendo la prossima linea e continuo

  	}
  	return 1; //se non è presente ritorno 1
}

//Funzione invocata dalla comandoLogin() per verificare che le credenziali inserite corrispondano 
//effettivamente ad un utente registrato. 
//Il controllo viene effettuato sul file users.txt 

int controllaCredenziali(char* username, char* password){ //Restituisce 0 in caso le credenziali siano corrette, 1 altrimenti

	//Variabili per la lettura delle righe di un file
	char *line_buf = NULL;
  	size_t line_buf_size = 0;
  	ssize_t line_size;

	//Formatto la stringa come è in users.txt (<username>/<password>)
	int usrlen = strlen(username);
	int pswlen = strlen(password); 
	char strConcatenata[usrlen + pswlen + 3];
	sprintf(strConcatenata, "%s/%s\n", username, password); //formato username/password

	FILE *fd = fopen("users.txt", "r"); //apro users.txt in lettura

  	if (!fd) {
    	printf("**** SERVER: Errore nell’apertura del file! ****\n");
  	}

  	line_size = getline(&line_buf, &line_buf_size, fd); //prende la prima linea del file fd

  	while (line_size >= 0) { // Loop in cui analizza tutte le linee del file users.txt

    	if(strcmp(line_buf,strConcatenata) == 0) { //se la riga letta dal file è uguale alla mia stringa <user>/<password>
    		printf("**** SERVER: L'username e password inseriti dal client sono corretti ****\n");
    		return 0; //controllo andato a buon fine
    	}
    
   
    	line_size = getline(&line_buf, &line_buf_size, fd); //prendo la prossima linea e continuo

  	}
  	return 1; //match non avvenuto, utente con tale password non presente
}

//Funzione che genera una stringa di lunghezza len con caratteri alfanumerici casuali
//Viene utilizzata da comandoLogin per generare il sessionID da inviare al client

void randomString(const int len) {
	int i;
    const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    srand(time(NULL));
    for (i = 0; i < len; ++i) {
        sessionID[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    sessionID[len] = 0; //aggiungo \0
}

//Funzione per gestire il comando signup

void comandoSignup(char* buffer){ 

	//Variabili per spezzettare il buffer in substrings dato un delimitatore (in questo caso lo spazio " ")
	int numeroParole = 0;
	char delimiter[2] = " ";
	char* token = strtok(buffer, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
	char parole[3][1024]; //parole[0] conterrà !signup (non ci interessa)
						  //parole[1] conterrà <username>
						  //parole[2] conterrà <password>

	int presente = 0; //variabile di ritorno della controllaUsername()
	// che mi dice se l'username è già presente o meno (0 presente/1 assente)

	char* msgDuplicato = "ErrorCode-0x800: L'username inserito è già presente, inseriscine un altro\n";
	//messaggio di errore inviato dal server al client in caso di username duplicato

	char newusr[1024]; //stringa in cui verrà memorizzato l'username reinserito dall'utente nel caso fosse già presente


	while (token != NULL) { //con questo while vado a riempire l'array parole[] con le substring ottenute
		strcpy(parole[numeroParole],token);
		numeroParole++;
        token = strtok(NULL,delimiter); 
    }

    presente = controllaUsername(parole[1]); //controlla se l'username è presente e ritorna 1 se non lo è oppure 0
    
    if(presente == 1) {  //l'username non è presente
    	scriviUsernamePsw(parole[1],parole[2]); //lo scrivo nel file users.txt
    	inviaMessaggio("**** SERVER: Il suo username è stato registrato con successo ****\n"); //notifico il client
    }
    else {
    	while(presente == 0) { //finchè non ne trovo uno non presente
    		inviaMessaggio(msgDuplicato); //invio messaggio di errore
    		riceviMessaggio(newusr); //mi preparo a ricevere il nuovo username
    		strtok(newusr,"\n"); //rimuovo \n
    		presente = controllaUsername(newusr); //lo controllo, e se è sempre presente riparto con il while altrimenti esco
    	}
    	scriviUsernamePsw(newusr,parole[2]); //lo scrivo nel file con la password inserita in precedenza
    	inviaMessaggio("**** SERVER: Il suo username è stato registrato con successo ****\n"); //notifico il client
    
    }

}

//Funzione mandata in esecuzione non appena viene ricevuto il comando di login

void comandoLogin(char* buffer){ 

	//variabili per spezzettare il buffer in substring, lo spezzettamento avviene tramite il delimitatore " "
	int numeroParole = 0;
	char delimiter[2] = " ";
	char* token = strtok(buffer, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "

	char parole[3][1024]; //array che salverà le substring ottenute dal buffer
						  //parole[0] conterrà !login (non mi interessa)
						  //parole[1] conterrà <username> 
						  //parole[2] conterrà <password>

	int autenticazione; //variabile di ritorno per la controllaCredenziali (0 in caso di credenziali corrette, 1 altrimenti)
	char msgErrore[1024]; //stringa per inviare un messaggio di errore al client in caso di login non riuscito

	while (token != NULL) { //while per riempire l'array di substring parole[]
		strcpy(parole[numeroParole],token);
		numeroParole++;
        token = strtok(NULL,delimiter); 
    }

    autenticazione = controllaCredenziali(parole[1],parole[2]); //controllo sulle credenziali del login

    if(autenticazione == 0){
    	//il login è andato a buon fine

    	tentativiLogin = 3; //reimposto i tentativi di login al valore iniziale
    	randomString(10); //funzione che assegna una stringa casuale di 10 caratteri alfanumerici alla var globale sessionID
    	strcpy(loggedUsr,parole[1]); //inserisco l'username dell'utente appena loggato nella var globale loggedUsr
    	printf("**** SERVER: Tentativo di login andato a buon fine ****\n");
    	//Invio un messaggio di successo al client seguito dal sessionID appena generato
    	//L'utente una volta loggato dovrà inviare i comandi seguiti dal sessionID
    	//Tale sessionID sarà poi ogni volta controllato dal server per verificare che sia corretto
    	inviaMessaggio("**** SERVER: Login effettuato correttamente ****\n");
    	inviaMessaggio(sessionID);
    	printf("**** SERVER: SessionID inviato con successo ****\n");
    } else{
    	//login non riuscito

    	sprintf(msgErrore, "ErrorCode-0x900: Le credenziali inserite non sono corrette, riprova. (tentativi rimasti: %i ) \n", tentativiLogin-1);
    	printf("**** SERVER: Tentativo di login fallito ****\n");
    	if(tentativiLogin > 1)inviaMessaggio(msgErrore); //invio un messaggio di errore al client
    		else {
    			inviaMessaggio("**** SERVER: Disconnessione . . . ****\n");
    			printf("**** SERVER: Il client ha mandato troppi tentativi di login fallimentari, lo disconnetto ****\n");
    		}
    	tentativiLogin = tentativiLogin - 1; //decremento i tentativi di login (ne ho altri 2)
    	if(tentativiLogin == 0){ 

    		//Se non ho più tentativi di login devo inserire l'ip del client e data e orario dell'ultima fallimento
    		//in un file chiamato login_failed.txt e bloccare l'ip per 30 minuti 

    		char strConcatenata[100]; //stringa per la stampa su file

    		//variabili per il timestamp
  			char timestamp[50]; //stringa dove verrà salvato il timestamp nel formato "dd-mm-yyyy hh-mm-ss"
    		time_t t = time(NULL);
  			struct tm tm = *localtime(&t); //struct per il timestamp

			sprintf(timestamp,"%02d-%02d-%d %02d:%02d:%02d ",tm.tm_mday, tm.tm_mon + 1,tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);

    		//inet_ntoa mi restituisce l'indirizzo ip come stringa da cl_addr

    		sprintf(strConcatenata, "%d/%s/%s", (int)time(NULL), inet_ntoa(cl_addr.sin_addr), timestamp); //formato IPClient/date time
    		//time() restituisce il timestamp in millisecondi dal 1 gennaio 1970, mi fa comodo per i confronti tra timestamp
    		FILE *fd = fopen("login_failed.txt", "a");
    		fprintf(fd, "%s\n", strConcatenata); //inserisco l'ip del client e il timestamp nel file login_failed.txt
    		fclose(fd);
    		connesso = 0;
    	}
    }

}


int individuaRuota(char* ruota){
	//ritorna -1 se la stringa è "tutte", altrimenti un valore da 0 a 10 a seconda della ruota specificata
	//Bari,	Cagliari,	Firenze,	Genova,	Milano,	Napoli,	Palermo,	Roma,	Torino,	Venezia	e	Nazionale.
	if(strcmp(ruota,"bari") == 0 || strcmp(ruota,"Bari") == 0) return 0;
	if(strcmp(ruota,"cagliari") == 0 || strcmp(ruota,"Cagliari") == 0) return 1;
	if(strcmp(ruota,"firenze") == 0 || strcmp(ruota,"Firenze") == 0 ) return 2;
	if(strcmp(ruota,"genova") == 0 || strcmp(ruota,"Genova") == 0 ) return 3;
	if(strcmp(ruota,"milano") == 0 || strcmp(ruota,"Milano") == 0 ) return 4;
	if(strcmp(ruota,"napoli") == 0 || strcmp(ruota,"Napoli") == 0 ) return 5;
	if(strcmp(ruota,"palermo") == 0 || strcmp(ruota,"Palermo") == 0 ) return 6;
	if(strcmp(ruota,"roma") == 0 || strcmp(ruota,"Roma") == 0 ) return 7;
	if(strcmp(ruota,"torino") == 0 || strcmp(ruota,"Torino") == 0 ) return 8;
	if(strcmp(ruota,"venezia") == 0 || strcmp(ruota,"Venezia") == 0 ) return 9;
	if(strcmp(ruota,"nazionale") == 0 || strcmp(ruota,"Nazionale") == 0 ) return 10;
	return -1; //nel caso sia "tutte"

}

//Funzione che inizializza tutti i campi della struct Schedina a 0
//Modalita serve per decidere quale schedina inizializzare, se quella in attesa (per il controllo vincite)
//oppure la schedina "s" che viene utilizzata nell'invio di una nuova giocata

void inizializzaSchedina(int modalita){ 
	int i;
	for(i = 0; i < 11; i++){
		if(modalita == 0) s.ruote[i] = 0;
		if(modalita == 1) schedinaInAttesa.ruote[i] = 0;
	}
	for(i = 0; i < 90; i++){
		if(modalita == 0) s.numeri[i] = 0;
		if(modalita == 1) schedinaInAttesa.numeri[i] = 0;
	}
	for(i = 0; i < 5 ; i++){
		if(modalita == 0) s.importi[i] = 0;
		if(modalita == 1) schedinaInAttesa.importi[i] = 0;
	}
}


//Funzione invocata da comandoInvia_Giocata()
//Dato il buffer ricevuto dal client, va a impostare correttamente tutti i campi della var globale Schedina s
//Tale schedina viene resettata ogni volta e riempita appena viene chiamato il comando !invia_giocata

int impostoSchedina(char* buffer){

	//variabili per ottenere substrings dal buffer
	int numeroParole = 0;
	char delimiter[2] = " ";
	char* token = strtok(buffer, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
	char parole[50][1024]; //array di substrings
	//il formato del buffer è !invia_giocata -r ruota1 ruota2 . . -n n1 n2 . . -i i1 i2 . . 
	//parole[0] conterrà !invia_giocata 
	//parole[1] conterrà -r
	//parole[2] conterrà la prima ruota etc..

	int j;
	for(j = 0; j<50; j++) strcpy(parole[j],"");

	int i = 2; //per il ciclo while (parto da 2, parole[0] e parole[1] non mi interessano)

	int impostoRuote = 0; //se sto impostando le ruote è a 0, altrimenti va a 1
	int impostoNumeri = 1; 
	int impostoImporto = 1;
	int indiceRuota; //valore di ritorno per la funzione individuaRuota();
	int indiceImporto = 0; //indice per settare il corrispettivo elemento dell'array degli importi nella schedina

	while (token != NULL) { //while per riempire l'array di stringhe
		strcpy(parole[numeroParole],token);
		numeroParole++;
        token = strtok(NULL,delimiter); 
    }

    inizializzaSchedina(0);

    while(strcmp(parole[i],"") != 0){
    	if(impostoRuote == 0){ //sto impostando le ruote
    		if(strcmp(parole[i],"-n") == 0){
    			impostoRuote = 1; //ho finito di impostare le ruote
    			impostoNumeri = 0; //inizio ad impostare i numeri
    			goto finewhile; //vado alla fine del while dato che ho "-n"
    		}
    		indiceRuota = individuaRuota(parole[i]);
    		if(indiceRuota == -1){
    			int k;
    			for(k = 0; k<11; k++) s.ruote[k] = 1; //le imposto tutte a 1 (sono tutte selezionate)
    		} else {
    			s.ruote[indiceRuota] = 1; //altrimenti imposto la ruota in questione
    		}
    	}
    	if(impostoNumeri == 0){ // sto impostando i numeri
    		int numero;
    		if(strcmp(parole[i],"-i") == 0){
    			impostoNumeri = 1; //ho finito di impostare i numeri
    			impostoImporto = 0; //inizio ad impostare gli importi
    			goto finewhile; //vado alla fine del while dato che ho "-n"
    		}
    		numero = atoi(parole[i]);
    		s.numeri[numero-1] = 1;
    	}
    	if(impostoImporto == 0){
    		float importo;
    		if(strcmp(parole[i+1],"") == 0) { //parole[i] contiene il sessionID
    			if(strcmp(parole[i],sessionID) != 0){ //controllo che il sessionID sia giusto
    				return 0; //exit status in caso di errore
    			}
    			impostoImporto = 1;
    			goto finewhile;
    		}
    		importo = atof(parole[i]);
    		s.importi[indiceImporto] = importo;
    		indiceImporto++; //vado sulla prossima casella
    	}
    	finewhile:
    	i++;
    }
    return 1;
}

//Funziona chiamata dalla comandoInvia_Giocata()
//Va ad inserire la giocata appena effettuata sia nella scheda dell'utente, sia nel file giocate_in_attesa.txt
//in quanto tale giocata ovviamente è in attesa di estrazione

void inserisciGiocataFile(){

	//Per inserire la giocata utilizzo la struct globale Schedina s
	//Tale schedina è stata precedentemente riempita dalla funzione impostoSchedina()
	//Funzione anche essa chiamata dalla comandoInvia_Giocata();

	int i;

	char strScheda[1024] = " "; //Stringa che verrà stampata nella scheda dell'utente che ha fatto la giocata
	//Il formato è quello della !vedi_giocate ovvero:
	//r1 r2 . . n1 n2 . . * i1 Estratto * i2 Ambo * . . 
	//Se gli importi sono a 0 non vengono specificati

    char strAttesa[1024]; //Stringa che verrà stampata in giocate_in_attesa
    //Il formato di tale stringa sarà <username>: r1 r2 . . -n n1 n2 . . -i i1 i2 i3 i4 i5
    //Specifico tutti gli importi anche se a 0

	char path[1024]; //stringa che conterrà il path della scheda dell'utente

	sprintf(strAttesa,"%s:",loggedUsr); //la stringa della giocata in attesa inizia con <username>:

	for(i=0; i<11; i++){ //scorro tutto s.ruote[]
		if(s.ruote[i] == 1){ // a seconda di quali ruote sono settate le concateno a strScheda
			if(i == 0)  strcat(strScheda,"Bari ");
			if(i == 1)  strcat(strScheda,"Cagliari ");
			if(i == 2)  strcat(strScheda,"Firenze ");
			if(i == 3)  strcat(strScheda,"Genova ");
			if(i == 4)  strcat(strScheda,"Milano ");
			if(i == 5)  strcat(strScheda,"Napoli ");
			if(i == 6)  strcat(strScheda,"Palermo ");
			if(i == 7)  strcat(strScheda,"Roma ");
			if(i == 8)  strcat(strScheda,"Torino ");
			if(i == 9)  strcat(strScheda,"Venezia ");
			if(i == 10) strcat(strScheda,"Nazionale ");
			}
	}

	strcat(strAttesa,strScheda); //Ora strAttesa contiene l'insieme di ruote
	strcat(strAttesa,"-n "); //aggiungo un separatore -n per i numeri (ho inserito tutte le ruote)

	for(i=0; i<90; i++){ //scorro tutto s.numeri[]

		char intStr[4]; //per la conversione da int a stringa
		sprintf(intStr,"%i ",i+1);

		if(s.numeri[i] == 1) { 
			strcat(strScheda,intStr); //concateno la stringa con il numero
			strcat(strAttesa,intStr); //concateno la stringa con il numero
		}
	}

	strcat(strAttesa,"-i "); //aggiungo un separatore -i per gli importi (ho finito di specificare i numeri)

	for(i=0; i<5; i++){

		char strPuntate[1024]; //conterrà  "* <importo> Estratto(etc) "
		char puntata[9]; 
		char importo[1024]; //stringa per l'importo singolo da concatenare a strAttesa

		if(s.importi[i] != 0) { //in strScheda voglio concatenare solo gli importi diversi da 0
			if(i == 0) strcpy(puntata,"Estratto");
			if(i == 1) strcpy(puntata,"Ambo");
			if(i == 2) strcpy(puntata,"Terno");
			if(i == 3) strcpy(puntata,"Quaterna");
			if(i == 4) strcpy(puntata,"Cinquina");
			sprintf(strPuntate,"* %f %s ",s.importi[i],puntata);
			sprintf(importo,"%f ",s.importi[i]);
			strcat(strScheda,strPuntate);
			strcat(strAttesa,importo); //per strAttesa mi interessano solo gli importi in ordine
		} else { //anche se è a 0 voglio inserirli nel file delle giocate in attesa
			sprintf(importo,"%f ",s.importi[i]);
			strcat(strAttesa,importo); //concateno anche gli importi a 0 alla strAttesa
		}
	}

	sprintf(path,"./schedeUtenti/%s.txt",loggedUsr); //inserisco il path per scrivere nella scheda dell'utente loggato

	//inserisco la giocata nella scheda relativa all'utente
	FILE *fd = fopen(path, "a");

	if (!fd) {
    	printf("**** SERVER: Errore nell’apertura del file! ****\n");
    	return;
  	}
    fprintf(fd, "%s\n", strScheda);
    fclose(fd);
    printf("**** SERVER: Inserita la giocata nel file relativo all'utente ****\n");

    //inserisco la giocata tra le giocate in attesa di estrazione

    FILE *filedesc = fopen("giocate_in_attesa.txt", "a");

	if (!filedesc) {
    	printf("**** SERVER: Errore nell’apertura del file! ****\n");
    	return;
  	}
    fprintf(filedesc, "%s\n", strAttesa);
    fclose(filedesc);
    printf("**** SERVER: La giocata è in attesa di estrazione ****\n");

}

//Funzione di gestione per il comando !invia_giocata

void comandoInvia_Giocata(char* buffer){
	int ret = impostoSchedina(buffer);
	if(ret == 0) inviaMessaggio("**** SERVER: Devi essere loggato prima di poter giocare una schedina ****\n");
		else {
			inviaMessaggio("**** SERVER: Giocata registrata correttamente ****\n");
			inserisciGiocataFile();
		}

}

//Funzione che mi legge le giocate relative ad un utente o da giocate_in_attesa.txt o da giocate_estratte.txt
//Se modalita == 0 allora leggerà le giocate già estratte, altrimenti se modalita == 1 le giocate attive

void leggiGiocate(int modalita){

	/* ESEMPIO DI OUTPUT

	> !vedi_giocate 1 	
		1) Roma 15 19 20 * 10.00 terno * 5.00 ambo
		2) Milano Napoli Palermo 90 * 15.00 estratto
	
	*/

	//Variabili per la lettura delle righe di un file
	char *line_buf = NULL;
  	size_t line_buf_size = 0;
  	ssize_t line_size;
  	FILE* fd;

  	char outputstr[4096] = ""; //stringa contenente tutte le giocate attive da mandare come ritorno

	int numeroRiga = 1; // mi serve per la formattare la stampa in modo che ogni riga stampata abbia 1) 2) etc

	//in base al valore di modalita cambia il file da cui leggere
  	if(modalita == 1) fd = fopen("giocate_in_attesa.txt", "r");
  	if(modalita == 0) fd = fopen("giocate_estratte.txt", "r");

  	if (!fd) {
    	printf("**** SERVER: Errore nell’apertura del file! ****\n");
    	return;
  	}

  	line_size = getline(&line_buf, &line_buf_size, fd); //prende la prima linea del file fd

  	while (line_size >= 0) { // Loop in cui analizza tutte le linee del file fd

  		//per ogni riga letta da file devo ottenere delle substring da inserire nell'array parole[]
  		int numeroParole = 0;
		char delimiter[2] = " ";
		char* token = strtok(line_buf, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
		char parole[50][1024]; //array dove verranno salvate le substrings ottenute delle righe del file
							   //parole[0] conterrà <utente>:
							   //parole[1] conterrà la prima ruota etc. . 

  		char stringaUtente[1024]; //stringa dove viene inserito l'utente attualmente loggato che sta richiedendo la !vedi_giocate
  		sprintf(stringaUtente,"%s:",loggedUsr);

  		while (token != NULL) { //while per riempire l'array di parole[] con le substring
			strcpy(parole[numeroParole],token);
			numeroParole++;
        	token = strtok(NULL,delimiter); 
    	}

		if(strcmp(stringaUtente,parole[0]) == 0) { //se è una giocata dell'utente loggato
			int i = 1; //variabile per il while

			//le seguenti variabili sono binarie e quando valgono 0 significa che sto specificando i numeri o le ruote
			//quando da 0 passano a 1 significa che ho finito con le ruote ad esempio e sto passando ai numeri
			//dovrò perciò impostare numeriSpecificati a 0 e così via

			int ruoteSpecificate = 0;
			int numeriSpecificati = 1;
			int importiSpecificati = 1;

			int indiceImporto = 0; //mi serve per capire se l'importo è riferito all'estratto/ambo/terno etc

			char inizioriga[5]; //stringa che conterrà "<numeroriga>) "
			sprintf(inizioriga,"%i) ",numeroRiga);
			strcat(outputstr,inizioriga);

			//il formato delle righe di entrambi i file è così
			// <username>: ruota1 ruota2 . . . -n n1 n2 n3 . . . -i importo1 importo2 importo3 importo4 importo5
			// tutti gli importi sono presenti, anche se a 0.0
			// tramite il while converto tale formato in quello desiderato dalla !vedi_giocate
			while(strcmp(parole[i],"") != 0){ 

				if(ruoteSpecificate == 0){
					char ruota[1024];
					if(strcmp(parole[i],"-n") == 0){
						ruoteSpecificate = 1;
						numeriSpecificati = 0;
						goto finewhile;
					}
					sprintf(ruota,"%s ",parole[i]);
					strcat(outputstr,ruota);
				}

				if(numeriSpecificati == 0){
					char numeri[1024];
					if(strcmp(parole[i],"-i") == 0){
						numeriSpecificati = 1;
						importiSpecificati = 0;
						goto finewhile;
					}
					sprintf(numeri,"%s ",parole[i]);
					strcat(outputstr,numeri);
				}

				if(importiSpecificati == 0){
					float importo;
					if(indiceImporto == 5){
						importiSpecificati = 1;
					}
					importo = atof(parole[i]);
					if(importo != 0){
						char str[1024];
						char tipoImporto[1024];
						if(indiceImporto == 0) strcpy(tipoImporto,"Estratto");
						if(indiceImporto == 1) strcpy(tipoImporto,"Ambo");
						if(indiceImporto == 2) strcpy(tipoImporto,"Terno");
						if(indiceImporto == 3) strcpy(tipoImporto,"Quaterna");
						if(indiceImporto == 4) strcpy(tipoImporto,"Cinquina");	
						sprintf(str,"* %f %s ",importo,tipoImporto);
						strcat(outputstr,str);
					}
					indiceImporto++;
				}
				finewhile:
				i++;
			}
			strcat(outputstr,"\n");
   			numeroRiga++; //nuova riga, incremento il numero di riga
    	}
    
    	line_size = getline(&line_buf, &line_buf_size, fd); //prendo la prossima linea e continuo

  	}
  	//Se outputstr non è stata modificata significa che non ho giocate per quell'utente
  	if(strcmp(outputstr,"") == 0) inviaMessaggio("**** SERVER: Siamo spiacenti, non è stata trovata alcuna giocata ****\n");
  		else inviaMessaggio(outputstr); //Altrimenti invio la outputstr
  	fclose(fd);
}

//Funzione di gestione del comando !vedi_giocate <tipo>

void comandoVedi_Giocate(char* buffer){

	//Variabili per ottenere le substrings dal buffer
	int numeroParole = 0;
	char delimiter[2] = " ";
	char* token = strtok(buffer, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
	char parole[3][1024];
							//parole[0] conterrà !vedi_giocate
							//parole[1] conterrà <tipo> 
							//parole[2] conterrà il sessionID

	while (token != NULL) { //while per riempire l'array di substring parole[]
		strcpy(parole[numeroParole],token);
		numeroParole++;
        token = strtok(NULL,delimiter); 
    }

    if(strcmp(parole[2],sessionID) != 0) { //controllo sul sessionID
    	inviaMessaggio("**** SERVER: Devi essere loggato prima di poter giocare una schedina ****\n");
    } else{
    	//in base al valore di <tipo> passo un parametro diverso alla leggiGiocate()
    	if(strcmp(parole[1],"1") == 0) leggiGiocate(1); //leggerà le giocate attive (quelle in giocate_in_attesa.txt)
    	if(strcmp(parole[1],"0") == 0) leggiGiocate(0); //leggerà le giocate già estratte (quelle in giocate_estratte.txt)
    }

}

//Funzione che invia le vincite dell'utente che ha richiesto la !vedi_vincite

void stampaVincite(){

	//variabili per la letture delle righe del file giocate_vincenti.txt
	char *line_buf = NULL;
  	size_t line_buf_size = 0;
  	ssize_t line_size;

  	FILE* fd;

  	char outputstr[1024] = ""; //stringa che verrà inviata al client
  	char date[10];  //stringa per contenere la data (mi serve per discriminare estrazioni diverse per lo stesso utente)
  	char time[9];	//stringa per contenere l'ora (mi serve per discriminare estrazioni diverse per lo stesso utente)
	int found = 0;  //se l'utente viene trovato all'interno di giocate_vincenti.txt viene settata

  	fd = fopen("giocate_vincenti.txt", "r");

  	if (!fd) {
    	printf("**** SERVER: Errore nell’apertura del file! ****\n");
    	return;
  	}

  	line_size = getline(&line_buf, &line_buf_size, fd); //prende la prima linea del file giocate_vincenti.txt

  	while (line_size >= 0) { // Loop in cui analizza tutte le righe del file giocate_vincenti.txt 

  		//stringa che conterrà "<username>:" mi serve per controllare che la vincita sia dell'utente che ha chiamato !vedi_vincite 
  		char stringaUtente[1024]; 

  		//variabili per spezzettare la riga del file in substring che verranno inserite in parole[]
  		int numeroParole = 0;
		char delimiter[2] = " ";
		char* token = strtok(line_buf, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
		char parole[15][1024];  //array di substring
								//parole[0] conterrà <username>:
								//parole[1] conterrà "dd-mm-yyyy"
								//parole[2] conterrà "hh:mm:ss"
								//parole[3] etc conterrà le ruote vincenti e poi i numeri etc

  		sprintf(stringaUtente,"%s:",loggedUsr); //preparo la stringa contentente il loggedUsr per il controllo sull'utente loggato

		while (token != NULL) { //while per riempire l'array parole[]
			strcpy(parole[numeroParole],token);
			numeroParole++;
        	token = strtok(NULL,delimiter); 
    	}

    	//line_buf è ora stata divisa in singole parole all'interno dell'array parole[i]

    	if(strcmp(parole[0],stringaUtente) == 0){ //è l'utente in questione
    		char firstline[1024] = "";
    		if(found == 0){ //se è la prima volta che trovo l'utente
    			found = 1; //setto la variabile, l'ho trovato
    			strcpy(date,parole[1]); //mi salvo il timestamp della giocata
    			strcpy(time,parole[2]);
    			sprintf(firstline,"Estrazione del %s ore %s\n",parole[1],parole[2]); //sicuramente sarà una nuova estrazione
    			strcat(outputstr,firstline);
    		}
    		if(found == 1){ //altrimenti se l'ho già trovato devo controllare se è una nuova estrazione o la stessa
    			int i = 3; //parto da 3, gli 0 1 2 non mi interessano
    			//mi servo delle variabili date e time
    			if(strcmp(parole[1],date) != 0 || strcmp(parole[2],time) != 0) { //se non è la stessa estrazione
    				strcpy(date,parole[1]); //salvo il nuovo timestamp
    				strcpy(time,parole[2]);
    				sprintf(firstline,"Estrazione del %s ore %s\n",parole[1],parole[2]); //nuova estrazione
    				strcat(outputstr,firstline);
    			}
    			while(strcmp(parole[i],"") != 0 ){ //scorro le substring a partire dalle ruote vincenti e inserisco tutto in outputstr
    				char str[1024];
    				if(strcmp(parole[i+1],"") == 0 ) sprintf(str,"%s",parole[i]); //aggiusto la formattazione della stringa
    				else sprintf(str,"%s ",parole[i]);
    				strcat(outputstr,str);
    				i++;
    			}
    		}
    		
    	}
   
    	line_size = getline(&line_buf, &line_buf_size, fd); //prendo la prossima linea e continuo

  	}
  	//se non è stata modificata outputstr significa che non ho trovato vincite per l'utente in questione
  	if(strcmp(outputstr,"") == 0 ) inviaMessaggio("**** SERVER: Siamo spiacenti, non è stata trovata alcuna vincita ****\n");
  		else inviaMessaggio(outputstr);
}

//Funzione che viene invocata non appena il server riceve il comando !vedi_vincite

void comandoVedi_Vincite(char* buffer){

	//variabili per spezzettare buffer in substrings
	int numeroParole = 0;
	char delimiter[2] = " ";
	char* token = strtok(buffer, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
	char parole[2][1024];

	while (token != NULL) { //while per riempire l'array di parole[]
		strcpy(parole[numeroParole],token);
		numeroParole++;
        token = strtok(NULL,delimiter); 
    }

    //parole[0] conterrà !vedi_vincite
    //parole[1] conterrà il sessionID

    if(strcmp(parole[1],sessionID) != 0) { //controllo che l'utente sia loggato
    	inviaMessaggio("**** SERVER: Devi essere loggato prima di poter giocare una schedina ****\n");
    } else{
    	stampaVincite(); //se è loggato chiamo la stampaVincite() che stampa le vincite relative a tale utente
    }

}

//Funzione invocata dalla !vedi_estrazione
//Nel caso l'utente non specifichi la ruota, il campo ruota conterrà "0"

void stampaEstrazioni(char* numEstrazioni, char* ruota){ 
	char outputstr[8192] = ""; //conterrà la risposta da inviare al client
	char estrazione[12][1024]; //array di stringhe, estrazione[0] conterrà la riga che inizia con Bari etc

	//variabili per la lettura dei byte del file
	int ch;
	int count = 0;

	int quanteEstrazioni = atoi(numEstrazioni); //conterrà l'int corrispondente a quante estrazioni voglio leggere
	int indiceRuota = individuaRuota(ruota); //conterrà -1 se la ruota era "0" altrimenti un valore da 0 a 10 a seconda della ruota

	int indice = 11; //indice per riempire l'array estrazione[]

	FILE* fd;
	fd=fopen("estrazioni.txt","r"); //apertura in sola lettura

	if (!fd) { //controllo errori in caso di apertura non riuscita
    	printf("**** SERVER: Errore nell’apertura del file! ****\n");
    	return;
  	}

  	fseek(fd,0,SEEK_END); //sposto il cursore a fine file

  	while (ftell(fd) > 1 && indice >= 0){ //while che continua a leggere dal file finche non finisce oppure indice < 0

  				  char str[1024];

            fseek(fd, -2  , SEEK_CUR); //sposto il cursore di 2 indietro dalla posizione corrente ovvero dalla fine del file

            if(ftell(fd) <= 2) //se sono sull'ultimo byte esco
              break;

            ch =fgetc(fd); //prendo il primo carattere

            if(ch != '\n') str[count++] = ch; //finchè leggo caratteri diversi da \n li inserisco in str

            if(ch == '\n') { //se leggo \n significa che ho letto una riga dell'estrazione

              int len = strlen(str);
              int w = 0;
              int i;

                	//in str ho i caratteri letti in ordine inverso
                	//ad esempio : 58   57   04   28   24 elanoizaN
                	//perciò tramite questo for li ordino in maniera corretta e li inserisco nell'array estrazione[]
                	//dato che leggo per prima l'ultima riga dell'estazione la inserisco per ultima nell'array estrazione[]
                	//per fare ciò utilizzo l'intero "indice" che ho inizializzato a 11 (che sono il numero di righe dell'estrazione)
                	//e lo decremento ogni volta che ho inserito una riga in estrazione[]

        			for(i= len - 1; i >= 0 && len > 0; i--){ 
        				estrazione[indice][w] = str[i];
        				w++;
        			}

        			indice --;

        			if(indice == -1){ //se ho completato l'estrazione

        				if(indiceRuota == -1) { //se non ho specificato una ruota
        					int k;
        					for(k=0; k<11; k++){ //inserisco tutte le righe lette in outputstr
    							strcat(outputstr,estrazione[k]);
    							strcat(outputstr,"\n");
    						}
    					} else { //altrimenti ci inserisco solo le righe della ruota interessata
    						  strcat(outputstr,estrazione[indiceRuota]);
    						  strcat(outputstr,"\n");
    					}

    					if(quanteEstrazioni > 1){ //se devo leggere più estrazioni
        					indice = 11; //reimposto l'indice
        					quanteEstrazioni--; 
        					if(indiceRuota == -1) strcat(outputstr,"\n"); 
        					//per separare le estrazioni (in caso non sia specificata la ruota)
        				}
        			}
        			count = 0;
            }

        }

    inviaMessaggio(outputstr);
    fclose(fd);
}

//Funzione di gestione per il comando !vedi_estrazione

void comandoVedi_Estrazione(char* buffer){

	//variabili per spezzettare buffer in substrings
	int i;
	int numeroParole = 0;
	char delimiter[2] = " ";
	char* token = strtok(buffer, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
	char parole[4][1024];

	for(i = 0; i<4; i++) strcpy(parole[i],"");

	while (token != NULL) { //while per riempire l'array di parole[]
		strcpy(parole[numeroParole],token);
		numeroParole++;
        token = strtok(NULL,delimiter); 
    }

    //devo ora vedere se è stata specificata la ruota o meno, nel caso sia specificata il sessionID sarà in parole[3] 
    //altrimenti in parole[2]

    if(strcmp(parole[3],"") != 0) {		//allora contiene il sessionID
    	if(strcmp(parole[3],sessionID) != 0) 
    		inviaMessaggio("**** SERVER: Devi essere loggato prima di poter visualizzare le estrazioni ****\n");
    	else {
    		//questo else indica che il comando presenta una ruota
    		stampaEstrazioni(parole[1],parole[2]); 
    	}
    } else{ //altrimenti è contenuto in parole[2]
    	if(strcmp(parole[2],sessionID) != 0) 
    		inviaMessaggio("**** SERVER: Devi essere loggato prima di poter visualizzare le estrazioni ****\n");
    	else {
    		//Questo else mi indica che il comando non sta specificando una ruota ma le richiede tutte
    		stampaEstrazioni(parole[1],"0"); //ruota default "0", indica che non è stata specificata
    	}
    }
}

//Funzione di gestione del comando !esci

void comandoEsci(char* buffer){

	//Variabili per spezzattare il buffer in substrings
	int numeroParole = 0;
	char delimiter[2] = " ";
	char* token = strtok(buffer, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
	char parole[2][1024];

	int i;
	for(i = 0; i<2; i++) strcpy(parole[i],""); //inizializzo l'array

	while (token != NULL) { //while per riempire l'array di parole[]
		strcpy(parole[numeroParole],token);
		numeroParole++;
        token = strtok(NULL,delimiter); 
    }
    //parole[0] conterrà !esci (non mi interessa)
    //parole[1] conterrà il session id
    if(strcmp(parole[1],sessionID) != 0){ 
    	//Devo verificare che l'utente sia loggato
    	//Se chiamo il comando !esci non da loggato parole[1] sarà a 0 in quanto non verrà concatenato il sessionID
    	//E dato che di base il sessionID è inizializzato a 0x0000000 entrerò comunque in questo if
    	inviaMessaggio("**** SERVER: Devi essere loggato prima di poter visualizzare le estrazioni ****\n");
    } else{ //se il comando è stato inviato da un utente loggato
    	strcpy(sessionID,"0x00000000"); //resetto il sessionID al valore di default
    	inviaMessaggio("**** SERVER: Disconnessione avvenuta con successo  ****\n"); //invio un messaggio di successo
    	printf("**** SERVER: Il client è stato disconnesso con successo ****\n"); 
    	connesso = 0; //mi fa uscire dal while del figlio provocando la chiusura del socket
    }
}

//Funzione invocata da main, controlla il buffer e in base alla parola chiave (comando) con cui inizia esegue diversi comandi

int riconosciComando(char* buffer){ //Restituisce un valore diverso a seconda del comando in questione

  	if(iniziaCon(buffer,"!signup") == 1) { 
  		comandoSignup(buffer); 
  		return 2; 
  	}
  	if(iniziaCon(buffer,"!login") == 1) { 
  		comandoLogin(buffer);
  		return 3;
  	}
  	if(iniziaCon(buffer,"!invia_giocata") == 1) { 
  		comandoInvia_Giocata(buffer);
  		return 4;
  	}

  	if(iniziaCon(buffer,"!vedi_giocate") == 1) { 
  		comandoVedi_Giocate(buffer);
  		return 5;
  	}

  	if(iniziaCon(buffer,"!vedi_estrazione") == 1) { 
  		comandoVedi_Estrazione(buffer);
  		return 6;
  	}


  	if(iniziaCon(buffer,"!vedi_vincite") == 1) { 
  		comandoVedi_Vincite(buffer);
  		return 7;
  	}

  	if(iniziaCon(buffer,"!esci") == 1) { 
  		comandoEsci(buffer);
  		return 8;
  	}

  	return 0;

}

//Funzione chiamata in estrazione(), serve per generare un numero casuale da 1 a 90

int randomNumber(){ //genera un numero casuale da 1 a 90 per le estrazioni
	int random_number;
	struct timeval t; //per utilizzare un seed che dipende dai secondi e nanosecondi correnti
	gettimeofday(&t, NULL);
	srand(t.tv_usec * t.tv_sec);
    random_number = rand() % 90; //numero da 0 a 89
    random_number ++;
    return random_number;
}

//Funzione che resetta e imposta ogni volta la struct globale schedinaInAttesa
//Tale schedina è utilizzata per il controllo delle vincite
//Per ogni riga contenuta nel file giocate_in_attesa.txt, riempie la schedina corrispondente a tale giocata

void impostaSchedinaAttesa(char* giocataAttesa){ //Ha come argomento una riga del file giocate_in_attesa.txt

	//variabili per spezzettare la riga passata come argomento in substrings
	int numeroParole = 0;
	char delimiter[2] = " ";
	char* token = strtok(giocataAttesa, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
	char parole[50][1024];

	int i=1; //variabili per il while, parto da 1 perchè parole[0] contiene l'username

	//Variabili binarie che se a 0 significa che è attivo ad esempio l'inserimento ruote (sto leggendo le ruote)
	//Quando ho finito di leggere le ruote (ad esempio) reimposto la variabile a 1 e imposto inserimentoNumeri = 0
	// e cosi via

	int inserimentoRuote = 0;
	int inserimentoNumeri = 1;
	int inserimentoImporti = 1;

	int indiceImporto = 0; //mi dice a quale indice corrisponde un determinato importo (estratto,ambo etc)

	while (token != NULL) { //while per riempire l'array parole[]
		strcpy(parole[numeroParole],token);
		numeroParole++;
        token = strtok(NULL,delimiter); 
    }

	//la riga di giocate_in_attesa è nel formato <username>: ruota1 ruota2 . . -n n1 n2 . . -i i1 i2 i3 i4 i5
	//NB: tutti gli importi anche se a 0 sono specificati
	//parole[0] conterrà <username>: / parole[1] conterrà la prima ruota etc

    inizializzaSchedina(1); //voglio inizializzare la schedina prima di riempirla 
    //(1 indica che la schedina da inizializzare è la schedinaInAttesa)

    //inizializzo queste variabili globali che a fine riempimento della schedina
    //mi diranno il n° di numeri giocati e il n° di ruote selezionate
    numeriGiocati = 0; 
    ruoteSelezionate = 0;

    strcpy(schedinaInAttesa.utente,parole[0]); //inserisco l'utente della giocata nella schedina

    while(strcmp(parole[i],"") != 0){ //scorro l'array di parole finchè non finisce

    	if(inserimentoRuote == 0){ //sto specificando le ruote
    		int indiceRuota; //valore di ritorno per individuaRuota();
    		if(strcmp(parole[i],"-n") == 0){ //ho trovato il "-n", ho finito di specificare le ruote
    			inserimentoRuote = 1;
    			inserimentoNumeri = 0;
    			goto finewhile; //ho il -n che non devo gestirlo, vado a fine while
    		}
    		indiceRuota = individuaRuota(parole[i]);
    		schedinaInAttesa.ruote[indiceRuota] = 1; //setto a 1 il campo corrispondente di ruote[]
    		ruoteSelezionate++; //nuova ruota individuata, incremento ruoteSelezionate
    	}

    	if(inserimentoNumeri == 0){ //sto specificando i numeri
    		int numero;
    		if(strcmp(parole[i],"-i") == 0){ //ho trovato il "-i", ho finito di specificare i numeri
    			inserimentoNumeri = 1;
    			inserimentoImporti = 0;
    			goto finewhile; //vado a fine while
    		}
    		numero = atoi(parole[i]); //cast della stringa a int
    		schedinaInAttesa.numeri[numero-1] = 1; //dato che il mio array numeri[] va da 0 a 89 sottraggo 1
    		numeriGiocati++; //incremento i numeri giocati
    	}

    	if(inserimentoImporti == 0){ //sto specificando gli importi
    		float importo;
    		if(strcmp(parole[i+1],"") == 0){ //se sono all'ultimo importo
    			inserimentoImporti = 1;
    		}
    		importo = atof(parole[i]); //cast da stringa a float
    		schedinaInAttesa.importi[indiceImporto] = importo;
    		indiceImporto++;
    	}

    	finewhile:
    	i++;
    }
}

//Funzione che calcola l'importo della vincita
//modalita indica il tipo di vincita ottenuta (estratto, ambo, terno etc)
//importo è la puntata effettuata sulla vincita ottenuta

float calcolaVincita(int modalita, float importo){

	//L'importo della vincita varia molto a seconda del tipo di vincita ottenuta
	//del numero di numeri giocati e delle ruote selezionate

	float vincita = 0; //variabile che conterrà l'importo finale della vincita
	int n = numeriGiocati; //numeri giocati dall'utente vincente
	int quante_ruote = ruoteSelezionate; //ruote selezionate dall'utente vincente

	//Devo tener conto degli ambo generabili, dei terni generabili e delle quaterne generabili e delle cinquine
	//Devo dividere la vincita per il numero di ruote specificate


	/* Formula per il calcolo degli ambi generabili, terni etc
	   n = numeriGiocati mentre k = 2 (ambo) 3(terno) etc
	
	n*(n-1)*(n-2)*..*(n-k+1)
	__________________________
            k!

	*/


	if(modalita == 1){
		vincita = 11.23; //valore fisso
		vincita = vincita/n;
		vincita = vincita/quante_ruote;
		vincita = vincita * importo;
		return vincita;
	}
	if(modalita == 2){
		// n(n-1)/2
		int numeratore =  n*(n-1);
		int ambiGenerabili = numeratore/2;
		vincita = 250; //valore fisso
		vincita = vincita/ambiGenerabili;
		vincita = vincita/quante_ruote;
		vincita = vincita * importo;
		return vincita;
	}
	if(modalita == 3){
		//n(n-1)(n-2)/6
		int numeratore = n*(n-1)*(n-2);
		int terniGenerabili = numeratore/6;
		vincita = 4500; //valore fisso
		vincita = vincita/terniGenerabili;
		vincita = vincita/quante_ruote;
		vincita = vincita * importo;
		return vincita;
	}
	if(modalita == 4){
		//n(n-1)(n-2)(n-3)/24
		int numeratore = n*(n-1)*(n-2)*(n-3);
		int quaterneGenerabili = numeratore/24;
		vincita = 120000; //valore fisso
		vincita = vincita/quaterneGenerabili;
		vincita = vincita/quante_ruote;
		vincita = vincita * importo;
		return vincita;
	}
	if(modalita == 5){
		//n(n-1)(n-2)(n-3)*(n-4)/120
		int numeratore = n*(n-1)*(n-2)*(n-3)*(n-4);
		int quaterneGenerabili = numeratore/120;
		vincita = 6000000; //valore fisso
		vincita = vincita/quaterneGenerabili;
		vincita = vincita/quante_ruote;
		vincita = vincita * importo;
		return vincita;
	}
	return vincita;
}

//Funzione chiamata nel while di controllaVincite()
//Le viene passata una riga dell'ultima estrazione ad esempio (Bari 20 34 56 78 90)
//E scorre tra tutte le righe del file estrazioni_in_attesa.txt per verificare se è avvenuta una vittoria su tale ruota

void controllaRigaEstrazione(char* riga){ 
//data una riga di una estrazione nel formato Bari x x x x x, controllo possibili match con le giocate in attesa

	//Variabili per spezzattare la riga di ultima_estrazione.txt (quella passata come argomento) in substrings
	int numeroParole = 0;
	char delimiter[2] = " ";
	char* token = strtok(riga, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
	char parole[6][1024];

	//Variabili per la lettura delle righe del file estrazioni_in_attesa.txt
	char *line_buf = NULL;
  	size_t line_buf_size = 0;
  	ssize_t line_size;

  	FILE* fd; //giocate_in_attesa.txt
  	FILE* fd1; //giocate_vincenti.txt

  	int indiceRuota; //variabile di ritorno per la individuaRuota()
  	int risultato; //va da 0 a 5 e se 0 non ho alcuna vincita, se 1 è Estratto, 2 Ambo e così via
  	int j; //for di controllo numeri
  	char temp_line_buf[1024];
  	char strVittoria[1024]; //stringa formattata come <username>: <timestamp> <ruota_vincente> <numeri_vincenti> >> <ambo/etc> <vincita>
  	int vittoria = 0; //in caso di vittoria viene settato

  	//variabili per ottenere il timestamp corrente
  	time_t t = time(NULL);
  	struct tm tm = *localtime(&t);
  	char timestamp[50]; //stringa dove verrà salvato il timestamp

	while (token != NULL) { //while per riempire l'array parole[]
		strcpy(parole[numeroParole],token);
		numeroParole++;
        token = strtok(NULL,delimiter); 
    }

    //parole[0] avrà la ruota che sto esaminando
    //parole[1] avrà il primo numero estratto e cosi via fino a parole[5]

    if ((fd=fopen("giocate_in_attesa.txt", "r"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n");
    if ((fd1=fopen("giocate_vincenti.txt", "a"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n");

    line_size = getline(&line_buf, &line_buf_size, fd); //estraggo la prima giocata in attesa e la inserisco in line_buf

    while (line_size >= 0) { // Loop in cui analizza tutte le righe del file delle giocate in attesa

    	//resetto le variabili per la vincita ad ogni riga estratta
    	risultato = 0; 
    	vittoria = 0;

    	//dato che la impostaSchedinaAttesa andrebbe a spezzettare la linea line_buf ne passo una copia
    	strcpy(temp_line_buf,line_buf); 

    	impostaSchedinaAttesa(temp_line_buf); //imposto la schedina con le informazioni della prima riga del file delle giocate in attesa

    	//formattazione per la stampa corretta sul file delle vincite
    	sprintf(strVittoria,"%s ",schedinaInAttesa.utente); // inserisco <username>:
    	sprintf(timestamp,"%02d-%02d-%d %02d:%02d:%02d ",tm.tm_mday, tm.tm_mon + 1,tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    	strcat(strVittoria,timestamp);
    	strcat(strVittoria,parole[0]); //inserisco la ruota attuale (stampo solo in caso di vincita)

    	indiceRuota = individuaRuota(parole[0]); //mi restituisce un indice da 0 a 10 a seconda della ruota che ho in parole[0]

    	if(schedinaInAttesa.ruote[indiceRuota] == 1){ //Se la schedina in attesa contiene la ruota della riga che sto controllando
    		for(j=1; j<6; j++){ //for per scorrere tra i numeri estratti parole[1] -> parole[5]
    			int numeroEstrazione = atoi(parole[j]); //faccio il cast del numero contenuto nella stringa
    			if(schedinaInAttesa.numeri[numeroEstrazione-1] == 1) { //ho puntato su un numero legato all'estazione
    				char str[1024];
    				sprintf(str," %i",numeroEstrazione);
    				strcat(strVittoria,str); //inserisco i numeri vincenti in strVittoria
    				//incremento risultato se ho avuto un match di un altro numero precedentemente, altrimenti ho un estratto per ora
    				if(risultato == 4 || risultato == 3 || risultato == 2 || risultato == 1) risultato++;
    				if(risultato == 0) risultato = 1;
    			}
    		}

    		strcat(strVittoria," >> ");

    		if(risultato == 5){ //ho fatto cinquina
    			//controllo se ho puntato sulla cinquina (potrei aver fatto cinquina ma non averci scommesso niente)
    			if(schedinaInAttesa.importi[4] != 0) { //ho puntato sulla cinquina, ho vinto
    				float vincita; //float per il calcolo dell'importo della vincita
    				char strVincita[1024]; //stringa per memorizzare l'importo della vincita
    				vincita = calcolaVincita(5,schedinaInAttesa.importi[4]);
    				sprintf(strVincita,"%f ",vincita);
    				strcat(strVittoria,"Cinquina ");
    				strcat(strVittoria,strVincita);
    				vittoria = 1; //imposto vittoria a 1 (ho vinto)
    			}
    		}

    		//i >= sono stati inseriti perchè se ho risultato == 5 automaticamente ho fatto sia quaterna/terno etc

    		if(risultato >= 4){ //ho fatto quaterna

    			//qua ho il controllo anche sulla variabile vittoria, nel caso avessi fatto cinquina e ci avessi scommesso
    			//non dovrei conteggiare le vincite anche per le sottovittorie

    			if(schedinaInAttesa.importi[3] != 0 && vittoria == 0) { //controllo se ho puntato sulla quaterna
    				//ho puntato sulla quaterna -> vittoria
    				float vincita;
    				char strVincita[1024];
    				vincita = calcolaVincita(4,schedinaInAttesa.importi[3]);
    				sprintf(strVincita,"%f ",vincita);
    				strcat(strVittoria,"Quaterna ");
    				strcat(strVittoria,strVincita);
    				vittoria = 1;
    			}
    		}

    		if(risultato >= 3){ //ho fatto terno
    			if(schedinaInAttesa.importi[2] != 0 && vittoria == 0) { //controllo se ho puntato sul terno
    				//ho puntato sull'terno -> vittoria
    				float vincita;
    				char strVincita[1024];
    				vincita = calcolaVincita(3,schedinaInAttesa.importi[2]);
    				sprintf(strVincita,"%f ",vincita);
    				strcat(strVittoria,"Terno ");
    				strcat(strVittoria,strVincita);
    				vittoria = 1;
    			}
    		}

    		if(risultato >= 2){ //ho fatto ambo
    			if(schedinaInAttesa.importi[1] != 0 && vittoria == 0) { //controllo se ho puntato sull'ambo
    				//ho puntato sull'ambo -> vittoria
    				float vincita;
    				char strVincita[1024];
    				vincita = calcolaVincita(2,schedinaInAttesa.importi[1]);
    				sprintf(strVincita,"%f ",vincita);
    				strcat(strVittoria,"Ambo ");
    				strcat(strVittoria,strVincita);
    				vittoria = 1;
    			}
    		}

    		if(risultato >= 1){ //ho fatto un estratto
    			if(schedinaInAttesa.importi[0] != 0 && vittoria == 0) { //controllo se ho puntato sull'estratto
    				float vincita;
    				char strVincita[1024];
    				vincita = calcolaVincita(2,schedinaInAttesa.importi[1]);
    				sprintf(strVincita,"%f ",vincita);
    				strcat(strVittoria,"Estratto ");
    				strcat(strVittoria,strVincita);
    				vittoria = 1;
    			}
    		}
    		
    	}

    	if(vittoria == 1) fprintf(fd1,"%s\n",strVittoria); //scrivo la vittoria sul file
    
    	line_size = getline(&line_buf, &line_buf_size, fd); //prendo la prossima linea e continuo

  	}

  	fclose(fd);
  	fclose(fd1);
}

//Funzione chiamata al termine di ogni estrazione, va a scorrere le righe dell'ultima estrazione (scorre le ruote)
//E per ogni riga (ruota) chiama la funzione controllaRigaEstrazione()
//Quest'ultima controlla ogni riga del file estrazioni_in_attesa.txt per verificare se vi è una vincita

void controllaVincite(){

	FILE* fd;

	char *line_buf = NULL;
  	size_t line_buf_size = 0;
  	ssize_t line_size;

  	if ((fd=fopen("ultima_estrazione.txt", "r"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n");

  	line_size = getline(&line_buf, &line_buf_size, fd); //prende la prima linea del file fd

  	while (line_size >= 0) { // Loop in cui analizza tutte le linee del file ultima_estrazione.txt 

    	controllaRigaEstrazione(line_buf); //per ogni riga di ultima_estrazione.txt controllo se vi è una vittoria
    
   
    	line_size = getline(&line_buf, &line_buf_size, fd); //prendo la prossima linea e continuo

  	}
}

//Funzione che mi inserisce al termine di un'estrazione tutte le giocate in attesa nel file giocate_estratte.txt
//Tale file conterrà le giocate già estratte (mi serve per il !vedi_giocate)

void inserimentoGiocateEstratte(){ 
	FILE* fd;  //giocate_in_attesa.txt
	FILE* fd1; //giocate_estratte.txt
	char *line_buf = NULL;
  	size_t line_buf_size = 0;
  	ssize_t line_size;

  	if ((fd=fopen("giocate_in_attesa.txt", "r"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n");
  	if ((fd1=fopen("giocate_estratte.txt", "a"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n");

  	line_size = getline(&line_buf, &line_buf_size, fd); //prende la prima linea del file fd

  	while (line_size >= 0) { // Loop in cui analizza tutte le linee del file giocate_in_attesa.txt 

  		fprintf(fd1, "%s", line_buf); //stampo la riga di giocate_in_attesa.txt direttamente in giocate_estratte.txt
   
    	line_size = getline(&line_buf, &line_buf_size, fd); //prendo la prossima linea e continuo

  	}
  	fclose(fd);
  	fclose(fd1);

}
        
//Funzione che esegue una estrazione del lotto, viene chiamata ciclicamente ogni <periodo>

void estrazione(int sig){ 
	alarm(periodo*60); 
	//manderà una alarm dopo tot secondi specificati da <periodo>, tale segnale provocherà la ricorsiva chiamata ad estrazione()

	int i; //variabile per il for sulle ruote
	int j; //variabile per il for sui numeri

	FILE* fd;  //estrazioni.txt
	FILE* fd1; //ultima_extrazione.txt
	FILE* fd2; //giocate_in_attesa.txt

	//L'estrazione viene inserita nel file estrazioni.txt e nel file ultima_estrazione.txt
	//Quest'ultimo voglio che contenga solo l'ultima estrazione eseguita, perciò devo cancellare il file ad ogni estrazione

	if ((fd=fopen("estrazioni.txt", "a"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n");

	//chiamando la fopen con "w" automaticamente cancella il contenuto ad ogni estrazione mantenendomi in ultima_estrazione.txt
	//solo l'estrazione più recente che è quello che voglio
	if ((fd1=fopen("ultima_estrazione.txt", "w"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n");

	for(i = 0; i<11; i++){ //Devo stampare nel file una riga per ogni ruota

		char outputRiga[1024]=""; //stringa che conterrà la riga da stampare
		int numeriRiga[5]; //inserisco i numeri estratti ad ogni riga in questo array, mi servirà per il controllo sui duplicati

		if(i==0) strcat(outputRiga,"Bari      "); 
		if(i==1) strcat(outputRiga,"Cagliari  "); 
		if(i==2) strcat(outputRiga,"Firenze   "); 
		if(i==3) strcat(outputRiga,"Genova    "); 
		if(i==4) strcat(outputRiga,"Milano    "); 
		if(i==5) strcat(outputRiga,"Napoli    "); 
		if(i==6) strcat(outputRiga,"Palermo   "); 
		if(i==7) strcat(outputRiga,"Roma      "); 
		if(i==8) strcat(outputRiga,"Torino    "); 
		if(i==9) strcat(outputRiga,"Venezia   "); 
		if(i==10) strcat(outputRiga,"Nazionale "); 

		for(j = 0; j<5; j++){ //for per l'estrazione dei 5 numeri per ogni ruota

			int k; //variabile per il for su numeriRiga per il controllo sui duplicati
			int numero; //variabile in cui verrà salvato il numero estratto
			int duplicato = 0; //variabile che si setta a 1 in caso sia presente un duplicato
			char str[20]; //stringa temporanea in cui verranno concatenati i numeri estratti

			numero = randomNumber(); //vado a generare tramite questa funzione un numero casuale da 1 a 90

			for(k=0; k<j; k++){ //for controllo duplicati
				if(numeriRiga[k] == numero) duplicato = 1;
			}
			if(duplicato == 1) j--; //se è già presente torno indietro nel for per ripetere il procedimento
				else { //è un nuovo numero
					numeriRiga[j] = numero; //lo inserisco nell'array dei numeri estratti

					//nel caso il numero sia di due cifre devo aggiustare la formattazione
					if (numero >= 10 && numero <= 90) sprintf(str,"%i   ",numero); 
						else sprintf(str," %i   ",numero); //numero ad una cifra
					strcat(outputRiga,str); //concateno la stringa con i numeri alla stringa della riga
			}
		}
		fprintf(fd, "%s\n", outputRiga); //inserisce la stringa nel formato impostato nel file estrazioni.txt
		fprintf(fd1, "%s\n", outputRiga); //inserisce la stringa nel formato impostato nel file ultima_estrazione.txt
	}
	fprintf(fd, "\n");
	fclose(fd);
	fclose(fd1);

	//Inserita l'estrazione devo controllare tra le estrazioni in attesa se una di queste è vincente o meno
	//Inserisco le estrazioni vincenti nel file giocate_vincenti.txt
	controllaVincite();
	inserimentoGiocateEstratte(); //prima di svuotare il file giocate_in_attesa.txt me le salvo in giocate_estratte.txt

	//Svuoto il file giocate_in_attesa.txt (non mi servono più)
	if ((fd2=fopen("giocate_in_attesa.txt", "w"))==NULL) printf("**** SERVER: Errore nell’apertura del file! ****\n");
	printf("**** SERVER: Estrazione avvenuta ****\n");
	fclose(fd2);
}

//Funzione che viene chiamata appena dopo la accept(), verifica che il client non sia bloccato in seguito a 3 tentativi
//falliti di login e che siano trascorsi i 30 minuti

int controllaConnessione(){ //restituisce 0 se devo vietare la connessione, 1 altrimenti
	char timestamp[1024];

	//variabili per la lettura delle righe di un file
	char *line_buf = NULL;
  	size_t line_buf_size = 0;
  	ssize_t line_size;

  	FILE *fd = fopen("login_failed.txt", "r"); //apro login_failed.txt in lettura

  	if (!fd) {
    	printf("**** CLIENT: Errore nell’apertura del file! ****\n");
  	}

	sprintf(timestamp,"%d",(int)time(NULL));

	line_size = getline(&line_buf, &line_buf_size, fd); //prende la prima linea del file fd

  	while (line_size >= 0) { // Loop in cui analizza tutte le linee del file login_failed.txt

  		//Variabili per ottenere le substring di ogni riga del file login_failed

  		int numeroParole = 0;
		char delimiter[2] = "/";
		char* token = strtok(line_buf, delimiter); //passandogli un delimitatore va a spezzettare la stringa ogni volta che incontra " "
		char parole[3][1024]; //array per le substrings
							  //parole[0] conterrà il timestamp sotto forma di secondi
							  //parole[1] conterrà l'IP del client
							  //parole[2] conterrà "data ora" (che non ci interessa)

    	while (token != NULL) { //con questo while vado a riempire l'array parole[] con le substring ottenute
			strcpy(parole[numeroParole],token);
			numeroParole++;
        	token = strtok(NULL,delimiter); 
    	}

    	//inet_ntoa(cl_addr.sin_addr) mi restituisce una stringa contenente l'ip del client

    	if(strcmp(inet_ntoa(cl_addr.sin_addr),parole[1]) == 0){ 
    		//ho trovato nel file login_failed.txt un match con l'IP client che vuole connettersi
    		//devo adesso controllare se il fallimento è abbastanza vecchio da permettere la connessione o meno
    		//tale controllo lo faccio utilizzando il timestamp attuale sotto forma di secondi e quello registrato nel file
    		//se differiscono di meno 60*30 secondi (che equivale a dire meno di 30minuti) devo bloccare la connessione
    		int timestampAttuale = atoi(timestamp);
    		int timestampFile = atoi(parole[0]);
    		int differenza = timestampAttuale - timestampFile;
    		if(differenza <= 1800) return 0; //1800 è 30*60 ovvero 30 minuti
    	}
   
    	line_size = getline(&line_buf, &line_buf_size, fd); //prendo la prossima linea e continuo

  	}
  	return 1;
}

int main(int argc, char* argv[]){

//Devo avviare il server come segue
// ./lotto_server <porta> <periodo>
//argc può valere 2 o 3 con argv[1]=porta e argv[2]=periodo (se specificata)

	int porta; //argomenti main
	int ret; //ret per gestire i ritorni di funzione in caso di errore 
	//int len;
	socklen_t len;
    pid_t pid;
    char msgServer[1024]; //messaggio ricevuto dal client e successivamente reinviato
    char tmpBuffer[1024]; //buffer temporaneo
    int idComando = 0; //Variabile di ritorno per la riconosciComando()

	if(argc < 2 || argc > 3) { //in caso di assenza di parametri, o se ne venissero specificati di più
		printf("**** ERRORE: per poter avviare il server devi specificare almeno la porta ****\n");
		exit(-1);
	}

	porta = atoi(argv[1]); //cast a int

	if(argc == 3) periodo = atoi(argv[2]); else periodo = 5; //default value 5 minuti (se non lo specifico)

	sd = socket(AF_INET, SOCK_STREAM, 0); //creazione socket
	printf("**** SERVER: Socket creato con successo ****\n");

	/* Creazione indirizzo server */

	memset(&my_addr, 0, sizeof(my_addr)); // Pulizia 
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(porta);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr) ); //setting
    printf("**** SERVER: Bind avvenuta con successo ****\n");
    ret = listen(sd, 10); //mi metto in ascolto
    printf("**** SERVER: Server in attesa di connessione ****\n");

 	if(ret < 0){
        perror("**** SERVER: Errore in fase di bind ****\n");
        exit(-1);
    }

    signal( SIGALRM, estrazione ); 
    //Ridefinisce l'handler per il segnale SIGALRM
    //quando verrà chiamata la alarm e saranno passati i secondi verrà chiamata la funzione estrazione()
    alarm(periodo*60); //chiamo la alarm della durata di periodo (moltiplico per 60 in modo da ottenere i secondi)

   while(1){
        
        len = sizeof(cl_addr);
    
        // Accetto nuove connessioni
        new_sd = accept(sd, (struct sockaddr*) &cl_addr, &len);
        connesso = 1; //Connessione avvenuta con successo (variabile da passare al while del figlio)

        if(controllaConnessione() == 0){ //Controllo se il client è nella blacklist per troppi tentativi falliti di login
        	printf("**** SERVER: Errore, la connessione con client è stata bloccata ****\n");
        	inviaMessaggio("ErrorCode-0x001: Mi dispiace ma al momento ti è stato vietato l'accesso, riprova più tardi\n");
        	connesso = 0; //Faccio tornare connesso a 0 in modo da non entrare neanche nel while e chiudere subito il socket
        } else {
        	printf("**** SERVER: Connessione con il client stabilita con successo ****\n");
        	inviaMessaggio("**** SERVER: Connessione stabilita correttamente ****\n");
        }
        pid = fork();
        
        if( pid == 0 ){

            // Sono nel processo figlio
            
            close(sd);
        
            while(connesso == 1){

            	riceviMessaggio(msgServer); //Ricevo il comando dal client

            	printf("il comando ricevuto è : %s\n", msgServer); 

                strcpy(tmpBuffer,msgServer); 

                //dato che la riconosciComando va a spezzare il buffer con la strtok
                //copio il buffer in un buffer temporaneo e uso quello

                idComando = riconosciComando(tmpBuffer);

                if(idComando == 0){ //comando non riconosciuto 
                	//è un controllo aggiuntivo anche se i comandi inviati dal client vengono prima controllati
                	//al server arrivano solo comandi puliti e formattati correttamente
                	//però nel caso riesca a passare qualche comando non pulito verrà gestito in questo if
                	printf("**** SERVER: Il comando ricevuto non è stato riconosciuto ****\n");
                	exit(-1);
                }
            }
            
            close(new_sd);
            
            exit(1);
        } 

        else {

            // Processo padre

            close(new_sd);
        }
    }
}

