/*! \file simptcp_lib.c
    \brief Defines the functions that gather the actions performed by a simptcp protocol entity in
    reaction to events (system calls, simptcp packet arrivals, timeouts) given its state at a point in time  (closed, ..established,..).

    \author{DGEI-INSAT 2010-2011}
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>              /* for errno macros */
#include <sys/socket.h>
#include <netinet/in.h>         /* for htons,.. */
#include <arpa/inet.h>
#include <unistd.h>             /* for usleep() */
#include <sys/time.h>           /* for gettimeofday,..*/

#include <libc_socket.h>
#include <simptcp_packet.h>
#include <simptcp_entity.h>
#include "simptcp_func_var.c"    /* for socket related functions' prototypes */
#include <term_colors.h>        /* for color macros */
#define __PREFIX__              "[" COLOR("SIMPTCP_LIB", BRIGHT_YELLOW) " ] "
#include <term_io.h>

#ifndef __DEBUG__
#define __DEBUG__               1
#endif


/*! \fn char *  simptcp_socket_state_get_str(simptcp_socket_state_funcs * state)
* \brief renvoie une chaine correspondant a l'etat dans lequel se trouve un socket simpTCP. Utilisee a des fins d'affichage
* \param state correspond typiquement au champ socket_state de la structure #simptcp_socket qui indirectement identifie l'etat dans lequel le socket se trouve et les fonctions qu'il peut appeler depuis cet etat
* \return chaine de carateres correspondant a l'etat dans lequel se trouve le socket simpTCP
*/
char *  simptcp_socket_state_get_str(simptcp_socket_state_funcs * state) {
    if (state == &  simptcp_socket_states.closed)
        return "CLOSED";
    else if (state == & simptcp_socket_states.listen)
        return "LISTEN";
    else if (state == & simptcp_socket_states.synsent)
        return "SYNSENT";
    else if (state == & simptcp_socket_states.synrcvd)
        return "SYNRCVD";
    else if (state == & simptcp_socket_states.established)
        return "ESTABLISHED";
    else if (state == & simptcp_socket_states.closewait)
        return "CLOSEWAIT";
    else if (state == & simptcp_socket_states.finwait1)
        return "FINWAIT1";
    else if (state == & simptcp_socket_states.finwait2)
        return "FINWAIT2";
    else if (state == & simptcp_socket_states.closing)
        return "CLOSING";
    else if (state == & simptcp_socket_states.lastack)
        return "LASTACK";
    else if (state == & simptcp_socket_states.timewait)
        return "TIMEWAIT";
    else
        assert(0);
}

/**
 * \brief called at socket creation
 * \return the first sequence number to be used by the socket
 */
unsigned int get_initial_seq_num(){
  unsigned int init_seq_num;
#if __DEBUG__
        printf("function %s called\n", __func__);
#endif
    // on utilise scrand() pour obtenir un nombre aléatoire d'ISN entre 1 et 100
    srand((unsigned)time(NULL));
    init_seq_num = rand()%100+1;
    return init_seq_num;
}

/*!
* \brief Initialise les champs de la structure #simptcp_socket
* \param sock pointeur sur la structure simptcp_socket associee a un socket simpTCP
* \param lport numero de port associe au socket simptcp local
*/
void init_simptcp_socket(struct simptcp_socket *sock, unsigned int lport)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif

    assert(sock != NULL);
    lock_simptcp_socket(sock);

    /* Initialization code */

    sock->socket_type = unknown;
    sock->new_conn_req=NULL;
    sock->pending_conn_req=0;

    /* set simpctp local socket address */
    memset(&(sock->local_simptcp), 0, sizeof (struct sockaddr));
    sock->local_simptcp.sin_family = AF_INET;
    sock->local_simptcp.sin_addr.s_addr = htonl(INADDR_ANY); /* htonl() host to network long*/
    sock->local_simptcp.sin_port = htons(lport);
    memset(&(sock->remote_simptcp), 0, sizeof (struct sockaddr));
    sock->socket_state = &(simptcp_entity.simptcp_socket_states->closed);

    /* protocol entity sending side */
    sock->socket_state_sender=-1;
    sock->next_seq_num=get_initial_seq_num();
    memset(sock->out_buffer, 0, SIMPTCP_SOCKET_MAX_BUFFER_SIZE);
    sock->out_len=0;
    sock->nbr_retransmit=0;
    sock->timer_duration=1500;

    /* protocol entity receiving side */
    sock->socket_state_receiver=-1;
    sock->next_ack_num=0;
    memset(sock->in_buffer, 0, SIMPTCP_SOCKET_MAX_BUFFER_SIZE);
    sock->in_len=0;

    /* MIB statistics initialisation  */
    sock->simptcp_send_count=0;
    sock->simptcp_receive_count=0;
    sock->simptcp_in_errors_count=0;
    sock->simptcp_retransmit_count=0;

    pthread_mutex_init(&(sock->mutex_socket), NULL);

    /* Add Optional field initialisations */
    unlock_simptcp_socket(sock);
}

/*! \fn int create_simptcp_socket()
* \brief cree un nouveau socket SimpTCP et l'initialise.
* parcourt la table de  descripteur a la recheche d'une entree libre. S'il en trouve, cree
* une nouvelle instance de la structure simpTCP, la rattache a la table de descrpteurs et l'initialise.
* \return descripteur du socket simpTCP cree ou une erreur en cas d'echec
*/
int create_simptcp_socket()
{
  int fd;

  /* get a free simptcp socket descriptor */
  for (fd=0;fd< MAX_OPEN_SOCK;fd++) {
    if ((simptcp_entity.simptcp_socket_descriptors[fd]) == NULL){
      /* this is a free descriptor */
      /* Allocating memory for the new simptcp_socket */
      simptcp_entity.simptcp_socket_descriptors[fd] =
        (struct simptcp_socket *) malloc(sizeof(struct simptcp_socket));
      if (!simptcp_entity.simptcp_socket_descriptors[fd]) {
        return -ENOMEM;
      }
      /* initialize the simptcp socket control block with
         local port number set to 15000+fd */
      init_simptcp_socket(simptcp_entity.simptcp_socket_descriptors[fd],15000+fd);
      simptcp_entity.open_simptcp_sockets++;

      /* return the socket descriptor */
      return fd;
    }
  } /* for */
  /* The maximum number of open simptcp
     socket reached  */
  return -ENFILE;
}

