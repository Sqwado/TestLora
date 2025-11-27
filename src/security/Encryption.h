#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <Arduino.h>
#include <string.h>
#include "mbedtls/aes.h"

// ============================================
// MODULE D'ENCRYPTION AES-128
// ============================================
// Utilise mbedtls pour l'encryption/décryption AES-128
// Supporte ECB et CBC (avec IV à zéro pour compatibilité AESLib)

// Clé AES-128 (16 bytes)
// IMPORTANT: Changez cette clé pour votre réseau !
static const uint8_t AES_KEY[16] = { 
    0x2B, 0x7E, 0x15, 0x16, 
    0x28, 0xAE, 0xD2, 0xA6, 
    0xAB, 0xF7, 0x15, 0x88, 
    0x09, 0xCF, 0x4F, 0x3C 
};

// IV pour mode CBC (16 bytes à zéro pour compatibilité avec AESLib)
static const uint8_t AES_IV_ZERO[16] = { 
    0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0 
};

// Taille de bloc AES (toujours 16 bytes)
#define AES_BLOCK_SIZE 16

// Mode d'encryption (décommentez celui que vous voulez)
//#define USE_AES_ECB  // Mode ECB (simple, pas d'IV)
#define USE_AES_CBC  // Mode CBC avec IV=0 (compatible AESLib)

namespace Encryption {
    
    /**
     * Ajoute un padding PKCS7 aux données
     * @param data Buffer de données à padder
     * @param dataLen Longueur des données originales
     * @param paddedData Buffer de sortie (doit être assez grand)
     * @return Longueur des données paddées
     */
    static uint16_t addPadding(const uint8_t* data, uint16_t dataLen, uint8_t* paddedData) {
        // Calculer combien de bytes de padding nécessaires
        uint8_t paddingLen = AES_BLOCK_SIZE - (dataLen % AES_BLOCK_SIZE);
        
        // Copier les données originales
        memcpy(paddedData, data, dataLen);
        
        // Ajouter le padding (PKCS7: tous les bytes de padding ont la valeur du nombre de bytes de padding)
        for (uint8_t i = 0; i < paddingLen; i++) {
            paddedData[dataLen + i] = paddingLen;
        }
        
        return dataLen + paddingLen;
    }
    
    /**
     * Retire le padding PKCS7 des données
     * @param data Buffer de données paddées
     * @param paddedLen Longueur des données paddées
     * @param unpaddedLen Pointeur pour stocker la longueur sans padding
     * @return true si le padding est valide
     */
    static bool removePadding(const uint8_t* data, uint16_t paddedLen, uint16_t* unpaddedLen) {
        if (paddedLen == 0 || paddedLen % AES_BLOCK_SIZE != 0) {
            return false;
        }
        
        // Lire le dernier byte pour connaître la longueur du padding
        uint8_t paddingLen = data[paddedLen - 1];
        
        // Vérifier que le padding est valide
        if (paddingLen == 0 || paddingLen > AES_BLOCK_SIZE || paddingLen > paddedLen) {
            return false;
        }
        
        // Vérifier que tous les bytes de padding ont la même valeur
        for (uint8_t i = 1; i <= paddingLen; i++) {
            if (data[paddedLen - i] != paddingLen) {
                return false;
            }
        }
        
        *unpaddedLen = paddedLen - paddingLen;
        return true;
    }
    
