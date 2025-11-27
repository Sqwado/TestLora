#include "SecurityManager.h"

SecurityManager::SecurityManager() : initialized(false) {
	mbedtls_ecp_group_init(&ecdhGrp);
	mbedtls_mpi_init(&ecdhD);
	mbedtls_ecp_point_init(&ecdhQ);
	mbedtls_ctr_drbg_init(&ctrDrbg);
	mbedtls_entropy_init(&entropy);
}

SecurityManager::~SecurityManager() {
	if (initialized) {
		mbedtls_ecp_group_free(&ecdhGrp);
		mbedtls_mpi_free(&ecdhD);
		mbedtls_ecp_point_free(&ecdhQ);
		mbedtls_ctr_drbg_free(&ctrDrbg);
		mbedtls_entropy_free(&entropy);
	}
}

bool SecurityManager::init() {
	if (initialized) return true;
	
	rngInit();
	mbedtls_ecp_group_load(&ecdhGrp, MBEDTLS_ECP_DP_SECP256R1);
	initialized = true;
	return true;
}

void SecurityManager::rngInit() {
	const char* pers = "lora-ecdh";
	mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy, 
	                     (const unsigned char*)pers, strlen(pers));
}

void SecurityManager::sha256(const uint8_t* data, size_t len, uint8_t out32[32]) {
	mbedtls_sha256_context ctx;
	mbedtls_sha256_init(&ctx);
	mbedtls_sha256_starts_ret(&ctx, 0);
	mbedtls_sha256_update_ret(&ctx, data, len);
	mbedtls_sha256_finish_ret(&ctx, out32);
	mbedtls_sha256_free(&ctx);
}

bool SecurityManager::generateKeypair(std::vector<uint8_t>& pubOut) {
	if (!initialized) {
		if (!init()) return false;
	}
	
	mbedtls_ecp_keypair kp;
	mbedtls_ecp_keypair_init(&kp);
	
	if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, &kp, 
	                        mbedtls_ctr_drbg_random, &ctrDrbg) != 0) {
		mbedtls_ecp_keypair_free(&kp);
		return false;
	}
	
	// Stocker d (priv) et Q (pub)
	mbedtls_mpi_copy(&ecdhD, &kp.d);
	mbedtls_ecp_copy(&ecdhQ, &kp.Q);
	
	// Exporter Q non compressé
	unsigned char buf[100];
	size_t olen = 0;
	if (mbedtls_ecp_point_write_binary(&kp.grp, &kp.Q, MBEDTLS_ECP_PF_UNCOMPRESSED, 
	                                  &olen, buf, sizeof(buf)) != 0) {
		mbedtls_ecp_keypair_free(&kp);
		return false;
	}
	
	pubOut.assign(buf, buf + olen);
	mbedtls_ecp_keypair_free(&kp);
	return true;
}

bool SecurityManager::computeSharedSecret(const uint8_t* peerPub, size_t peerLen, 
                                         std::vector<uint8_t>& sharedOut) {
	if (!initialized) {
		if (!init()) return false;
	}
	
	mbedtls_ecp_point Qp;
	mbedtls_ecp_point_init(&Qp);
	
	if (mbedtls_ecp_point_read_binary(&ecdhGrp, &Qp, peerPub, peerLen) != 0) {
		mbedtls_ecp_point_free(&Qp);
		return false;
	}
	
	// R = d * Qp
	mbedtls_ecp_point R;
	mbedtls_ecp_point_init(&R);
	
	if (mbedtls_ecp_mul(&ecdhGrp, &R, &ecdhD, &Qp, 
	                   mbedtls_ctr_drbg_random, &ctrDrbg) != 0) {
		mbedtls_ecp_point_free(&R);
		mbedtls_ecp_point_free(&Qp);
		return false;
	}
	
	// secret = X coordinate of R
	mbedtls_mpi z;
	mbedtls_mpi_init(&z);
	mbedtls_mpi_copy(&z, &R.X);
	
	size_t blen = (mbedtls_mpi_bitlen(&z) + 7) / 8;
	if (blen == 0) blen = 1;
	
	sharedOut.resize(blen);
	mbedtls_mpi_write_binary(&z, sharedOut.data(), blen);
	
	mbedtls_mpi_free(&z);
	mbedtls_ecp_point_free(&R);
	mbedtls_ecp_point_free(&Qp);
	
	return true;
}

void SecurityManager::deriveSessionKeyFromShared(const uint8_t* shared, size_t sharedLen,
                                                 const uint8_t nonceI[16], const uint8_t nonceR[16],
                                                 uint8_t outKey16[16]) {
	std::vector<uint8_t> m;
	m.insert(m.end(), shared, shared + sharedLen);
	m.insert(m.end(), nonceI, nonceI + 16);
	m.insert(m.end(), nonceR, nonceR + 16);
	
	uint8_t full[32];
	sha256(m.data(), m.size(), full);
	memcpy(outKey16, full, 16);
}

void SecurityManager::aesCtrCrypt(const uint8_t key[16], const uint8_t iv[16],
                                 const uint8_t* in, uint8_t* out, size_t len) {
	mbedtls_aes_context aes;
	mbedtls_aes_init(&aes);
	mbedtls_aes_setkey_enc(&aes, key, 128);
	
	unsigned char nonce_counter[16];
	unsigned char stream_block[16];
	memcpy(nonce_counter, iv, 16);
	
	size_t nc_off = 0;
	mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce_counter, stream_block, in, out);
	mbedtls_aes_free(&aes);
}

void SecurityManager::hmacSha256Trunc16(const uint8_t* key, size_t keyLen,
                                       const uint8_t* msg, size_t msgLen, uint8_t out16[16]) {
	// HMAC simplifié pour la démo (hash(K || msg))
	std::vector<uint8_t> buf;
	buf.reserve(keyLen + msgLen);
	buf.insert(buf.end(), key, key + keyLen);
	buf.insert(buf.end(), msg, msg + msgLen);
	
	uint8_t full[32];
	sha256(buf.data(), buf.size(), full);
	memcpy(out16, full, 16);
}

void SecurityManager::generateRandomBytes(uint8_t* out, size_t len) {
	for (size_t i = 0; i < len; ++i) {
		uint32_t r = esp_random();
		out[i] = (uint8_t)(r & 0xFF);
	}
}

bool SecurityManager::exportPublicKey(std::vector<uint8_t>& pubOut) {
	if (!initialized) return false;
	
	unsigned char buf[100];
	size_t olen = 0;
	if (mbedtls_ecp_point_write_binary(&ecdhGrp, &ecdhQ, MBEDTLS_ECP_PF_UNCOMPRESSED,
	                                  &olen, buf, sizeof(buf)) != 0) {
		return false;
	}
	
	pubOut.assign(buf, buf + olen);
	return true;
}