/*! \fn void print_simptcp_socket(struct simptcp_socket *sock)
* \brief affiche sur la sortie standard les variables d'etat associees a un socket simpTCP
* Les valeurs des principaux champs de la structure simptcp_socket d'un socket est affichee a l'ecran
* \param sock pointeur sur les variables d'etat (#simptcp_socket) d'un socket simpTCP
*/
void print_simptcp_socket(struct simptcp_socket *sock)
{
    printf("----------------------------------------\n");
    printf("local simptcp address: %s:%hu \n",inet_ntoa(sock->local_simptcp.sin_addr),ntohs(sock->local_simptcp.sin_port));
    printf("remote simptcp address: %s:%hu \n",inet_ntoa(sock->remote_simptcp.sin_addr),ntohs(sock->remote_simptcp.sin_port));
    printf("socket type      : %d\n", sock->socket_type);
    printf("socket state: %s\n",simptcp_socket_state_get_str(sock->socket_state) );
    if (sock->socket_type == listening_server)
      printf("pending connections : %d\n", sock->pending_conn_req);
    printf("sending side \n");
    printf("sender state       : %d\n", sock->socket_state_sender);
    printf("transmit  buffer occupation : %d\n", sock->out_len);
    printf("next sequence number : %u\n", sock->next_seq_num);
    printf("retransmit number : %u\n", sock->nbr_retransmit);

    printf("Receiving side \n");
    printf("receiver state       : %d\n", sock->socket_state_receiver);
    printf("Receive  buffer occupation : %d\n", sock->in_len);
    printf("next ack number : %u\n", sock->next_ack_num);

    printf("send count       : %lu\n", sock->simptcp_send_count);
    printf("receive count       : %lu\n", sock->simptcp_receive_count);
    printf("receive error count       : %lu\n", sock->simptcp_in_errors_count);
    printf("retransmit count       : %lu\n", sock->simptcp_retransmit_count);
    printf("----------------------------------------\n");
}

/*! \fn inline int lock_simptcp_socket(struct simptcp_socket *sock)
* \brief permet l'acces en exclusion mutuelle a la structure #simptcp_socket d'un socket
* Les variables d'etat (#simptcp_socket) d'un socket simpTCP peuvent etre modifiees par
* l'application (client ou serveur via les appels systeme) ou l'entite protocolaire (#simptcp_entity_handler).
* Cette fonction repose sur l'utilisation de semaphores binaires (un semaphore par socket simpTCP).
* Avant tout  acces en ecriture a ces variables, l'appel a cette fonction permet
* 1- si le semaphore est disponible (unlocked) de placer le semaphore dans une etat indisponible
* 2- si le semaphore est indisponible, d'attendre jusqu'a ce qu'il devienne disponible avant de le "locker"
* \param sock pointeur sur les variables d'etat (#simptcp_socket) d'un socket simpTCP
*/
inline int lock_simptcp_socket(struct simptcp_socket *sock)
{
#if __DEBUG__
        printf("function %s called\n", __func__);
#endif

    if (!sock)
        return -1;

    return pthread_mutex_lock(&(sock->mutex_socket));
}

/*! \fn inline int unlock_simptcp_socket(struct simptcp_socket *sock)
* \brief permet l'acces en exclusion mutuelle a la structure #simptcp_socket d'un socket
* Les variables d'etat (#simptcp_socket) d'un socket simpTCP peuvent etre modifiees par
* l'application (client ou serveur via les appels systeme) ou l'entite protocolaire (#simptcp_entity_handler).
* Cette fonction repose sur l'utilisation de semaphores binaires (un semaphore par socket simpTCP).
* Après un acces "protege" en ecriture a ces variables, l'appel a cette fonction permet de liberer le semaphore
* \param sock pointeur sur les variables d'etat (#simptcp_socket) d'un socket simpTCP
*/
inline int unlock_simptcp_socket(struct simptcp_socket *sock)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif

    if (!sock)
        return -1;

    return pthread_mutex_unlock(&(sock->mutex_socket));
}

/*! \fn void start_timer(struct simptcp_socket * sock, int duration)
 * \brief lance le timer associe au socket en fixant l'instant ou la duree a mesurer "duration" sera ecoulee (champ "timeout" de #simptcp_socket)
 * \param sock  pointeur sur les variables d'etat (#simptcp_socket) d'un socket simpTCP
 * \param duration duree a mesurer en ms
*/
void start_timer(struct simptcp_socket * sock, int duration)
{
	struct timeval t0;
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	assert(sock!=NULL);
	gettimeofday(&t0,NULL);
	sock->timeout.tv_sec=t0.tv_sec + (duration/1000);
	sock->timeout.tv_usec=t0.tv_usec + (duration %1000)*1000;
}

/*! \fn void stop_timer(struct simptcp_socket * sock)
 * \brief stoppe le timer en reinitialisant le champ "timeout" de #simptcp_socket
 * \param sock  pointeur sur les variables d'etat (#simptcp_socket) d'un socket simpTCP
 */
void stop_timer(struct simptcp_socket * sock)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	assert(sock!=NULL); /*si sock=NULL -> arrêter le programme*/
	sock->timeout.tv_sec=0;
	sock->timeout.tv_usec=0;
}

/*! \fn int has_active_timer(struct simptcp_socket * sock)
 * \brief Indique si le timer associe a un socket simpTCP est actif ou pas
 * \param sock  pointeur sur les variables d'etat (#simptcp_socket) d'un socket simpTCP
 * \return 1 si timer actif, 0 sinon
 */
int has_active_timer(struct simptcp_socket * sock)
{
	return (sock->timeout.tv_sec!=0) || (sock->timeout.tv_usec!=0);
}

/*! \fn int is_timeout(struct simptcp_socket * sock)
 * \brief Indique si la duree mesuree par le timer associe a un socket simpTCP est actifs'est ecoulee ou pas
 * \param sock  pointeur sur les variables d'etat (#simptcp_socket) d'un socket simpTCP
 * \return 1 si duree ecoulee, 0 sinon
 */
int is_timeout(struct simptcp_socket * sock)
{
	struct timeval t0;
	assert(sock!=NULL);
	/* make sure that the timer is launched */
	assert(has_active_timer(sock));
	gettimeofday(&t0,NULL);
	return ((sock->timeout.tv_sec < t0.tv_sec) ||
			( (sock->timeout.tv_sec == t0.tv_sec) && (sock->timeout.tv_usec < t0.tv_usec)));
}

/*! \fn void send_pdu_flag(struct simptcp_socket * sock, char flag)
 * \brief fonction qui permet de créer un PDU type flag et l'envoyer
 * \param sock  pointeur sur les variables d'etat (#simptcp_socket) d'un socket simpTCP
 * \param flag  type de flag de PDU
 */
void send_pdu_flag(struct simptcp_socket * sock, char flag)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	// on ajoute les informations nécessaires
	simptcp_set_flags(sock->out_buffer,flag);
	simptcp_set_sport(sock->out_buffer,htons(sock->local_simptcp.sin_port));
	simptcp_set_dport(sock->out_buffer,htons(sock->remote_simptcp.sin_port));
	simptcp_set_ack_num(sock->out_buffer,sock->next_ack_num);
	simptcp_set_seq_num(sock->out_buffer,sock->next_seq_num);
	simptcp_set_head_len(sock->out_buffer,SIMPTCP_GHEADER_SIZE);
	simptcp_set_total_len(sock->out_buffer,SIMPTCP_GHEADER_SIZE);
	simptcp_add_checksum(sock->out_buffer,SIMPTCP_GHEADER_SIZE);
	// on incrémente le nombre des PDUs envoyés
	sock->simptcp_send_count++;
	// on l'envoie
	libc_sendto(simptcp_entity.udp_fd,sock->out_buffer,SIMPTCP_GHEADER_SIZE,0,(struct sockaddr*)(&(sock->remote_udp)),sizeof(sock->remote_udp));
}

