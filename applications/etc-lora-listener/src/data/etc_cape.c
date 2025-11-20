/***************************************************************************/
/*!
\file       etc_cape.c
\brief      ETC encryption/decryption method

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include "etc_cape.h"

static char *_key;
static uint16_t _key_length;
static char _reduced_key;
// Salt used for encryption, can be exchanged if encrypted
static char salt;

static void compute_reduced_key(char *key, uint16_t length)
{
	_reduced_key = 0;
	for (uint16_t i = 0; i < length; i++)
		_reduced_key ^= (key[i] << (i % 8));
};

static void hash(char *source, char *destination, uint16_t length)
{
	for (uint16_t i = 0; i < length; i++)
		destination[i] = ((_reduced_key ^ source[i] ^ salt ^ i) ^
				  _key[(_reduced_key ^ salt ^ i) % _key_length]);
};

void etc_cape_init(const char *key, int length, char s)
{
	salt = s;
	_key = (char *)key;
	_key_length = length;
	compute_reduced_key(_key, length);
}

void etc_cape_decrypt(char *source, char *destination, uint16_t length)
{
	// 1 - Hash data without triyng to decode initialization vector
	hash(source, destination, length);
	// 2 - Decrypt initialization vector
	length = length - 1;
	destination[length] ^= (_reduced_key ^ salt);
	// 3 - Decrypt data with private key, reduced key and salt
	for (uint16_t i = 0; i < length; i++)
		destination[i] ^=
			((destination[length] ^ i) ^ _key[(salt ^ i ^ _reduced_key) % _key_length]);
	// 4 - Hash data with key (static symmetric hashing)
	hash(destination, destination, length);
}

void etc_cape_encrypt( char *source, char *destination, uint16_t source_length, 
	uint16_t destination_length, uint8_t iv)
{
	// 0 - Write iv to the last element of dest buffer (dest buffer will have length > source + 1 byte)
	destination[destination_length - 1] = iv;
	// 1 - Hash data with key (static symmetric hashing)
	hash(source, destination, source_length);
	// 2 - Encrypt data with private key, reduced key and salt
	for (uint16_t i = 0; i < source_length; i++)
		destination[i] ^=
			((destination[source_length] ^ i) ^ _key[(salt ^ i ^ _reduced_key) % _key_length]);
	// 3 - Encrypt initialization vector using reduced key and salt
	destination[source_length] ^= (_reduced_key ^ salt);
	// 4 - Further encrypt result and initialization vector
	hash(destination, destination, destination_length);
}

void etc_cape_set_key(char *key, uint16_t length)
{
	_key = key;
	_key_length = length;
	compute_reduced_key(key, length);
};