    /**
     * Chiffre des données avec AES-128 (ECB ou CBC selon configuration)
     * @param plaintext Données en clair
     * @param plaintextLen Longueur des données en clair
     * @param ciphertext Buffer de sortie (doit être assez grand pour données + padding)
     * @param ciphertextLen Pointeur pour stocker la longueur chiffrée
     * @return true si succès
     */
    static bool encrypt(const uint8_t* plaintext, uint16_t plaintextLen, 
                       uint8_t* ciphertext, uint16_t* ciphertextLen) {
        // Buffer temporaire pour données paddées
        uint8_t paddedData[256];
        
        // Ajouter le padding
        uint16_t paddedLen = addPadding(plaintext, plaintextLen, paddedData);
        
        // Vérifier que la longueur paddée est un multiple de 16
        if (paddedLen % AES_BLOCK_SIZE != 0 || paddedLen > 256) {
            return false;
        }
        
        // Initialiser le contexte AES
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        
        // Configurer la clé pour encryption
        int ret = mbedtls_aes_setkey_enc(&aes, AES_KEY, 128);
        if (ret != 0) {
            mbedtls_aes_free(&aes);
            return false;
        }
        
#ifdef USE_AES_CBC
        // Mode CBC avec IV à zéro
        uint8_t iv[16];
        memcpy(iv, AES_IV_ZERO, 16);
        
        ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen,
                                    iv, paddedData, ciphertext);
        if (ret != 0) {
            mbedtls_aes_free(&aes);
            return false;
        }
#else
        // Mode ECB (bloc par bloc)
        for (uint16_t i = 0; i < paddedLen; i += AES_BLOCK_SIZE) {
            ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, 
                                        paddedData + i, ciphertext + i);
            if (ret != 0) {
                mbedtls_aes_free(&aes);
                return false;
            }
        }
#endif
        
        *ciphertextLen = paddedLen;
        
        // Libérer le contexte
        mbedtls_aes_free(&aes);
        return true;
    }
    
    /**
     * Déchiffre des données avec AES-128 (ECB ou CBC selon configuration)
     * @param ciphertext Données chiffrées
     * @param ciphertextLen Longueur des données chiffrées
     * @param plaintext Buffer de sortie
     * @param plaintextLen Pointeur pour stocker la longueur déchiffrée
     * @return true si succès
     */
    static bool decrypt(const uint8_t* ciphertext, uint16_t ciphertextLen,
                       uint8_t* plaintext, uint16_t* plaintextLen) {
        // Vérifier que la longueur est un multiple de 16
        if (ciphertextLen == 0 || ciphertextLen % AES_BLOCK_SIZE != 0 || ciphertextLen > 256) {
            return false;
        }
        
        // Buffer temporaire pour données déchiffrées (avec padding)
        uint8_t decryptedPadded[256];
        
        // Initialiser le contexte AES
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        
        int ret;
        
#ifdef USE_AES_CBC
        // Mode CBC - utiliser setkey_dec pour le déchiffrement CBC
        ret = mbedtls_aes_setkey_dec(&aes, AES_KEY, 128);
        if (ret != 0) {
            mbedtls_aes_free(&aes);
            return false;
        }
        
        // Copier l'IV (à zéro)
        uint8_t iv[16];
        memcpy(iv, AES_IV_ZERO, 16);
        
        ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, ciphertextLen,
                                    iv, ciphertext, decryptedPadded);
        if (ret != 0) {
            mbedtls_aes_free(&aes);
            return false;
        }
#else
        // Mode ECB - utiliser setkey_dec
        ret = mbedtls_aes_setkey_dec(&aes, AES_KEY, 128);
        if (ret != 0) {
            mbedtls_aes_free(&aes);
            return false;
        }
        
        // Déchiffrer bloc par bloc
        for (uint16_t i = 0; i < ciphertextLen; i += AES_BLOCK_SIZE) {
            ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT,
                                        ciphertext + i, decryptedPadded + i);
            if (ret != 0) {
                mbedtls_aes_free(&aes);
                return false;
            }
        }
#endif
        
        // Libérer le contexte
        mbedtls_aes_free(&aes);
        
        // Retirer le padding
        uint16_t unpaddedLen;
        if (!removePadding(decryptedPadded, ciphertextLen, &unpaddedLen)) {
            return false;
        }
        
        // Copier les données déchiffrées
        memcpy(plaintext, decryptedPadded, unpaddedLen);
        *plaintextLen = unpaddedLen;
        
        return true;
    }
    
    /**
     * Affiche une clé ou des données en hexadécimal (pour debug)
     */
    static void printHex(const char* label, const uint8_t* data, uint16_t len) {
        Serial.print(label);
        Serial.print(": ");
        for (uint16_t i = 0; i < len; i++) {
            if (data[i] < 0x10) Serial.print("0");
            Serial.print(data[i], HEX);
            if (i < len - 1) Serial.print(" ");
        }
        Serial.println();
    }
}

#endif // ENCRYPTION_H