/*! \fn void send_pdu_data(struct simptcp_socket * sock, const void * buf, size_t n){
 * \brief fonction qui permet de créer un PDU type data et l'envoyer
 * \param sock  pointeur sur les variables d'etat (#simptcp_socket) d'un socket simpTCP
 * \param buf   pointeur sur le message a transmettre
 * \param n		taille du buffer (contenant le message) en octets pointe par buf
 */
ssize_t send_pdu_data(struct simptcp_socket * sock, const void * buf, size_t n){
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	// on ajoute les informations nécessaires
	size_t total_len = SIMPTCP_GHEADER_SIZE + n;
	simptcp_set_flags(sock->out_buffer,0);
	simptcp_set_sport(sock->out_buffer,htons(sock->local_simptcp.sin_port));
	simptcp_set_dport(sock->out_buffer,htons(sock->remote_simptcp.sin_port));
	simptcp_set_ack_num(sock->out_buffer,0);
	simptcp_set_seq_num(sock->out_buffer,get_initial_seq_num());
	// on copie le message du buffer dans le socket
    memcpy(sock->out_buffer+SIMPTCP_GHEADER_SIZE,buf,n);
    // on set la taille de PDU
    simptcp_set_head_len(sock->out_buffer,SIMPTCP_GHEADER_SIZE);
    simptcp_set_total_len(sock->out_buffer,total_len);
    simptcp_add_checksum(sock->out_buffer,total_len);
	// on incrémente le nombre des PDUs envoyés
	sock->simptcp_send_count++;
    // on l'envoie
    return libc_sendto(simptcp_entity.udp_fd,sock->out_buffer,total_len,0,(struct sockaddr*)(&(sock->remote_udp)),sizeof(sock->remote_udp));
  }

/*** socket state dependent functions ***/


/*********************************************************
 * closed_state functions *
 *********************************************************/

/*! \fn int closed_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "closed"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */
int closed_simptcp_socket_state_active_open (struct simptcp_socket * sock, struct sockaddr * addr, socklen_t len)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	// le socket est du type client
	sock->socket_type = client;
	// on configure la destination du socket
	memcpy(&(sock->remote_simptcp),addr,len);
	sock->remote_udp=sock->remote_simptcp;
	sock->remote_udp.sin_port=htons(SERVER_PORT);
	// on crée et envoie PDU SYN pour demander l'établissement de connexion
	send_pdu_flag(sock,SYN);
	printf("*** côté CLIENT : J'ai envoyé un SYN pour demander d'établir la connexion. *** \n");
	// on start le timer
	start_timer(sock, sock->timer_duration);
	simptcp_lprint_packet(sock->out_buffer);
	// on incrémente le numéro de séquence
	sock->next_seq_num++;
	// on passe le socket à l'état SYNSENT
	sock->socket_state = &(simptcp_entity.simptcp_socket_states->synsent);
	// si connect() n'a pas encore réussi (pas reçu ACK), on bloque le socket dans cet état
	while((sock->socket_state != &(simptcp_entity.simptcp_socket_states->established)) && (sock->socket_state != &(simptcp_entity.simptcp_socket_states->closed)));
	// si on est dans l'état ESTABLISHED (établissement de connection réussi), on renvoie succès
	if ((sock->socket_state == &(simptcp_entity.simptcp_socket_states->established)))
		return 0;
	// sinon renvoie échec
	return -1;
}

/*! \fn int closed_simptcp_socket_state_passive_open(struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "closed"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int closed_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	// le socket est du type listening server
	sock->socket_type = listening_server;
	// on passe à l'état listen
	sock->socket_state = &(simptcp_entity.simptcp_socket_states->listen);
	// on fixe la taille maximale des demandes de connexion
	sock->max_conn_req_backlog = n;
	// on loue le mémoire pour la file d'attente des demandes de connexion au maximal
	sock->new_conn_req = malloc(n*sizeof(struct simptcp_socket*));
	return 0;
}

/*! \fn int closed_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "closed"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int closed_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
	perror("ERROR : connexion non établie \n");
#endif
	return -1;
}

/*! \fn ssize_t closed_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "closed"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t closed_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
	perror("ERROR : connexion non établie \n");
#endif
	return -1;
}

/*! \fn ssize_t closed_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "closed"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t closed_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
	perror("ERROR : connexion non établie \n");
#endif
	return-1;
}

/*! \fn  int closed_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "closed"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int closed_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
	perror("ERROR : connexion non établie \n");
#endif
	return -1;
}

/*! \fn  int closed_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "closed"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int closed_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
	perror("ERROR : connexion non établie \n");
#endif
	return -1;
}

/*!
 * \fn void closed_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "closed"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void closed_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
	perror("ERROR : connexion non établie \n");
#endif
}

/*!
 * \fn void closed_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "closed"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void closed_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
	perror("ERROR : connexion non établie \n");
#endif
}

/*********************************************************
 * listen_state functions *
 *********************************************************/

/*! \fn int listen_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "listen"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */
int listen_simptcp_socket_state_active_open (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
	perror("ERROR : connexion déjà établie \n");
#endif
    return -1;
}

/**
 * called when application calls listen
 */
/*! \fn int listen_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "listen"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int listen_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on est déjà dans l'état LISTEN \n");
#endif
    return -1;
}

/**
 * called when application calls accept
 */
/*! \fn int listen_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "listen"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int listen_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	// tant qu'il n'y a pas de demande de connexion, on bloque
	while (sock->pending_conn_req == 0);
	// pour faciliter la mise à jour, on crée un pointeur ici
	struct simptcp_socket * last_received_socket = sock->new_conn_req[sock->pending_conn_req-1];
	// comme on a déjà alloué un mémoire dans l'état listen, il ne reste qu'à passer le pointeur dans le tableau de descripteurs
	int fd = 0 ;
	// on cherche parmi tous les open sockets possibles
	for (fd=0; fd< MAX_OPEN_SOCK; fd++)
	{
		// si on trouve le premier descripteur libre
		if (simptcp_entity.simptcp_socket_descriptors[fd] == NULL ){
			// on y passe le pointeur de dernier demande de connexion
			simptcp_entity.simptcp_socket_descriptors[fd]=last_received_socket;
			break;
		}
	}
	// on recupère le pointeur de ce "socket fils"
	struct simptcp_socket * sock_fils = simptcp_entity.simptcp_socket_descriptors[fd] ;
	// on met à jour des informations de socket fils que l'on n'a pas fait avant
	sock_fils->socket_state=&(simptcp_entity.simptcp_socket_states->synrcvd);
	sock_fils->next_seq_num = sock->next_seq_num ;
	sock_fils->socket_type = nonlistening_server;
	// on onvoie le PDU SYNACK par socket fils
	send_pdu_flag(sock_fils,SYNACK);
	// on vide les informations pour le client dans le socket père
	sock->remote_simptcp.sin_addr.s_addr = 0;
	sock->remote_simptcp.sin_port = 0 ;
	// on incrémente le numéro de séquence
	sock_fils->next_seq_num ++ ;
	printf("*** côté SERVER : J'ai envoyé un SYNACK pour répondre à client. *** \n");
	simptcp_lprint_packet(sock_fils->out_buffer);
	// on décremente le nombre de demandes non traitées
	sock->pending_conn_req --;
	while(sock_fils->socket_state != &(simptcp_entity.simptcp_socket_states->established));
	// on retourne le descripteur de socket fils
	return fd;
}

/**
 * called when application calls send
 */
