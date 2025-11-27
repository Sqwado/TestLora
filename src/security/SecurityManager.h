#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <cstdint>
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

class SecurityManager {
public:
	SecurityManager();
	~SecurityManager();
	
	// Initialisation
	bool init();
	
	// Génération de clés ECDH
	bool generateKeypair(std::vector<uint8_t>& pubOut);
	bool computeSharedSecret(const uint8_t* peerPub, size_t peerLen, std::vector<uint8_t>& sharedOut);
	
	// Dérivation de clé de session
	void deriveSessionKeyFromShared(const uint8_t* shared, size_t sharedLen, 
	                                const uint8_t nonceI[16], const uint8_t nonceR[16], 
	                                uint8_t outKey16[16]);
	
	// Chiffrement/déchiffrement AES-CTR
	void aesCtrCrypt(const uint8_t key[16], const uint8_t iv[16], 
	                const uint8_t* in, uint8_t* out, size_t len);
	
	// HMAC-SHA256 (tronqué à 16 octets)
	void hmacSha256Trunc16(const uint8_t* key, size_t keyLen, 
	                      const uint8_t* msg, size_t msgLen, uint8_t out16[16]);
	
	// Utilitaires
	void generateRandomBytes(uint8_t* out, size_t len);
	
	// Getters pour les clés publiques/privées (pour appairage)
	bool exportPublicKey(std::vector<uint8_t>& pubOut);
	
private:
	mbedtls_ecp_group ecdhGrp;
	mbedtls_mpi ecdhD; // clé privée
	mbedtls_ecp_point ecdhQ; // clé publique
	mbedtls_ctr_drbg_context ctrDrbg;
	mbedtls_entropy_context entropy;
	bool initialized;
	
	void rngInit();
	void sha256(const uint8_t* data, size_t len, uint8_t out32[32]);
};

#endif // SECURITY_MANAGER_H

