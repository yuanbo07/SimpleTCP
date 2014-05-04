/*! \file simptcp_lib.h
*  \brief Defines the state variables needed by a simptcp protocol entity
*  \author{DGEI-INSAT 2010-2011}
*/

#ifndef _SIMPTCP_LIB_H_
#define _SIMPTCP_LIB_H_

#include <stdint.h>             /* for INT32_MAX */
#include <pthread.h>            /* for pthread_mutex_t, pthread_cond_t */
#include <sys/socket.h>
#include <pthread.h>


#define ETH_MTU 1500 /* Ethernet Max transmit Unit */

#define SIMPTCP_SOCKET_MAX_BUFFER_SIZE (ETH_MTU-16-20-8) /* SIMPTCP_MAX_SIZE to avoid IP 
							    fragmentation assuming no IP options */
#define MAX_RETRANSMIT 255  /* Maximum number of retransmissions */



/*!
 * \var socket_types
 * \brief simptcp socket types; socket of type listening_server waits for connection requests 
*/
enum{
  unknown=0,
  client=1,
  nonlistening_server=2,
  listening_server=3
} socket_types;

/*!
 * \var simptcp_states 
 * \brief list of default SimpTCP socket states (taken from TCP finite state machine [RFC 793]). 
 * It Could be updated at will but in conjunction with data structure "struct simptcp_socket_states_funcs"
 */
enum {
  closed_state=1,
  liste_staten=2,
  syn_sen_statet=3,
  syn_rcvd_state=4,
  established_state=5,
  fin_wait_1_state=6,
  fin_wait_2_state=7,
  closing_state=8,
  close_wait_state=9,
  last_ack_state=10,
  time_wait_state=11
} simptcp_states;

/*!
 * \enum data_transfert_states
 * \brief  list of socket states during the data tranfer phase (see TD)
 */
enum data_transfert_states{
  /* sender side */
  wait_message=1, 
  wait_ack=2,
  /* receiver side */
  wait_packet=3
};

/*! 
 * \struct simptcp_socket 
 * \brief structure regroupant toutes les variables d'etat specifiques a un socket simpTCP (<a href="./StructureDonnees.jpg">voir diagramme des données</a>)
 *
*/
struct simptcp_socket { /* SimpTCP Protocol Control Block */

  short  socket_type; /*!< SimpTCP socket type (#socket_types): either client,
					   listening or server socket */ 
  struct simptcp_socket * * new_conn_req; /*!<  remote SAPs of backlogged 
					      connection requests received on a listening socket - 
					      used by sys call accept to set up new connections */
  int pending_conn_req; /*!< number of pending syn requests. 
						 For simplicity, assume 1 */

  int max_conn_req_backlog; /*!< this is fixed with sys call listen */

  /* simptcp SAP Address */
  struct sockaddr_in local_simptcp; /*!< local simptcp SAP address */  
  struct sockaddr_in remote_simptcp; /*!< remote simptcp SAP address */ 

  /*! remote UDP SAP address */
  struct sockaddr_in remote_udp; 

  /* current socket state - related to connection management  */
  struct simptcp_socket_state_funcs * socket_state; /*!< socket state + functions 
						 that can be called at the current state  */
 
  /* Related to data transmissions */
  short socket_state_sender; /*!< sender side FSM describing 
							  the data transfer phase (started during TD) */
  unsigned int next_seq_num;  /*!< Next sequence number */
  char out_buffer[SIMPTCP_SOCKET_MAX_BUFFER_SIZE]; /*!< SimpTCP socket Transmit
						      buffer used to store 
						      outgoing SimpTCP PDUs */
  unsigned int out_len; /*!< instantaneous out_buffer occupation */
  char nbr_retransmit; /*!< number of times first unacked message 
			  retransmitted (limited to 255) */

  /* timer */
  int timer_duration; /*!< expressed in ms, normally derived from estimated_rtt  */
  struct timeval timeout; /*!< Expected timeout for last unacked packet */

  /* when receiving  Data */   
  short socket_state_receiver; /*!< receiver side FSM describing 
				the data transfer phase */
  unsigned int next_ack_num;  /*!< Next ack number */
  char in_buffer[SIMPTCP_SOCKET_MAX_BUFFER_SIZE]; /*!< SimpTCP socket Receive 
						     buffer used to store 
						     ingoing SimpTCP PDUs */
  unsigned int in_len;/*!< instantaneous in_buffer occupation */

  /* MIB Statistics */
  unsigned long simptcp_send_count; /* number of sent SimpTCP PDU */
  unsigned long simptcp_receive_count; /* number of sent SimpTCP PDU */
  unsigned long simptcp_in_errors_count; /* number of unexpected received SimpTCP PDU */
  unsigned long simptcp_retransmit_count; /* number of SimpTCP PDU retransmissions */
  

  /* optional fields */
  /* related to the sending  window used with GoBack-N mechanism */
  unsigned int sending_window_size;
  unsigned int sending_window_base; /* sequence number of first unacked 
				       simptcp packet */