/*! \fn ssize_t listen_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "listen"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t listen_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion non établie, impossible d'envoyer les données \n");
#endif
    return -1;
}

/**
 * called when application calls recv
 */
/*! \fn ssize_t listen_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "listen"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t listen_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion non établie, impossible de recevoir les données \n");
#endif
    return -1;
}

/**
 * called when application calls close
 */
/*! \fn  int listen_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "listen"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int listen_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion non établie \n");
#endif
   return -1;
}

/**
 * called when application calls shutdown
 */
/*! \fn  int listen_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "listen"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int listen_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how) // fermer dans un seul sens
{
#if __DEBUG__
	printf("function %s called\n", __func__);
	printf("ERROR : connexion non établie \n");
#endif
	return -1;
}

/**
 * called when library demultiplexed a packet to this particular socket
 */
/*!
 * \fn void listen_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "listen"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void listen_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	// si on reçoit un SYN
	if (simptcp_get_flags(buf)==SYN){
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
		lock_simptcp_socket(sock);
		printf("*** côté SERVER : J'ai reçu un SYN. *** \n");
		simptcp_lprint_packet(buf);
		// on loue un mémoire dans la file d'attente de socket père
		sock->new_conn_req[sock->pending_conn_req] = malloc(sizeof(struct simptcp_socket));
		// pour faciliter la manipulation du socket, on utilise un pointeur
		struct simptcp_socket * last_received_socket = sock->new_conn_req[sock->pending_conn_req];
		// on met à jour les informations de cette nouvelle demande de connexion pour qu'on puisse traiter dans accept()
		last_received_socket->remote_simptcp = sock->remote_simptcp;
		last_received_socket->remote_udp = sock->remote_udp;
		last_received_socket->local_simptcp = sock->local_simptcp;
		last_received_socket->next_ack_num = simptcp_get_seq_num(buf) + 1;
		// on incrémente le nombre de demande de connexion et on passe à accept()
		sock->pending_conn_req ++;
		unlock_simptcp_socket(sock);
	}
	else {
		perror("ERROR : on attend un SYN dans l'état LISTEN .");
		// on incrémente le nombre des PDUs inattendus
		sock->simptcp_in_errors_count ++ ;
	}
}

/**
 * called after a timeout has detected
 */
/*!
 * \fn void listen_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "listen"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void listen_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
	perror("ERROR : on n'a pas de timeout dans l'état listen \n");
#endif
}


/*********************************************************
 * synsent_state functions *
 *********************************************************/

/**
 * called when application calls connect
 */

/*! \fn int synsent_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "synsent"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */
int synsent_simptcp_socket_state_active_open (struct  simptcp_socket* sock,struct sockaddr* addr, socklen_t len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion existante \n");
#endif
    return -1;
}

/**
 * called when application calls listen
 */
/*! \fn int synsent_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "synsent"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int synsent_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on ne peut pas faire listen() en état SYNSENT \n");
#endif
    return -1;
}

/**
 * called when application calls accept
 */
/*! \fn int synsent_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "synsent"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int synsent_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on ne peut pas faire accept() en état SYNSENT \n");
#endif
    return -1;
}

/**
 * called when application calls send
 */
/*! \fn ssize_t synsent_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "synsent"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t synsent_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion non établie, impossible d'envoyer les données \n");
#endif
    return -1;
}

/**
 * called when application calls recv
 */
/*! \fn ssize_t synsent_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "synsent"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t synsent_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion non établie, impossible de recevoir les données \n");
#endif
    return -1;
}

/**
 * called when application calls close
 */
/*! \fn  int synsent_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "synsent"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int synsent_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on le fera avec shutdown \n");
#endif
    return -1;
}

/**
 * called when application calls shutdown
 */
/*! \fn  int synsent_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "synsent"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int synsent_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    int fd ;
    // on passe tous les sockets à l'état closed
    for (fd=0;fd< MAX_OPEN_SOCK;fd++) {
    	if ((simptcp_entity.simptcp_socket_descriptors[fd]) == sock) {
    		sock->socket_state = &(simptcp_entity.simptcp_socket_states->closed);
    		return 0;
    	}
    }
    // on libère le TCB
    free(sock);
    return 0;
}

/**
 * called when library demultiplexed a packet to this particular socket
 */
/*!
 * \fn void synsent_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "synsent"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void synsent_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
    // si on reçoit un SYNACK et son numéro d'ACK est bon
    if ((simptcp_get_flags(buf) == SYNACK) && (simptcp_get_ack_num(buf) == sock->next_seq_num)){
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
        printf("*** côté CLIENT : J'ai reçu un SYNACK avec bon numéro ACK. *** \n");
        simptcp_lprint_packet(buf);
        // on stop le timer
        stop_timer(sock);
        // on incrémente le numéro ACK de PDU à envoyer
        sock->next_ack_num = simptcp_get_seq_num(buf) + 1;
        //on renvoie un ACK
        printf("*** côté CLIENT : Suite à la réception de SYNACK, j'ai envoyé un ACK. *** \n");
        send_pdu_flag(sock,ACK);
        simptcp_lprint_packet(sock->out_buffer);
        // le socket passe à l'état ESTABLISHED
        sock->socket_state = &(simptcp_entity.simptcp_socket_states->established);
        // le socket est désormais prêt à recevoir les messages
        sock->socket_state_sender=wait_message;
    }
    else{
		// on incrémente le nombre des PDUs inattendus
		sock->simptcp_in_errors_count ++ ;
    }
}

/**
 * called after a timeout has detected
 */
/*!
 * \fn void synsent_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "synsent"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void synsent_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	// si le nombre de retransmission n'est pas dépassé
	if (sock->nbr_retransmit < MAX_RETRANSMIT){
		lock_simptcp_socket(sock);
		// on renvoie un SYN
		send_pdu_flag(sock,SYN);
		start_timer(sock,sock->timer_duration);
		sock->nbr_retransmit++;
		unlock_simptcp_socket(sock);
	}
	// sinon on passe à l'état closed
	else
		sock->socket_state=&(simptcp_entity.simptcp_socket_states->closed);
}

/*********************************************************
 * synrcvd_state functions *
 *********************************************************/

