/*
 * Copyright (C) 2011-2019 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include "kps.h"
#include "enclave_u.h"
#include "sgx_utils/sgx_utils.h"
#include "sample_libcrypto.h"

#include "ecp.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <string.h>
#include "ias_ra.h"
#include "app.h"

#ifndef SAFE_FREE
#define SAFE_FREE(ptr) {if (NULL != (ptr)) {free(ptr); (ptr) = NULL;}}
#endif

using namespace std;

//TODO: Create a wrapper for the KPS, have it have the API, and basically
// when you call establishInitialConnection with KPS, you get a connectoin ID
// and the KPS is running on a thread, and whenever it gets a message with the correct
// connection ID, it calls the correct methods in thsi file and returns the results,
// if there are mulitple messages sent by mutiple connections, put them in a thred pool

//NOTE: in certain parts of this file, SP refers to the Ping machine

map<tuple<string, string>, string> capabilityKeyAccessDictionary;
map<tuple<string, string>, string> capabilityKeyDictionary;

map<string, list<string>> MachineTypeToValidIPAddresses;

sgx_enclave_id_t kps_enclave_eid;

char* KPS_IP_ADDRESS;
int KPS_PORT_GENERIC;
int KPS_PORT_ATTESTATION;

//This represents the payload the KPS receives from the enclave after a succesful attestation
char secure_message[SIZE_OF_MESSAGE]; 


//KPS Setup functions *******************
void initKPS() {
    kps_enclave_eid = 0;
    string token = "enclavekps.token";
    //Need an enclave for the KPS to be able to use sgx librarie for key generation
    if (initialize_enclave(&kps_enclave_eid, token, "enclave.signed.so") < 0) {
        ocall_print("Fail to initialize enclave.");
    }    

}

void addRegisteredMachineToKPS(char* machineName, char* machineAddress) {
    list<string> lst;
    lst.push_back(string(machineAddress));
    MachineTypeToValidIPAddresses[string(machineName)] = lst;
}

//*******************


//KPS API Functions*******************

//Returns IP address of a valid distributed host that can spawn SM of requested type
char* queryIPAddressForMachineType(char* machineTypeRequested, int& responseSize) {
    char* response;
    if (MachineTypeToValidIPAddresses.count(string(machineTypeRequested)) > 0) {
        response = createStringLiteralMalloced((char*)MachineTypeToValidIPAddresses[string(machineTypeRequested)].front().c_str());
    } else {
        ocall_print("ERROR: KPS queried for invalid machine type!");
        response = createStringLiteralMalloced("ERROR: Machine type not found!");
    }
    responseSize = strlen(response) + 1;
    return response;
}

//Generates capability key pair, should only be called after successful remote attestation
int createCapabilityKey(char* newMachinePublicIDKey, char* parentTrustedMachinePublicIDKey, char* requestedNewMachineTypeToCreate) {
    ocall_print("CREATING CAPABILITY USING");
    printRSAKey(newMachinePublicIDKey);
    printRSAKey(parentTrustedMachinePublicIDKey);
    ocall_print(requestedNewMachineTypeToCreate);
    char* private_capability_key_raw = (char*) malloc(SGX_RSA3072_KEY_SIZE);
    char* public_capability_key_raw = (char*) malloc(SGX_RSA3072_KEY_SIZE);
    char* private_capability_key = (char*) malloc(sizeof(sgx_rsa3072_key_t));
    char* public_capability_key = (char*) malloc(sizeof(sgx_rsa3072_public_key_t));
    sgx_status_t status = enclave_createRsaKeyPairEcall(kps_enclave_eid, public_capability_key_raw, private_capability_key_raw, public_capability_key, private_capability_key, SGX_RSA3072_KEY_SIZE); 
    if (status != SGX_SUCCESS) {
        ocall_print("KPS Error in generating capability keys!");
    }

    char* concatStrings[] = {public_capability_key, ":", private_capability_key};
    int concatLengths[] = {sizeof(sgx_rsa3072_public_key_t), 1, sizeof(sgx_rsa3072_key_t)};
    char* capabilityKey = concatMutipleStringsWithLength(concatStrings, concatLengths, 3);
    int capabilityKeyLen = returnTotalSizeofLengthArray(concatLengths, 3) + 1;

    memcpy(secure_message, capabilityKey, SIZE_OF_CAPABILITYKEY);
    capabilityKeyAccessDictionary[make_tuple(string(newMachinePublicIDKey, SGX_RSA3072_KEY_SIZE), string(requestedNewMachineTypeToCreate))] = string(parentTrustedMachinePublicIDKey, SGX_RSA3072_KEY_SIZE);
    capabilityKeyDictionary[make_tuple(string(newMachinePublicIDKey, SGX_RSA3072_KEY_SIZE), string(requestedNewMachineTypeToCreate))] = string(capabilityKey, SIZE_OF_CAPABILITYKEY);

}

//Retrieves capability key pair to the parent SSM for the child SSM, should only be called after successful remote attestation
//Responsibility of caller to free return value
char* retrieveCapabilityKey(char* currentMachinePublicIDKey, char* childMachinePublicIDKey, char* requestedMachineTypeToCreate) {
    ocall_print("RETRIEVING CAPABILITY USING");
    printRSAKey(currentMachinePublicIDKey);
    printRSAKey(childMachinePublicIDKey);
    ocall_print(requestedMachineTypeToCreate);

    if (capabilityKeyAccessDictionary[make_tuple(string(childMachinePublicIDKey, SGX_RSA3072_KEY_SIZE), string(requestedMachineTypeToCreate))].compare(string(currentMachinePublicIDKey, SGX_RSA3072_KEY_SIZE)) == 0) {
        char* returnCapabilityKey = (char*) malloc(SIZE_OF_CAPABILITYKEY);
        memcpy(returnCapabilityKey, capabilityKeyDictionary[make_tuple(string(childMachinePublicIDKey, SGX_RSA3072_KEY_SIZE), string(requestedMachineTypeToCreate))].c_str(), SIZE_OF_CAPABILITYKEY);
    
        return (char*) returnCapabilityKey;
    } else {
        return createStringLiteralMalloced("Error: No Access!");
    }    

}

//*******************

//KPS functions necessary to attest enclaves*******************

// This is supported extended epid group of Ping machine. Ping machine can support more than one
// extended epid group with different extended epid group id and credentials.
static const sample_extended_epid_group g_extended_epid_groups[] = {
    {
        0,
        ias_enroll,
        ias_get_sigrl,
        ias_verify_attestation_evidence
    }
};

// This is the private part of the capability key. This is used to sign the authenticated
// DH between Ping machine and Pong enclave
static const sample_ec256_private_t g_sp_priv_key = {
    {
        0x90, 0xe7, 0x6c, 0xbb, 0x2d, 0x52, 0xa1, 0xce,
        0x3b, 0x66, 0xde, 0x11, 0x43, 0x9c, 0x87, 0xec,
        0x1f, 0x86, 0x6a, 0x3b, 0x65, 0xb6, 0xae, 0xea,
        0xad, 0x57, 0x34, 0x53, 0xd1, 0x03, 0x8c, 0x01
    }
};

// This is the public part of the capability key
static const sample_ec_pub_t g_sp_pub_key = {
    {
        0x72, 0x12, 0x8a, 0x7a, 0x17, 0x52, 0x6e, 0xbf,
        0x85, 0xd0, 0x3a, 0x62, 0x37, 0x30, 0xae, 0xad,
        0x3e, 0x3d, 0xaa, 0xee, 0x9c, 0x60, 0x73, 0x1d,
        0xb0, 0x5b, 0xe8, 0x62, 0x1c, 0x4b, 0xeb, 0x38
    },
    {
        0xd4, 0x81, 0x40, 0xd9, 0x50, 0xe2, 0x57, 0x7b,
        0x26, 0xee, 0xb7, 0x41, 0xe7, 0xc6, 0x14, 0xe2,
        0x24, 0xb7, 0xbd, 0xc9, 0x03, 0xf2, 0x9a, 0x28,
        0xa8, 0x3c, 0xc8, 0x10, 0x11, 0x14, 0x5e, 0x06
    }
};

// This is a context data structure used for Ping Machine
typedef struct _sp_db_item_t
{
    sample_ec_pub_t             g_a;
    sample_ec_pub_t             g_b;
    sample_ec_key_128bit_t      vk_key;// Shared secret key for the REPORT_DATA
    sample_ec_key_128bit_t      mk_key;// Shared secret key for generating MAC's
    sample_ec_key_128bit_t      sk_key;// Shared secret key for encryption
    sample_ec_key_128bit_t      smk_key;// Used only for SIGMA protocol
    sample_ec_priv_t            b;
    sample_ps_sec_prop_desc_t   ps_sec_prop;
}sp_db_item_t;
static sp_db_item_t g_sp_db;

static const sample_extended_epid_group* g_sp_extended_epid_group_id= NULL;
static bool g_is_sp_registered = false;
static int g_sp_credentials = 0;
static int g_authentication_token = 0;

uint8_t g_secret[SIZE_OF_MESSAGE] = {0,1,2,3,4,5,6,7};

sample_spid_t g_spid;


//Code for parsing signed files to obtain expected measurement of enclave
#define MAX_LINE 4096
char* extract_measurement(FILE* fp)
{
  char *linha = (char*) malloc(MAX_LINE);
  int s, t;
  char lemma[100];
  bool match_found = false;
  char* measurement = (char*) malloc(100);
  int i = 0;
  while(fgets(linha, MAX_LINE, fp))
  {
      //printf("%s", linha);
    if (strcmp(linha, "metadata->enclave_css.body.isv_prod_id: 0x0\n") == 0) {
        //printf("%s", linha);
        //printf("%s", measurement);
        //printf("\nEnd found!\n");
        measurement[i] = '\0';
        safe_free(linha);
        return measurement;
    }
    if (match_found == true) {
        int len = strlen( linha );
        bool skip = true;
        for (int k = 0; k < len; k++) {
            if (linha[k] == '\\') {
                break;
            }
            if (linha[k] == ' ') {
                continue;
            }
            if (skip) {
                skip = false;
                k++;
            } else {
                skip = true;
                measurement[i] = linha[k];
                i++;
                k++;
                measurement[i] = linha[k];
                i++;
            }
            
        }
    }
    if (strcmp(linha, "metadata->enclave_css.body.enclave_hash.m:\n") == 0) {
        //printf("MATCH FOUND\n");
        match_found = true;
    }   
   }
   safe_free(measurement);
   safe_free(linha);

   return NULL;
}
//
//NOTE code below is largely taken from Intel RemoteAttestation SampleCode and may contain
//comments in those regards

// Verify message 0 then configure extended epid group.
int sp_ra_proc_msg0_req(const sample_ra_msg0_t *p_msg0,
    uint32_t msg0_size)
{
    int ret = -1;

    if (!p_msg0 ||
        (msg0_size != sizeof(sample_ra_msg0_t)))
    {
        return -1;
    }
    uint32_t extended_epid_group_id = p_msg0->extended_epid_group_id;

    // Check to see if we have registered with the attestation server yet?
    if (!g_is_sp_registered ||
        (g_sp_extended_epid_group_id != NULL && g_sp_extended_epid_group_id->extended_epid_group_id != extended_epid_group_id))
    {
        // Check to see if the extended_epid_group_id is supported?
        ret = SP_UNSUPPORTED_EXTENDED_EPID_GROUP;
        for (size_t i = 0; i < sizeof(g_extended_epid_groups) / sizeof(sample_extended_epid_group); i++)
        {
            if (g_extended_epid_groups[i].extended_epid_group_id == extended_epid_group_id)
            {
                g_sp_extended_epid_group_id = &(g_extended_epid_groups[i]);
                // In the product, the Ping Machine will establish a mutually
                // authenticated SSL channel. During the enrollment process, the ISV
                // registers it exchanges TLS certs with attestation server and obtains an SPID and
                // Report Key from the attestation server.
                // For a product attestation server, enrollment is an offline process.  See the 'on-boarding'
                // documentation to get the information required.  The enrollment process is
                // simulated by a call in this sample.
                ret = g_sp_extended_epid_group_id->enroll(g_sp_credentials, &g_spid,
                    &g_authentication_token);
                if (0 != ret)
                {
                    ret = SP_IAS_FAILED;
                    break;
                }

                g_is_sp_registered = true;
                ret = SP_OK;
                break;
            }
        }
    }
    else
    {
        ret = SP_OK;
    }

    return ret;
}

// Verify message 1 then generate and return message 2 to isv.
int sp_ra_proc_msg1_req(const sample_ra_msg1_t *p_msg1,
						uint32_t msg1_size,
						ra_samp_response_header_t **pp_msg2)
{
    int ret = 0;
    ra_samp_response_header_t* p_msg2_full = NULL;
    sample_ra_msg2_t *p_msg2 = NULL;
    sample_ecc_state_handle_t ecc_state = NULL;
    sample_status_t sample_ret = SAMPLE_SUCCESS;
    bool derive_ret = false;

    if(!p_msg1 ||
       !pp_msg2 ||
       (msg1_size != sizeof(sample_ra_msg1_t)))
    {
        return -1;
    }

    // Check to see if we have registered?
    if (!g_is_sp_registered)
    {
        return SP_UNSUPPORTED_EXTENDED_EPID_GROUP;
    }

    do
    {
        // Get the sig_rl from attestation server using GID.
        // GID is Base-16 encoded of EPID GID in little-endian format.
        // In the product, the SP and attesation server uses an established channel for
        // communication.
        uint8_t* sig_rl;
        uint32_t sig_rl_size = 0;

        // The product interface uses a REST based message to get the SigRL.
        
        ret = g_sp_extended_epid_group_id->get_sigrl(p_msg1->gid, &sig_rl_size, &sig_rl);
        if(0 != ret)
        {
            fprintf(stderr, "\nError, ias_get_sigrl [%s].", __FUNCTION__);
            ret = SP_IAS_FAILED;
            break;
        }

        // Need to save the client's public ECCDH key to local storage
        if (memcpy_s(&g_sp_db.g_a, sizeof(g_sp_db.g_a), &p_msg1->g_a,
                     sizeof(p_msg1->g_a)))
        {
            fprintf(stderr, "\nError, cannot do memcpy in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        // Generate the Service providers ECCDH key pair.
        sample_ret = (sample_status_t) enclave_sgx_ecc256_open_context_Ecall(kps_enclave_eid, (sgx_ecc_state_handle_t*)&ecc_state);
        if(SAMPLE_SUCCESS != sample_ret)
        {
            fprintf(stderr, "\nError, cannot get ECC context in [%s].",
                             __FUNCTION__);
            ocall_print("Error: KPS attestation error in open context");
            ret = -1;
            break;
        }
        sample_ec256_public_t pub_key = {{0},{0}};
        sample_ec256_private_t priv_key = {{0}};
        sample_ret = (sample_status_t)enclave_sgx_ecc256_create_key_pair_Ecall(kps_enclave_eid, 
                                               (sgx_ec256_private_t*) &priv_key, 
                                               (sgx_ec256_public_t*)&pub_key,
                                                (sgx_ecc_state_handle_t) ecc_state);                                        
        if(SAMPLE_SUCCESS != sample_ret)
        {
            fprintf(stderr, "\nError, cannot generate key pair in [%s].",
                    __FUNCTION__);
            ocall_print("Error: KPS attestation error in create key pair");
            ret = SP_INTERNAL_ERROR;
            break;
        }

        // Need to save the SP ECCDH key pair to local storage.
        if(memcpy_s(&g_sp_db.b, sizeof(g_sp_db.b), &priv_key,sizeof(priv_key))
           || memcpy_s(&g_sp_db.g_b, sizeof(g_sp_db.g_b),
                       &pub_key,sizeof(pub_key)))
        {
            fprintf(stderr, "\nError, cannot do memcpy in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        // Generate the client/SP shared secret
        sample_ec_dh_shared_t dh_key = {{0}};
        sample_ret = (sample_status_t) enclave_sgx_ecc256_compute_shared_dhkey_Ecall( kps_enclave_eid,
            (sgx_ec256_private_t *)&priv_key,
            (sgx_ec256_public_t *)&p_msg1->g_a,
            (sgx_ec256_dh_shared_t *)&dh_key,
            (sgx_ecc_state_handle_t) ecc_state);
        if(SAMPLE_SUCCESS != sample_ret)
        {
            fprintf(stderr, "\nError, compute share key fail in [%s].",
                    __FUNCTION__);
            ocall_print("Error: KPS attestation error in compute shared dhkey");
            ret = SP_INTERNAL_ERROR;
            break;
        }

#ifdef SUPPLIED_KEY_DERIVATION

        // smk is only needed for msg2 generation.
        derive_ret = derive_key(&dh_key, SAMPLE_DERIVE_KEY_SMK_SK,
            &g_sp_db.smk_key, &g_sp_db.sk_key);
        if(derive_ret != true)
        {
            fprintf(stderr, "\nError, derive key fail in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        // The rest of the keys are the shared secrets for future communication.
        derive_ret = derive_key(&dh_key, SAMPLE_DERIVE_KEY_MK_VK,
            &g_sp_db.mk_key, &g_sp_db.vk_key);
        if(derive_ret != true)
        {
            fprintf(stderr, "\nError, derive key fail in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
#else
        // smk is only needed for msg2 generation.
        derive_ret = derive_key(&dh_key, SAMPLE_DERIVE_KEY_SMK,
                                &g_sp_db.smk_key);
        if(derive_ret != true)
        {
            fprintf(stderr, "\nError, derive key fail in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        // The rest of the keys are the shared secrets for future communication.
        derive_ret = derive_key(&dh_key, SAMPLE_DERIVE_KEY_MK,
                                &g_sp_db.mk_key);
        if(derive_ret != true)
        {
            fprintf(stderr, "\nError, derive key fail in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        derive_ret = derive_key(&dh_key, SAMPLE_DERIVE_KEY_SK,
                                &g_sp_db.sk_key);
        if(derive_ret != true)
        {
            fprintf(stderr, "\nError, derive key fail in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        derive_ret = derive_key(&dh_key, SAMPLE_DERIVE_KEY_VK,
                                &g_sp_db.vk_key);
        if(derive_ret != true)
        {
            fprintf(stderr, "\nError, derive key fail in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
#endif

        uint32_t msg2_size = (uint32_t)sizeof(sample_ra_msg2_t) + sig_rl_size;
        p_msg2_full = (ra_samp_response_header_t*)malloc(msg2_size
                      + sizeof(ra_samp_response_header_t));
        if(!p_msg2_full)
        {
            fprintf(stderr, "\nError, out of memory in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
        memset(p_msg2_full, 0, msg2_size + sizeof(ra_samp_response_header_t));
        p_msg2_full->type = TYPE_RA_MSG2;
        p_msg2_full->size = msg2_size;
        // The simulated message2 always passes.  This would need to be set
        // accordingly in a real service provider implementation.
        p_msg2_full->status[0] = 0;
        p_msg2_full->status[1] = 0;
        p_msg2 = (sample_ra_msg2_t *)p_msg2_full->body;

        // Assemble MSG2
        if(memcpy_s(&p_msg2->g_b, sizeof(p_msg2->g_b), &g_sp_db.g_b,
                    sizeof(g_sp_db.g_b)) ||
           memcpy_s(&p_msg2->spid, sizeof(sample_spid_t),
                    &g_spid, sizeof(g_spid)))
        {
            fprintf(stderr,"\nError, memcpy failed in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        // The service provider is responsible for selecting the proper EPID
        // signature type and to understand the implications of the choice!
        p_msg2->quote_type = SAMPLE_QUOTE_LINKABLE_SIGNATURE;

#ifdef SUPPLIED_KEY_DERIVATION
//isv defined key derivation function id
#define ISV_KDF_ID 2
        p_msg2->kdf_id = ISV_KDF_ID;
#else
        p_msg2->kdf_id = SAMPLE_AES_CMAC_KDF_ID;
#endif
        // Create gb_ga
        sample_ec_pub_t gb_ga[2];
        if(memcpy_s(&gb_ga[0], sizeof(gb_ga[0]), &g_sp_db.g_b,
                    sizeof(g_sp_db.g_b))
           || memcpy_s(&gb_ga[1], sizeof(gb_ga[1]), &g_sp_db.g_a,
                       sizeof(g_sp_db.g_a)))
        {
            fprintf(stderr,"\nError, memcpy failed in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        // Sign gb_ga
        // sample_ret = sample_ecdsa_sign((uint8_t *)&gb_ga, sizeof(gb_ga),
        //                 (sample_ec256_private_t *)&g_sp_priv_key,
        //                 (sample_ec256_signature_t *)&p_msg2->sign_gb_ga,
        //                 ecc_state);
        sample_ret = (sample_status_t) enclave_sgx_ecdsa_sign_Ecall( kps_enclave_eid,
                        (uint8_t *)&gb_ga, sizeof(gb_ga),
                        (sgx_ec256_private_t *)&g_sp_priv_key,
                        (sgx_ec256_signature_t *)&p_msg2->sign_gb_ga,
                        (sgx_ecc_state_handle_t) ecc_state);
        if(SAMPLE_SUCCESS != sample_ret)
        {
            fprintf(stderr, "\nError, sign ga_gb fail in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        // Generate the CMACsmk for gb||SPID||TYPE||KDF_ID||Sigsp(gb,ga)
        uint8_t mac[SAMPLE_EC_MAC_SIZE] = {0};
        uint32_t cmac_size = offsetof(sample_ra_msg2_t, mac);
        // sample_ret = sample_rijndael128_cmac_msg(&g_sp_db.smk_key,
        //     (uint8_t *)&p_msg2->g_b, cmac_size, &mac);
        sample_ret = (sample_status_t) enclave_sgx_rijndael128_cmac_msg_Ecall(kps_enclave_eid, &g_sp_db.smk_key,
            (uint8_t *)&p_msg2->g_b, cmac_size, &mac);
        if(SAMPLE_SUCCESS != sample_ret)
        {
            fprintf(stderr, "\nError, cmac fail in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
        if(memcpy_s(&p_msg2->mac, sizeof(p_msg2->mac), mac, sizeof(mac)))
        {
            fprintf(stderr,"\nError, memcpy failed in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        if(memcpy_s(&p_msg2->sig_rl[0], sig_rl_size, sig_rl, sig_rl_size))
        {
            fprintf(stderr,"\nError, memcpy failed in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
        p_msg2->sig_rl_size = sig_rl_size;

    }while(0);

    if(ret)
    {
        *pp_msg2 = NULL;
        SAFE_FREE(p_msg2_full);
    }
    else
    {
        // Freed by the network simulator in ra_free_network_response_buffer
        *pp_msg2 = p_msg2_full;
    }

    if(ecc_state)
    {
        enclave_sgx_ecc256_close_context_Ecall(kps_enclave_eid, (sgx_ecc_state_handle_t) ecc_state);
    }

    return ret;
}

// Process remote attestation message 3
int sp_ra_proc_msg3_req(const sample_ra_msg3_t *p_msg3,
                        uint32_t msg3_size,
                        ra_samp_response_header_t **pp_att_result_msg)
{
    int ret = 0;
    sample_status_t sample_ret = SAMPLE_SUCCESS;
    const uint8_t *p_msg3_cmaced = NULL;
    const sample_quote_t *p_quote = NULL;
    sample_sha_state_handle_t sha_handle = NULL;
    sample_report_data_t report_data = {0};
    sample_ra_att_result_msg_t *p_att_result_msg = NULL;
    ra_samp_response_header_t* p_att_result_msg_full = NULL;
    uint32_t i;

    if((!p_msg3) ||
       (msg3_size < sizeof(sample_ra_msg3_t)) ||
       (!pp_att_result_msg))
    {
        return SP_INTERNAL_ERROR;
    }

    // Check to see if we have registered?
    if (!g_is_sp_registered)
    {
        return SP_UNSUPPORTED_EXTENDED_EPID_GROUP;
    }
    do
    {
        // Compare g_a in message 3 with local g_a.
        ret = memcmp(&g_sp_db.g_a, &p_msg3->g_a, sizeof(sample_ec_pub_t));
        if(ret)
        {
            fprintf(stderr, "\nError, g_a is not same [%s].", __FUNCTION__);
            ret = SP_PROTOCOL_ERROR;
            break;
        }
        //Make sure that msg3_size is bigger than sample_mac_t.
        uint32_t mac_size = msg3_size - (uint32_t)sizeof(sample_mac_t);
        p_msg3_cmaced = reinterpret_cast<const uint8_t*>(p_msg3);
        p_msg3_cmaced += sizeof(sample_mac_t);

        // Verify the message mac using SMK
        sample_cmac_128bit_tag_t mac = {0};
        // sample_ret = sample_rijndael128_cmac_msg(&g_sp_db.smk_key,
        //                                    p_msg3_cmaced,
        //                                    mac_size,
        //                                    &mac);
        sample_ret = (sample_status_t) enclave_sgx_rijndael128_cmac_msg_Ecall(kps_enclave_eid,
                                            &g_sp_db.smk_key,
                                           p_msg3_cmaced,
                                           mac_size,
                                           &mac);
        if(SAMPLE_SUCCESS != sample_ret)
        {
            fprintf(stderr, "\nError, cmac fail in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
        // In real implementation, should use a time safe version of memcmp here,
        // in order to avoid side channel attack.
        ret = memcmp(&p_msg3->mac, mac, sizeof(mac));
        if(ret)
        {
            fprintf(stderr, "\nError, verify cmac fail [%s].", __FUNCTION__);
            ret = SP_INTEGRITY_FAILED;
            break;
        }

        if(memcpy_s(&g_sp_db.ps_sec_prop, sizeof(g_sp_db.ps_sec_prop),
            &p_msg3->ps_sec_prop, sizeof(p_msg3->ps_sec_prop)))
        {
            fprintf(stderr,"\nError, memcpy failed in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        p_quote = (const sample_quote_t*)p_msg3->quote;

        // Check the quote version if needed. Only check the Quote.version field if the enclave
        // identity fields have changed or the size of the quote has changed.  The version may
        // change without affecting the legacy fields or size of the quote structure.
        //if(p_quote->version < ACCEPTED_QUOTE_VERSION)
        //{
        //    fprintf(stderr,"\nError, quote version is too old.", __FUNCTION__);
        //    ret = SP_QUOTE_VERSION_ERROR;
        //    break;
        //}

        // Verify the report_data in the Quote matches the expected value.
        // The first 32 bytes of report_data are SHA256 HASH of {ga|gb|vk}.
        // The second 32 bytes of report_data are set to zero.
        // sample_ret = sample_sha256_init(&sha_handle);
        sample_ret = (sample_status_t) enclave_sgx_sha256_init_Ecall(kps_enclave_eid, (sgx_sha_state_handle_t*) &sha_handle);
        if(sample_ret != SAMPLE_SUCCESS)
        {
            fprintf(stderr,"\nError, init hash failed in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
        // sample_ret = sample_sha256_update((uint8_t *)&(g_sp_db.g_a),
        //                              sizeof(g_sp_db.g_a), sha_handle);
        sample_ret = (sample_status_t) enclave_sgx_sha256_update_Ecall(kps_enclave_eid, 
        (uint8_t *)&(g_sp_db.g_a), sizeof(g_sp_db.g_a), (sgx_sha_state_handle_t)sha_handle);

        if(sample_ret != SAMPLE_SUCCESS)
        {
            fprintf(stderr,"\nError, udpate hash failed in [%s].",
                    __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
        // sample_ret = sample_sha256_update((uint8_t *)&(g_sp_db.g_b),
        //                              sizeof(g_sp_db.g_b), sha_handle);
        sample_ret = (sample_status_t) enclave_sgx_sha256_update_Ecall(kps_enclave_eid, 
        (uint8_t *)&(g_sp_db.g_b), sizeof(g_sp_db.g_b), (sgx_sha_state_handle_t)sha_handle);
        if(sample_ret != SAMPLE_SUCCESS)
        {
            fprintf(stderr,"\nError, udpate hash failed in [%s].",
                    __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
        // sample_ret = sample_sha256_update((uint8_t *)&(g_sp_db.vk_key),
        //                              sizeof(g_sp_db.vk_key), sha_handle);
        sample_ret = (sample_status_t) enclave_sgx_sha256_update_Ecall(kps_enclave_eid, 
        (uint8_t *)&(g_sp_db.vk_key), sizeof(g_sp_db.vk_key), (sgx_sha_state_handle_t)sha_handle);
        if(sample_ret != SAMPLE_SUCCESS)
        {
            fprintf(stderr,"\nError, udpate hash failed in [%s].",
                    __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
        // sample_ret = sample_sha256_get_hash(sha_handle,
        //                               (sample_sha256_hash_t *)&report_data);
        sample_ret = (sample_status_t) enclave_sgx_sha256_get_hash_Ecall(kps_enclave_eid, 
        (sgx_sha_state_handle_t)sha_handle, (sgx_sha256_hash_t *)&report_data);
        if(sample_ret != SAMPLE_SUCCESS)
        {
            fprintf(stderr,"\nError, Get hash failed in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
        ret = memcmp((uint8_t *)&report_data,
                     &(p_quote->report_body.report_data),
                     sizeof(report_data));
        if(ret)
        {
            fprintf(stderr, "\nError, verify hash fail [%s].", __FUNCTION__);
            ret = SP_INTEGRITY_FAILED;
            break;
        }


        //Verify the measurement of the enclave is the same as the expected measurement from the file
        FILE *fp1 = fopen("metadata_info.txt", "r"); 
        if (fp1 == NULL) 
        { 
            ocall_print("Error : File not open"); 
            exit(0); 
        } 

        char* expected_measurement = extract_measurement(fp1);
        char* actual_measurement = (char*) malloc(100);
        char* ptr = actual_measurement;

        if (ENABLE_KPS_ATTESTATION_PRINT) {
            ocall_print("Expected Measurement is:");
            ocall_print(expected_measurement);
        }

        for(i=0;i<sizeof(sample_measurement_t);i++)
        {
            sprintf(ptr, "%02x",p_quote->report_body.mr_enclave[i]);
            ptr += 2;
        }
        ptr[i] = '\0';

        if (ENABLE_KPS_ATTESTATION_PRINT) {
            ocall_print("Actual Measurement is: ");
            ocall_print(actual_measurement);
        }

        //If measurements differ, we need to abort this connection 
        if (!(strcmp(expected_measurement, actual_measurement) == 0)) {
            ocall_print("MEASUREMENT ERROR!");
            ret = SP_QUOTE_VERIFICATION_FAILED;
            break;
        }    
        fclose(fp1);
        safe_free(expected_measurement);



        // Verify Enclave policy (an attestation server may provide an API for this if we
        // registered an Enclave policy)

        // Verify quote with attestation server.
        // In the product, an attestation server could use a REST message and JSON formatting to request
        // attestation Quote verification.  The sample only simulates this interface.
        ias_att_report_t attestation_report;
        memset(&attestation_report, 0, sizeof(attestation_report));
        ret = g_sp_extended_epid_group_id->verify_attestation_evidence(p_quote, NULL,
                                              &attestation_report);
        if(0 != ret)
        {
            ret = SP_IAS_FAILED;
            break;
        }
        FILE* OUTPUT;
        if (ENABLE_KPS_ATTESTATION_PRINT) {
            OUTPUT = stdout;
        } else {
            OUTPUT =  fopen ("temper.txt" , "w");
        }

        fprintf(OUTPUT, "\n\n\tAttestation Report:");
        fprintf(OUTPUT, "\n\tid: 0x%0x.", attestation_report.id);
        fprintf(OUTPUT, "\n\tstatus: %d.", attestation_report.status);
        fprintf(OUTPUT, "\n\trevocation_reason: %u.",
                attestation_report.revocation_reason);
        // attestation_report.info_blob;
        fprintf(OUTPUT, "\n\tpse_status: %d.",  attestation_report.pse_status);
        
        // Note: This sample always assumes the PIB is sent by attestation server.  In the product
        // implementation, the attestation server could only send the PIB for certain attestation 
        // report statuses.  A product SP implementation needs to handle cases
        // where the PIB is zero length.

        // Respond the client with the results of the attestation.
        uint32_t att_result_msg_size = sizeof(sample_ra_att_result_msg_t);
        p_att_result_msg_full =
            (ra_samp_response_header_t*)malloc(att_result_msg_size
            + sizeof(ra_samp_response_header_t) + sizeof(g_secret));
        if(!p_att_result_msg_full)
        {
            fprintf(stderr, "\nError, out of memory in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }
        memset(p_att_result_msg_full, 0, att_result_msg_size
               + sizeof(ra_samp_response_header_t) + sizeof(g_secret));
        p_att_result_msg_full->type = TYPE_RA_ATT_RESULT;
        p_att_result_msg_full->size = att_result_msg_size + sizeof(g_secret);
        if(IAS_QUOTE_OK != attestation_report.status)
        {
            p_att_result_msg_full->status[0] = 0xFF;
        }
        if(IAS_PSE_OK != attestation_report.pse_status)
        {
            p_att_result_msg_full->status[1] = 0xFF;
        }

        p_att_result_msg =
            (sample_ra_att_result_msg_t *)p_att_result_msg_full->body;

        // In a product implementation of attestation server, the HTTP response header itself could have
        // an RK based signature that the service provider needs to check here.

        // The platform_info_blob signature will be verified by the client
        // when sent. No need to have the Service Provider to check it.  The SP
        // should pass it down to the application for further analysis.

        fprintf(OUTPUT, "\n\n\tEnclave Report:");
        fprintf(OUTPUT, "\n\tSignature Type: 0x%x", p_quote->sign_type);
        fprintf(OUTPUT, "\n\tSignature Basename: ");
        for(i=0; i<sizeof(p_quote->basename.name) && p_quote->basename.name[i];
            i++)
        {
            fprintf(OUTPUT, "%c", p_quote->basename.name[i]);
        }
#ifdef __x86_64__
        fprintf(OUTPUT, "\n\tattributes.flags: 0x%0lx",
                p_quote->report_body.attributes.flags);
        fprintf(OUTPUT, "\n\tattributes.xfrm: 0x%0lx",
                p_quote->report_body.attributes.xfrm);
#else
        fprintf(OUTPUT, "\n\tattributes.flags: 0x%0llx",
                p_quote->report_body.attributes.flags);
        fprintf(OUTPUT, "\n\tattributes.xfrm: 0x%0llx",
                p_quote->report_body.attributes.xfrm);
#endif
        fprintf(OUTPUT, "\n\tmr_enclave: ");
        for(i=0;i<sizeof(sample_measurement_t);i++)
        {

            fprintf(OUTPUT, "%02x",p_quote->report_body.mr_enclave[i]);

            //fprintf(stderr, "%02x",p_quote->report_body.mr_enclave.m[i]);

        }
        fprintf(OUTPUT, "\n\tmr_signer: ");
        for(i=0;i<sizeof(sample_measurement_t);i++)
        {

            fprintf(OUTPUT, "%02x",p_quote->report_body.mr_signer[i]);

            //fprintf(stderr, "%02x",p_quote->report_body.mr_signer.m[i]);

        }
        fprintf(OUTPUT, "\n\tisv_prod_id: 0x%0x",
                p_quote->report_body.isv_prod_id);
        fprintf(OUTPUT, "\n\tisv_svn: 0x%0x",p_quote->report_body.isv_svn);
        fprintf(OUTPUT, "\n");

        // A product service provider needs to verify that its enclave properties 
        // match what is expected.  The SP needs to check these values before
        // trusting the enclave.  For the sample, we always pass the policy check.
        // Attestation server only verifies the quote structure and signature.  It does not 
        // check the identity of the enclave.
        bool isv_policy_passed = true;

        // Assemble Attestation Result Message
        // Note, this is a structure copy.  We don't copy the policy reports
        // right now.
        p_att_result_msg->platform_info_blob = attestation_report.info_blob;

        // Generate mac based on the mk key.
        mac_size = sizeof(ias_platform_info_blob_t);
        // sample_ret = sample_rijndael128_cmac_msg(&g_sp_db.mk_key,
        //     (const uint8_t*)&p_att_result_msg->platform_info_blob,
        //     mac_size,
        //     &p_att_result_msg->mac);
        sample_ret = (sample_status_t) enclave_sgx_rijndael128_cmac_msg_Ecall(kps_enclave_eid,
                                                &g_sp_db.mk_key,
                                                (const uint8_t*)&p_att_result_msg->platform_info_blob,
                                                mac_size,
                                                &p_att_result_msg->mac);
        if(SAMPLE_SUCCESS != sample_ret)
        {
            fprintf(stderr, "\nError, cmac fail in [%s].", __FUNCTION__);
            ret = SP_INTERNAL_ERROR;
            break;
        }

        
    }while(0);

    if(ret)
    {
        *pp_att_result_msg = NULL;
        SAFE_FREE(p_att_result_msg_full);
    }
    else
    {
        // Freed by the network simulator in ra_free_network_response_buffer
        *pp_att_result_msg = p_att_result_msg_full;
    }
    return ret;
}

//We have already created an attestation channel before this point
//TODO fix memory leaks
ra_samp_response_header_t* kps_exchange_capability_key(uint8_t *p_secret,  
                                uint32_t secret_size,
                                 uint8_t *p_gcm_mac) {

        uint8_t aes_gcm_iv[12] = {0};
        int ret = 0;

        enclave_sgx_rijndael128GCM_decrypt_Ecall(kps_enclave_eid,
                            &g_sp_db.sk_key, 
                            p_secret,
                            secret_size,
                            &g_secret[0],
                            &aes_gcm_iv[0],
                            SAMPLE_SP_IV_SIZE,
                            NULL,
                            0,
                            (sample_aes_gcm_128bit_tag_t *)p_gcm_mac);

        printf("KPS Secret is %s\n" , (char*)g_secret);
        printPayload((char*) g_secret, secret_size);

        int messageSize = SIZE_OF_MESSAGE;

        char* message = (char*) malloc(messageSize);


        char* queryString = (char*) malloc(secret_size);
        memcpy(queryString, g_secret, secret_size);

        char* split = strtok(queryString, ":");
        char* type = split;
        if (strcmp(type, "Create") == 0) {
            char* queryPayload = queryString + strlen("Create") + 1;

            char* childID = (char*) malloc(SGX_RSA3072_KEY_SIZE);
            char* parentID = (char*) malloc(SGX_RSA3072_KEY_SIZE);
            memcpy(childID, queryPayload, SGX_RSA3072_KEY_SIZE);
            memcpy(parentID, queryPayload + SGX_RSA3072_KEY_SIZE + 1, SGX_RSA3072_KEY_SIZE);

            char* split = strtok(queryPayload + SGX_RSA3072_KEY_SIZE + 1 + SGX_RSA3072_KEY_SIZE + 1, ":");
            int requestedMachineTypeSize = atoi(split);
            char* requestedMachineSizeToCreate = (char*) malloc(requestedMachineTypeSize + 1);
            // encryptedMessage[strlen(split)] = ':'; //undoing effect of strtok
            strncpy(requestedMachineSizeToCreate, queryPayload + SGX_RSA3072_KEY_SIZE + 1 + SGX_RSA3072_KEY_SIZE + 1 + strlen(split) + 1, requestedMachineTypeSize);
            requestedMachineSizeToCreate[requestedMachineTypeSize] = '\0';

            createCapabilityKey(childID, parentID, requestedMachineSizeToCreate);

            safe_free(childID);
            safe_free(parentID);
            safe_free(requestedMachineSizeToCreate);

            // strcpy((char*)g_secret, secure_message); //TODO XXXidentity
            memcpy(message, secure_message, SIZE_OF_MESSAGE);
            // p_att_result_msg->secret.payload_size = SIZE_OF_MESSAGE;
            ocall_print("Debug1: Capability Key");

            printPayload(message, SGX_RSA3072_KEY_SIZE);

        } else {
            char* queryPayload = queryString + strlen("Retrieve") + 1;

            char* currentMachineID = (char*) malloc(SGX_RSA3072_KEY_SIZE);
            char* childID = (char*) malloc(SGX_RSA3072_KEY_SIZE);
            memcpy(currentMachineID, queryPayload, SGX_RSA3072_KEY_SIZE);
            memcpy(childID, queryPayload + SGX_RSA3072_KEY_SIZE + 1, SGX_RSA3072_KEY_SIZE);

            char* split = strtok(queryPayload + SGX_RSA3072_KEY_SIZE + 1 + SGX_RSA3072_KEY_SIZE + 1, ":");
            int requestedMachineTypeSize = atoi(split);
            char* requestedMachineSizeToCreate = (char*) malloc(requestedMachineTypeSize + 1);
            // encryptedMessage[strlen(split)] = ':'; //undoing effect of strtok
            strncpy(requestedMachineSizeToCreate, queryPayload + SGX_RSA3072_KEY_SIZE + 1 + SGX_RSA3072_KEY_SIZE + 1 + strlen(split) + 1, requestedMachineTypeSize);
            requestedMachineSizeToCreate[requestedMachineTypeSize] = '\0';

            char* capabilityKey = retrieveCapabilityKey(currentMachineID, childID, requestedMachineSizeToCreate);
            ocall_print("Cap key generated by KPS is");
            ocall_print(capabilityKey);

            safe_free(childID);
            safe_free(currentMachineID);
            safe_free(requestedMachineSizeToCreate);

            // strcpy((char*)g_secret, secure_message);
            // memcpy(g_secret, capabilityKey, SIZE_OF_MESSAGE);
            // p_att_result_msg->secret.payload_size = SIZE_OF_MESSAGE;
            memcpy(message, capabilityKey, SIZE_OF_CAPABILITYKEY);

            // ocall_print("Debug2: Capability Key");

            // printPayload(message, SIZE_OF_CAPABILITYKEY);



        }




        uint32_t i;
        bool secret_match = true;
        int payload_size = messageSize;

        char* encrypted_payload = (char*) malloc(payload_size);
        uint8_t payload_tag[16];



        //encrypt and send message back
        ret = enclave_sgx_rijndael128GCM_encrypt_Ecall(kps_enclave_eid,
                            &g_sp_db.sk_key,
                            (const uint8_t *)message,
                            payload_size,
                            (uint8_t *)encrypted_payload,
                            &aes_gcm_iv[0],
                            SAMPLE_SP_IV_SIZE,
                            NULL,
                            0,
                            &payload_tag);

        char* payload_sizeString = (char*) malloc(10);
        itoa(payload_size, payload_sizeString, 10);

        char* concatStrings[] = {payload_sizeString, ":", encrypted_payload, ":", (char*)payload_tag};
        int concatLengths[] = {strlen(payload_sizeString), 1, payload_size, 1, 16};
        char* requestString = concatMutipleStringsWithLength(concatStrings, concatLengths, 5);
        int requestStringSize = returnTotalSizeofLengthArray(concatLengths, 5) + 1;

        int respSize = requestStringSize;
        

        ra_samp_response_header_t* p_resp_msg = (ra_samp_response_header_t*)malloc(respSize
                      + sizeof(ra_samp_response_header_t));

        memset(p_resp_msg, 0, respSize + sizeof(ra_samp_response_header_t));
        p_resp_msg->type = TYPE_RA_MSG2;
        p_resp_msg->size = respSize;
        p_resp_msg->status[0] = 0;
        p_resp_msg->status[1] = 0;

        memcpy(p_resp_msg->body, requestString, respSize);

        return p_resp_msg;
}

//*******************