  /* related to the receiving  window used with GoBack-N mechanism */
  unsigned int receiving_window_size;
  unsigned int receiving_window_base; /* sequence number of last in
					 sequence received packet */

  /* related to RTT estimation */
  double rtt_estimate;
  double last_rtt; /* last RTT */

  /*! mutex to control the write-access to this block 
   contening processes : primitives called by the 
   application vs simptcp protocol entity */
  pthread_mutex_t mutex_socket;
};

/*
 * The following function typedefs are for pointers to functions
 * residing in each simpTCP_socket_state_func. Each 
 * simpTCP_socket_state_func struct is a static instance of a 
 * behaviour which a socket might be at a point
 * in time. The simpTCP_socket_states_func struct wraps all these up and a
 * single static instance of it is kept so that the correct state can
 * be referenced to be pointed to be a socket whenever the socket's 
 *  state changes
 */


/**
 * function pointer whose function gets called when application calls
 * connect
 */

typedef int (simptcp_socket_state_active_open)
(struct  simptcp_socket * sock, struct sockaddr * addr, socklen_t len);


/**
 * function pointer whose function gets called when application calls
 * listen
 */
typedef int (simptcp_socket_state_passive_open)
     (struct simptcp_socket* sock, int n);

/**
 * function pointer whose function gets called when application calls
 * accept
 */
typedef int (simptcp_socket_state_accept)
     (struct simptcp_socket* sock, struct sockaddr* addr, socklen_t* len);

/**
 * function pointer whose function gets called when application calls
 * send
 */
typedef ssize_t (simptcp_socket_state_send)
     (struct simptcp_socket* sock, const void *buf, size_t n, int flags);

/**
 * function pointer whose function gets called when application calls
 * recv
 */
typedef ssize_t (simptcp_socket_state_recv)
     (struct simptcp_socket* sock, void *buf, size_t n, int flags);

/**
 * function pointer whose function gets called when application calls
 * close
 */
typedef int (simptcp_socket_state_close)
     (struct simptcp_socket* sock);

/**
 * function pointer whose function gets called when application calls
 * shutdown
 */
typedef int (simptcp_socket_state_shutdown)
     (struct simptcp_socket* sock, int how);
/**
 * function pointer whose function gets called when simptcp_entity
 * demultiplexes a packet to this particular socket
 */
typedef void (simptcp_socket_state_process_pdu)
    (struct simptcp_socket* sock, void* buf, int len);

/**
 * function pointer whose function gets called after a timeout
 */
typedef void (simptcp_socket_state_handle_timeout)
    (struct simptcp_socket* sock);

/**
 * \brief It gathers pointers to functions that will be used to handle different
 * situations (packet arrival, timeout, transmission request) for a particular
 * simpTCP socket state(<a href="./StructureDonnees.jpg">(voir diagramme des données)</a>). For example,  active_open is called 
 * when the applicaton calls connect
 */
typedef struct simptcp_socket_state_funcs {
    simptcp_socket_state_active_open    *active_open;
    simptcp_socket_state_passive_open   *passive_open;
    simptcp_socket_state_accept         *accept;
    simptcp_socket_state_send           *send;
    simptcp_socket_state_recv           *recv;
    simptcp_socket_state_close          *close;
    simptcp_socket_state_shutdown          *shutdown;
    simptcp_socket_state_process_pdu *process_simptcp_pdu;
    simptcp_socket_state_handle_timeout *handle_timeout;
} simptcp_socket_state_funcs;

/**
 * \brief It gathers pointers to functions that will be used to handle different
 * situations (packet arrival, timeout, transmission request) for all
 * simpTCP socket states (<a href="./StructureDonnees.jpg">voir diagramme des données</a>). 
 */
typedef struct simptcp_socket_states_funcs {
    simptcp_socket_state_funcs closed;
    simptcp_socket_state_funcs listen;
    simptcp_socket_state_funcs synsent;
    simptcp_socket_state_funcs synrcvd;
    simptcp_socket_state_funcs established;
    simptcp_socket_state_funcs closewait;
    simptcp_socket_state_funcs finwait1;
    simptcp_socket_state_funcs finwait2;
    simptcp_socket_state_funcs closing;
    simptcp_socket_state_funcs lastack;
    simptcp_socket_state_funcs timewait;
} simptcp_socket_states_funcs;



/* Fill a struct simptcp_socket with default values */
int create_simptcp_socket();
char * simptcp_socket_state_get_str(simptcp_socket_state_funcs *state);
inline int lock_simptcp_socket(struct simptcp_socket *sock);
inline int unlock_simptcp_socket(struct simptcp_socket *sock);
int is_timeout(struct simptcp_socket * sock);
int has_active_timer(struct simptcp_socket * sock);


#endif // _SIMPTCP_LIB_H_

/* vim: set expandtab ts=4 sw=4 tw=80: */