/**
 * called when application calls connect
 */

/*! \fn int synrcvd_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "synrcvd"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */

int synrcvd_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on ne peut pas faire connect() en état SYNRCVD \n");
#endif
    return -1;
}

/**
 * called when application calls listen
 */
/*! \fn int synrcvd_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "synrcvd"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int synrcvd_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on ne peut pas faire listen() en état SYNRCVD \n");
#endif
    return -1;
}

/**
 * called when application calls accept
 */
/*! \fn int synrcvd_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "synrcvd"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int synrcvd_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on ne peut pas faire accept() en état SYNRCVD \n");
#endif
    return -1;
}

/**
 * called when application calls send
 */
/*! \fn ssize_t synrcvd_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "synrcvd"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t synrcvd_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion non établie, impossible d'envoyer les données \n");
#endif
    return -1;
}

/**
 * called when application calls recv
 */
/*! \fn ssize_t synrcvd_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "synrcvd"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t synrcvd_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion non établie, impossible de recevoir les données \n");
#endif
    return -1;
}

/**
 * called when application calls close
 */
/*! \fn  int synrcvd_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "synrcvd"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int synrcvd_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion non établie \n");
#endif
    return -1;
}

/**
 * called when application calls shutdown
 */
/*! \fn  int synrcvd_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "synrcvd"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int synrcvd_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion non établie \n");
#endif
    return -1;
}

/**
 * called when library demultiplexed a packet to this particular socket
 */
/*!
 * \fn void synrcvd_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "synrcvd"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void synrcvd_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	// si on a reçu un ACK avec bon numéro ACK
	if ((simptcp_get_flags(buf)==ACK) && (simptcp_get_ack_num(buf) == sock->next_seq_num)){
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
		printf("*** côté SERVER : J'ai reçu un ACK avec bon numéro ACK en réponse de mon envoi SYNACK, connexion établie. *** \n");
		simptcp_lprint_packet(buf);
		// le socket passe à état ESTABLISHED, prêt à transfert de données
		sock->socket_state = &(simptcp_entity.simptcp_socket_states->established);
		// on attend les PDU data
		sock->socket_state_receiver = wait_packet;
	}
	else {
		// on incrémente le nombre des PDUs inattendus
		sock->simptcp_in_errors_count ++ ;
	}
}

/**
 * called after a timeout has detected
 */
/*!
 * \fn void synrcvd_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "synrcvd"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void synrcvd_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    if (sock->nbr_retransmit < MAX_RETRANSMIT){
  	  lock_simptcp_socket(sock);
  	  // on renvoie un SYNACK
  	  send_pdu_flag(sock,SYNACK);
  	  start_timer(sock,sock->timer_duration);
  	  sock->nbr_retransmit++;
  	  unlock_simptcp_socket(sock);
    }
    else
  	  sock->socket_state=&(simptcp_entity.simptcp_socket_states->closed);
}

/*********************************************************
 * established_state functions *
 *********************************************************/

/**
 * called when application calls connect
 */

/*! \fn int established_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "established"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */
int established_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion déjà établie \n");
#endif
    return -1;
}

/**
 * called when application calls listen
 */
/*! \fn int established_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "established"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int established_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on ne peut pas faire listen() en état ESTABLISHED \n");
#endif
    return -1;
}

/**
 * called when application calls accept
 */
/*! \fn int established_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "established"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int established_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on ne peut pas faire accept() en état ESTABLISHED \n");
#endif
    return -1;
}

/**
 * called when application calls send
 */
/*! \fn ssize_t established_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "established"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t established_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	ssize_t message_size = -1 ;
	// seuls côté client peut appeler send()
	if ((sock->socket_type == client) && (sock->socket_state_sender == wait_message)){
		// création et envoie du paquet
		message_size = send_pdu_data(sock, buf, n);
		printf("*** côté CLIENT : J'ai envoyé un PDU data. *** \n");
		simptcp_lprint_packet(sock->out_buffer);
		// on incrémente le numéro de séquence
		simptcp_set_seq_num(sock->out_buffer,simptcp_get_seq_num(sock->out_buffer)+1);
		// on attend l'aquittement de ce PDU donnée
		sock->socket_state_sender=wait_ack;
		// on lance le timer
		start_timer(sock,sock->timer_duration);
		// si on n'a pas reçu l'ACK, on attend
		while(sock->socket_state_sender == wait_ack);
	}
	return message_size;
}

/**
 * called when application calls recv
 */
/*! \fn ssize_t established_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "established"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t established_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	ssize_t message_size = -1 ;
	// si on a rien dans le buffer, on attend
	while (sock->in_len == 0);
	// si on reçoit un PDU
	printf("*** côté SERVER : J'ai reçu un PDU data. *** \n");
	simptcp_lprint_packet(sock->in_buffer);
	// on incrémente le numéro ACK
	sock->next_ack_num++;
	// on récupère le contenu
	message_size = simptcp_extract_data(sock->in_buffer,buf);
	sock->next_ack_num = simptcp_get_seq_num(sock->in_buffer) + 1 ;
	// on envoie l'aquittement en réponse de PDU data
	send_pdu_flag(sock,ACK);
	printf("*** côté SERVER : J'ai envoyé un ACK en réponse de PDU data de client. *** \n");
	simptcp_lprint_packet(sock->out_buffer);
	// on lance le timer
	start_timer(sock,sock->timer_duration);
	// on vide le buffer
	bzero(sock->in_buffer,sizeof(sock->in_buffer));
	// on renvoie la taille de message reçu
	return message_size;
}

/**
 * called when application calls close
 */
/*! \fn  int established_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "established"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int established_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on le fera dans shutdown \n");
#endif
    return -1;
}

/**
 * called when application calls shutdown
 */
/*! \fn  int established_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "established"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int established_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
	lock_simptcp_socket(sock);
	// pour la fermeture de connexion, on reinitialise numéro ACK à 0
	sock->next_ack_num = 0;
	// on envoie un FIN pour demander la fermeture de connexion (active close)
	send_pdu_flag(sock, FIN);
	sock->next_seq_num ++ ;
	printf("*** J'ai envoyé un FIN pour demander de fermer la connexion. *** \n");
	simptcp_lprint_packet(sock->out_buffer);
	// on start le timer
	start_timer(sock,sock->timer_duration);
	// on passe à l'état FINWAIT1
	sock->socket_state = &(simptcp_entity.simptcp_socket_states->finwait1);
	unlock_simptcp_socket(sock);
	// on bloque le socket jusqu'à c'est fermé
	while(sock->socket_state != &(simptcp_entity.simptcp_socket_states->closed));
    return 0;
}

/**
 * called when library demultiplexed a packet to this particular socket
 */
