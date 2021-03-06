#include "constants.h"
#include "network_ra.h"
#include "enclave_u.h"

extern char* KPS_CERT_LOCATION;
extern char* KPS_CERT_KEYS_LOCATION;
extern bool isKpsProcess;

extern char* DST_HOST_CERT_LOCATION;
extern char* DST_HOST_CERT_KEYS_LOCATION;

void* handle_socket_network_request(void* arg);
void* handle_socket_network_request_attestation(void* arg);
void* handle_socket_network_kps_generic_requests(void* arg);
char* network_socket_sender(char* request, int request_size, char* ipAddress, int ipAddressSize, int port=DEFAULT_PORT);
void printPayload(char* payload, int size);