/*!
 * \fn void established_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "established"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void established_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
	printf("function %s called\n", __func__);
#endif
	// si on a reçu un PDU data (côté serveur)
	if(simptcp_get_flags(buf)==0 && sock->socket_type == nonlistening_server){
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
		lock_simptcp_socket(sock);
		stop_timer(sock);
		// on copie les informations du PDU reçu
		memcpy(sock->in_buffer,buf,len);
		// on traite ce PDU dans established_simptcp_socket_state_recv()
		sock->in_len=len;
		// on attend d'autres PDU data
		sock->socket_state_receiver=wait_packet;
		unlock_simptcp_socket(sock);
	}
	// si on a reçu un ACK (côté client) avec bon numéro ACK
	if (simptcp_get_flags(buf)==ACK && sock->socket_type == client){
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
		if(simptcp_get_seq_num(sock->out_buffer) == simptcp_get_ack_num(buf)){
			lock_simptcp_socket(sock);
			// on arrête le timer
			stop_timer(sock);
			printf("*** côté CLIENT : J'ai reçu ACK en réponse de PDU data avec bon numéro ACK. *** \n");
			simptcp_lprint_packet(buf);
			// on peut envoyer d'autres messages
			sock->socket_state_sender=wait_message;
			unlock_simptcp_socket(sock);
		}
		else{
			perror("ERROR : numéro ACK incorrect");
		}
	}
	// si on a reçu un PDU flag FIN pour demander fermer la connexion
	if(simptcp_get_flags(buf)==FIN){
		printf("*** J'ai reçu un FIN demandant la fermeture de connexion. *** \n");
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
		stop_timer(sock);
		// si on n'a pas encore envoyé un FIN de ce côté : cas "non-simultaneuos close"
		if(simptcp_get_flags(sock->out_buffer) != FIN){
			lock_simptcp_socket(sock);
			// on envoie un ACK pour répondre à ce FIN
			send_pdu_flag(sock,ACK);
			printf("*** J'ai envoyé un ACK en réponse de FIN. *** \n");
			simptcp_lprint_packet(sock->out_buffer);
			// on start le timer
			start_timer(sock,sock->timer_duration);
			unlock_simptcp_socket(sock);
			// on passe à l'état CLOSEWAIT
			sock->socket_state=&(simptcp_entity.simptcp_socket_states->closewait);
		}
	}
	else {
		// on incrémente le nombre des PDUs inattendus
		sock->simptcp_in_errors_count ++ ;
	}
}

/**
 * called after a timeout has detected
 */
/*!
 * \fn void established_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "established"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void established_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    if (sock->nbr_retransmit < MAX_RETRANSMIT){
  	  lock_simptcp_socket(sock);
  	  // on renvoie ce qui est dans le buffer
  	  libc_sendto(simptcp_entity.udp_fd,sock->out_buffer,sock->out_len,0,(struct sockaddr*)(&(sock->remote_udp)),sizeof(sock->remote_udp));
  	  start_timer(sock,sock->timer_duration);
  	  sock->nbr_retransmit++;
  	  unlock_simptcp_socket(sock);
    }
    else
  	  sock->socket_state=&(simptcp_entity.simptcp_socket_states->closed);
}

/*********************************************************
 * closewait_state functions *
 *********************************************************/

/**
 * called when application calls connect
 */

/*! \fn int closewait_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "closewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */
int closewait_simptcp_socket_state_active_open (struct  simptcp_socket* sock,  struct sockaddr* addr, socklen_t len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion existante \n");
#endif
    return -1;
}

/**
 * called when application calls listen
 */
/*! \fn int closewait_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "closewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int closewait_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire listen() dans l'état closewait \n");
#endif
    return -1;
}

/**
 * called when application calls accept
 */
/*! \fn int closewait_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "closewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int closewait_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire accept() dans l'état closewait \n");
#endif
    return -1;
}

/**
 * called when application calls send
 */
/*! \fn ssize_t closewait_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "closewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t closewait_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire send() dans l'état closewait \n");
#endif
    return -1;
}

/**
 * called when application calls recv
 */
/*! \fn ssize_t closewait_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "closewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t closewait_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls close
 */
/*! \fn  int closewait_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "closewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int closewait_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : on le fera dans shutdown \n");
#endif
    return -1;
}

/**
 * called when application calls shutdown
 */
/*! \fn  int closewait_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "closewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int closewait_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    lock_simptcp_socket(sock);
    // on envoie FIN pour fermer la connexion dans l'autre sens
    send_pdu_flag(sock,FIN);
	printf("*** J'ai envoyé un FIN pour fermer la connexion dans l'autre sens. *** \n");
	simptcp_lprint_packet(sock->out_buffer);
    start_timer(sock,sock->timer_duration);
    // on passe à l'état LASTACK
    sock->socket_state=&(simptcp_entity.simptcp_socket_states->lastack);
    unlock_simptcp_socket(sock);
    // on bloque le socket dans cet état avant qu'il soit fermé
	while(sock->socket_state != &(simptcp_entity.simptcp_socket_states->closed));
    return 0;
}

/**
 * called when library demultiplexed a packet to this particular socket
 */
/*!
 * \fn void closewait_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "closewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void closewait_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    // normalement c'est l'application qui va faire l'appel de fermeture de connexion dans l'état CLOSEWAIT
    // ici nous l'appelons nous-même
    closewait_simptcp_socket_state_shutdown(sock,2);
}

/**
 * called after a timeout has detected
 */
/*!
 * \fn void closewait_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "closewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void closewait_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    if (sock->nbr_retransmit < MAX_RETRANSMIT){
  	  lock_simptcp_socket(sock);
  	  stop_timer(sock);
  	  // on renvoie un ACK
  	  send_pdu_flag(sock,ACK);
  	  start_timer(sock,sock->timer_duration);
  	  sock->nbr_retransmit++;
  	  unlock_simptcp_socket(sock);
    }
    else
  	  sock->socket_state=&(simptcp_entity.simptcp_socket_states->closed);
}

/*********************************************************
 * finwait1_state functions *
 *********************************************************/

/**
 * called when application calls connect
 */

/*! \fn int finwait1_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "finwait1"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */
int finwait1_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion existante \n");
#endif
    return -1;
}

/**
 * called when application calls listen
 */
/*! \fn int finwait1_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "finwait1"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int finwait1_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire listen() en état finwait1 \n");
#endif
    return -1;
}

/**
 * called when application calls accept
 */
/*! \fn int finwait1_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "finwait1"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int finwait1_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire accept() en état finwait1 \n");
#endif
    return -1;
}

/**
 * called when application calls send
 */
/*! \fn ssize_t finwait1_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "finwait1"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t finwait1_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls recv
 */
/*! \fn ssize_t finwait1_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "finwait1"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t finwait1_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    return -1;
}

/**
 * called when application calls close
 */
/*! \fn  int finwait1_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "finwait1"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int finwait1_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    return -1;
}

/**
 * called when application calls shutdown
 */
/*! \fn  int finwait1_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "finwait1"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int finwait1_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    return -1;
}

/**
 * called when library demultiplexed a packet to this particular socket
 */
/*!
 * \fn void finwait1_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "finwait1"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void finwait1_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    // si on est dans le cas "non-simultaneous CLOSE"
    if(simptcp_get_flags(buf)==ACK){
    	stop_timer(sock);
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
		printf("*** J'ai reçu un ACK en réponse de FIN. *** \n");
		simptcp_lprint_packet(buf);
    	// on passe à l'état FINWAIT2
    	sock->socket_state=&(simptcp_entity.simptcp_socket_states->finwait2);
    }
    // si on est dans le cas "simultaneous CLOSE"
    if(simptcp_get_flags(buf)==FIN){
    	lock_simptcp_socket(sock);
    	stop_timer(sock);
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
		printf("*** J'ai reçu un FIN pour fermer la connexion. *** \n");
		simptcp_lprint_packet(buf);
		sock->next_ack_num = simptcp_get_seq_num(buf)+1;
		// on envoie en ACK pour répondre le FIN reçu
    	send_pdu_flag(sock,ACK);
		printf("*** J'ai envoyé un ACK pour répondre à FIN. *** \n");
		simptcp_lprint_packet(sock->out_buffer);
    	start_timer(sock,sock->timer_duration);
    	unlock_simptcp_socket(sock);
    	// on passe à l'état CLOSING
    	sock->socket_state=&(simptcp_entity.simptcp_socket_states->closing);
    }
    else {
		// on incrémente le nombre des PDUs inattendus
		sock->simptcp_in_errors_count ++ ;
    }
}

/**
 * called after a timeout has detected
 */
/*!
 * \fn void finwait1_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "finwait1"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void finwait1_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    if (sock->nbr_retransmit < MAX_RETRANSMIT){
  	  lock_simptcp_socket(sock);
  	  // on renvoie ce qui est dans le buffer
  	  libc_sendto(simptcp_entity.udp_fd,sock->out_buffer,sock->out_len,0,(struct sockaddr*)(&(sock->remote_udp)),sizeof(sock->remote_udp));
  	  start_timer(sock,sock->timer_duration);
  	  sock->nbr_retransmit++;
  	  unlock_simptcp_socket(sock);
    }
    else
  	  sock->socket_state=&(simptcp_entity.simptcp_socket_states->closed);
}

/*********************************************************
 * finwait2_state functions *
 *********************************************************/

/**
 * called when application calls connect
 */

/*! \fn int finwait2_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "fainwait2"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */
int finwait2_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion existante \n");
#endif
    return -1;
}

/**
 * called when application calls listen
 */
/*! \fn int finwait2_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "finwait2"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int finwait2_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire listen() en état finwait2 \n");
#endif
    return -1;
}

/**
 * called when application calls accept
 */
/*! \fn int finwait2_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "finwait2"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int finwait2_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire accept() en état finwait2 \n");
#endif
    return -1;
}

/**
 * called when application calls send
 */
/*! \fn ssize_t finwait2_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "finwait2"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t finwait2_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls recv
 */
/*! \fn ssize_t finwait2_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "finwait2"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t finwait2_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    return -1;
}

/**
 * called when application calls close
 */
/*! \fn  int finwait2_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "finwait2"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int finwait2_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    return -1;
}

/**
 * called when application calls shutdown
 */
/*! \fn  int finwait2_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "finwait2"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int finwait2_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    return -1;
}

/**
 * called when library demultiplexed a packet to this particular socket
 */
/*!
 * \fn void finwait2_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "finwait2"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void finwait2_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    // si on a reçu un FIN de l'autre côté
    if(simptcp_get_flags(buf)==FIN){
    	stop_timer(sock);
		printf("*** J'ai reçu un FIN de l'autre côté. *** \n");
		simptcp_lprint_packet(buf);
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
    	// on envoie ACK et passe à l'état TIMEWAIT
		printf("*** J'ai envoyé un ACK pour répondre à ce dernier FIN. *** \n");
    	send_pdu_flag(sock,ACK);
		simptcp_lprint_packet(sock->out_buffer);
    	start_timer(sock,sock->timer_duration);
    	// on passe à l'état TIMEWAIT
    	sock->socket_state=&(simptcp_entity.simptcp_socket_states->timewait);
    }
    else {
		// on incrémente le nombre des PDUs inattendus
		sock->simptcp_in_errors_count ++ ;
    }
}

/**
 * called after a timeout has detected
 */
/*!
 * \fn void finwait2_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "finwait2"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void finwait2_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    if (sock->nbr_retransmit < MAX_RETRANSMIT){
  	  lock_simptcp_socket(sock);
  	  // on renvoie un ACK
  	  send_pdu_flag(sock,ACK);
  	  start_timer(sock,sock->timer_duration);
  	  sock->nbr_retransmit++;
  	  unlock_simptcp_socket(sock);
    }
    else
  	  sock->socket_state=&(simptcp_entity.simptcp_socket_states->closed);
}

/*********************************************************
 * closing_state functions *
 *********************************************************/

/**
 * called when application calls connect
 */

/*! \fn int closing_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "closing"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */
int closing_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion existante \n");
#endif
    return -1;
}

/**
 * called when application calls listen
 */
/*! \fn int closing_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "closing"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int closing_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire listen() en état CLOSING \n");
#endif
    return -1;
}

/**
 * called when application calls accept
 */
/*! \fn int closing_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "closing"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int closing_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire accept() en état CLOSING \n");
#endif
    return -1;
}

/**
 * called when application calls send
 */
/*! \fn ssize_t closing_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "closing"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t closing_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls recv
 */
/*! \fn ssize_t closing_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "closing"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t closing_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls close
 */
/*! \fn  int closing_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "closing"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int closing_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls shutdown
 */
/*! \fn  int closing_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "closing"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int closing_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when library demultiplexed a packet to this particular socket
 */
/*!
 * \fn void closing_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "closing"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void closing_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    // si on a reçu le dernier ACK
    if((simptcp_get_flags(buf)==ACK)){
    	stop_timer(sock);
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
		printf("*** J'ai reçu un ACK en réponse de dernier FIN. *** \n");
		simptcp_lprint_packet(buf);
		// on start le timer avec un temps de 2 fois le temps de MSL
		start_timer(sock,2*MSL_TIME);
		// on passe à l'état timewait
    	sock->socket_state=&(simptcp_entity.simptcp_socket_states->timewait);
    }
    else {
		// on incrémente le nombre des PDUs inattendus
		sock->simptcp_in_errors_count ++ ;
    }
}

/**
 * called after a timeout has detected
 */
/*!
 * \fn void closing_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "closing"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void closing_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    // timeout dans l'état CLOSING quand le timer de FIN expiré : FIN non reçu par l'autre côté
    if (sock->nbr_retransmit < MAX_RETRANSMIT){
  	  lock_simptcp_socket(sock);
  	  // on renvoie un FIN
  	  send_pdu_flag(sock,FIN);
  	  start_timer(sock,sock->timer_duration);
  	  sock->nbr_retransmit++;
  	  unlock_simptcp_socket(sock);
    }
    else
  	  sock->socket_state=&(simptcp_entity.simptcp_socket_states->closed);
}

/*********************************************************
 * lastack_state functions *
 *********************************************************/

/**
 * called when application calls connect
 */

/*! \fn int lastack_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "lastack"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */
int lastack_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion existante \n");
#endif
    return -1;
}

/**
 * called when application calls listen
 */
/*! \fn int lastack_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "lastack"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int lastack_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire listen() en état LASTACK \n");
#endif
    return -1;
}

/**
 * called when application calls accept
 */
/*! \fn int lastack_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "lastack"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int lastack_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire accept() en état LASTACK \n");
#endif
    return -1;
}

/**
 * called when application calls send
 */
/*! \fn ssize_t lastack_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "lastack"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t lastack_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls recv
 */
/*! \fn ssize_t lastack_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "lastack"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t lastack_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls close
 */
/*! \fn  int lastack_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "lastack"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int lastack_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls shutdown
 */
/*! \fn  int lastack_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "lastack"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int lastack_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when library demultiplexed a packet to this particular socket
 */
/*!
 * \fn void lastack_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "lastack"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void lastack_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    // si on a reçu le dernier ACK
    if(simptcp_get_flags(buf)==ACK){
    	printf("*** J'ai reçu le dernier ACK en état LASTACK *** \n");
    	simptcp_lprint_packet(sock->out_buffer);
    	// on stop le timer
    	stop_timer(sock);
		// on incrémente le nombre des PDUs reçus
		sock->simptcp_receive_count++;
        // on passe en état CLOSED
    	sock->socket_state=&(simptcp_entity.simptcp_socket_states->closed);
    	// on annule le pointeur
        sock=NULL;
    }
    else {
		// on incrémente le nombre des PDUs inattendus
		sock->simptcp_in_errors_count ++ ;
    }
}

/**
 * called after a timeout has detected
 */
/*!
 * \fn void lastack_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "lastack"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void lastack_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
}

/*********************************************************
 * timewait_state functions *
 *********************************************************/

/**
 * called when application calls connect
 */


/*! \fn int timewait_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
 * \brief lancee lorsque l'application lance l'appel "connect" alors que le socket simpTCP est dans l'etat "timewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param addr adresse de niveau transport du socket simpTCP destination
 * \param len taille en octets de l'adresse de niveau transport du socket destination
 * \return  0 si succes, -1 si erreur
 */
int timewait_simptcp_socket_state_active_open (struct  simptcp_socket* sock, struct sockaddr* addr, socklen_t len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion existante \n");
#endif
    return -1;
}

/**
 * called when application calls listen
 */
/*! \fn int timewait_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
 * \brief lancee lorsque l'application lance l'appel "listen" alors que le socket simpTCP est dans l'etat "timewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param n  nbre max de demandes de connexion en attente (taille de la file des demandes de connexion)
 * \return  0 si succes, -1 si erreur
 */
int timewait_simptcp_socket_state_passive_open (struct simptcp_socket* sock, int n)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire listen() en état LASTACK \n");
#endif
    return -1;
}

/**
 * called when application calls accept
 */
/*! \fn int timewait_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
 * \brief lancee lorsque l'application lance l'appel "accept" alors que le socket simpTCP est dans l'etat "timewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] addr pointeur sur l'adresse du socket distant de la connexion qui vient d'etre acceptee
 * \param len taille en octet de l'adresse du socket distant
 * \return 0 si succes, -1 si erreur/echec
 */
int timewait_simptcp_socket_state_accept (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : impossible de faire accept() en état LASTACK \n");
#endif
    return -1;
}

/**
 * called when application calls send
 */
/*! \fn ssize_t timewait_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "send" alors que le socket simpTCP est dans l'etat "timewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf  pointeur sur le message a transmettre
 * \param n Taille du buffer (contenant le message) en octets pointe par buf
 * \param flags options
 * \return taille en octet du message envoye ; -1 sinon
 */
ssize_t timewait_simptcp_socket_state_send (struct simptcp_socket* sock, const void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls recv
 */
/*! \fn ssize_t timewait_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
 * \brief lancee lorsque l'application lance l'appel "recv" alors que le socket simpTCP est dans l'etat "timewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param [out] buf  pointeur sur le message recu
 * \param n Taille max du buffer de reception pointe par buf
 * \param flags options
 * \return  taille en octet du message recu, -1 si echec
 */
ssize_t timewait_simptcp_socket_state_recv (struct simptcp_socket* sock, void *buf, size_t n, int flags)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls close
 */
/*! \fn  int timewait_simptcp_socket_state_close (struct simptcp_socket* sock)
 * \brief lancee lorsque l'application lance l'appel "close" alors que le socket simpTCP est dans l'etat "timewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \return  0 si succes, -1 si erreur
 */
int timewait_simptcp_socket_state_close (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when application calls shutdown
 */
/*! \fn  int timewait_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
 * \brief lancee lorsque l'application lance l'appel "shutdown" alors que le socket simpTCP est dans l'etat "timewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param how sens de fermeture de le connexion (en emisison, reception, emission et reception)
 * \return  0 si succes, -1 si erreur
 */
int timewait_simptcp_socket_state_shutdown (struct simptcp_socket* sock, int how)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
    perror("ERROR : connexion en cours de fermeture \n");
#endif
    return -1;
}

/**
 * called when library demultiplexed a packet to this particular socket
 */
/*!
 * \fn void timewait_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
 * \brief lancee lorsque l'entite protocolaire demultiplexe un PDU simpTCP pour le socket simpTCP alors qu'il est dans l'etat "timewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 * \param buf pointeur sur le PDU simpTCP recu
 * \param len taille en octets du PDU recu
 */
void timewait_simptcp_socket_state_process_simptcp_pdu (struct simptcp_socket* sock, void* buf, int len)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    // on attend 2 fois le temps de MSL avant d'entrer dans le timeout, et finalement fermer la connexion
    start_timer(sock,2*MSL_TIME);
}

/**
 * called after a timeout has detected
 */
/*!
 * \fn void timewait_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
 * \brief lancee lors d'un timeout du timer du socket simpTCP alors qu'il est dans l'etat "timewait"
 * \param sock pointeur sur les variables d'etat (#simptcp_socket) du socket simpTCP
 */
void timewait_simptcp_socket_state_handle_timeout (struct simptcp_socket* sock)
{
#if __DEBUG__
    printf("function %s called\n", __func__);
#endif
    printf("*** Expiration of double MSL time, connexion closed *** \n");
    sock->socket_state = &(simptcp_entity.simptcp_socket_states->closed);
    // on libère le mémore du socket
    free(sock);
}
