#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include "crypto.h"

extern void mylog(char *fmt, ...);

static int key_indexes[KEYLEN] = { 
     255, 144,  94, 194,  13,  31, 180, 232, 241,  21, 177, 226,
     222,  84, 197,   8, 101, 240,  91, 175,  34,  79, 196, 203,
     226,   3,   6, 208,  54,  27,  77,  54
};

static int iv_indexes[IVLEN] = { 
     171, 171, 248, 184, 202, 172, 161, 187, 193,  82, 158, 160,
     166,  99, 168,  11
};

static u_int8_t random_data[256];
static u_int8_t internal_key[KEYLEN];
static u_int8_t internal_iv[IVLEN];

/************************************************************************
 ********************           CODEC_INIT           ********************
 ************************************************************************/
int
crypto_init(char *home)
{
    int		i, fd, rndfd, len;
    struct stat	st;
    char	fname[1024];

    fname[sizeof(fname)-1] = '\0';
    snprintf(fname, sizeof(fname)-1, "%s/.crypto", home);
    len = sizeof(random_data);
    if (stat(fname, &st) < 0)
    {
	if ((rndfd = open("/dev/urandom", O_RDONLY)) < 0)
	    return(-1);
	if (read(rndfd, random_data, len) != len)
	    return(-1);
	if ((fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0)
	    return(-1);

#ifdef ZERO_OUT
	/*
	 * Find any 0 bytes in random data and turn them into something else.
	 * The BIO routines need null-terminated strings.
	 */
	for (i = 0; i < 64; ++i)
	    if (random_data[i] == 0)
		random_data[i] = '@';
#endif

	if (write(fd, random_data, len) != len)
	    return(-1);
	if (fchmod(fd, 0400) < 0)
	    return(-1);
	close(fd);
	close(rndfd);
    }
    else
    {
	if ((fd = open(fname, O_RDONLY)) < 0)
	    return(-1);
	if (read(fd, random_data, len) != len)
	    return(-1);
	close(fd);
    }

    for (i = 0; i < KEYLEN; ++i)
	internal_key[i] = random_data[key_indexes[i]];
    for (i = 0; i < IVLEN; ++i)
	internal_iv[i] = random_data[iv_indexes[i]];

    return(0);
}

/************************************************************************
 ********************             ENCODE             ********************
 ************************************************************************/
int
decode(u_int8_t *src, u_int8_t *dst, int srclen, int dstlen,
    aes256_key_t *aeskey)
{
    int		len, readlen, err;
    char	errbuf[1024];
    u_int8_t	*bufp, *key, *iv;
    BIO		*b64, *membuf, *cipher_bio;
    const EVP_CIPHER	*cipher;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    membuf = BIO_new(BIO_s_mem());
    cipher = EVP_aes_256_cfb128();
    cipher_bio = BIO_new(BIO_f_cipher());
    if (aeskey == NULL)
    {
	key = internal_key;
	iv = internal_iv;
    }
    else
    {
	key = aeskey->key;
	iv = aeskey->iv;
    }
    BIO_set_cipher(cipher_bio, cipher, key, iv, 0);

    BIO_push(b64, membuf);
    BIO_push(cipher_bio, b64);

    if ((len = BIO_write(membuf, src, srclen)) <= 0)
    {
	while ((err = ERR_get_error()) != 0)
	{
	    mylog("err=%d\n", err);
	    mylog("    %s\n", ERR_error_string(err, errbuf));
	}
	exit(1);
    }
    if (len != srclen)
	return(-1);

    BIO_flush(membuf);

    readlen = 0;
    bufp = dst;
    while (dstlen > 0 && (len = BIO_read(cipher_bio, bufp, dstlen)) > 0)
    {
	bufp += len;
	readlen += len;
	dstlen -= len;
    }

    while ((err = ERR_get_error()) != 0)
    {
	mylog("err=%d\n", err);
	mylog("    %s\n", ERR_error_string(err, errbuf));
    }

    BIO_free_all(membuf);

    return(readlen);
}

/************************************************************************
 ********************             DECODE             ********************
 ************************************************************************/
int
encode(u_int8_t *src, u_int8_t *dst, int srclen, int dstlen,
    aes256_key_t *aeskey)
{
    int		len, writelen, err;
    char	errbuf[1024];
    u_int8_t	*bufp, *key, *iv;
    BIO		*b64, *membuf, *cipher_bio;
    const EVP_CIPHER	*cipher;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    membuf = BIO_new(BIO_s_mem());
    cipher = EVP_aes_256_cfb128();
    cipher_bio = BIO_new(BIO_f_cipher());
    if (aeskey == NULL)
    {
	key = internal_key;
	iv = internal_iv;
    }
    else
    {
	key = aeskey->key;
	iv = aeskey->iv;
    }
    BIO_set_cipher(cipher_bio, cipher, key, iv, 1);
    BIO_push(cipher_bio, b64);
    BIO_push(b64, membuf);

    if ((len = BIO_write(cipher_bio, src, srclen)) != srclen)
	return(-1);
    if (len <= 0)
    {
	while ((err = ERR_get_error()) != 0)
	{
	    mylog("err=%d\n", err);
	    mylog("    %s\n", ERR_error_string(err, errbuf));
	}
	exit(1);
    }

    BIO_flush(cipher_bio);

    writelen = 0;
    bufp = dst;
    while (dstlen > 0 && (len = BIO_read(membuf, bufp, dstlen)) > 0)
    {
	bufp += len;
	writelen += len;
	dstlen -= len;
    }

    BIO_free_all(cipher_bio);

    return(writelen);
}

/************************************************************************
 ********************           GEN_AESKEY           ********************
 ************************************************************************/
int
gen_aeskey(aes256_key_t *key)
{
    int		fd, len;

    if ((fd = open("/dev/urandom", O_RDONLY)) < 0)
	return(-1);
    if ((len = read(fd, key->key, KEYLEN)) < 0)
	return(-1);
    if (len != KEYLEN)
	return(-1);

    if ((len = read(fd, key->iv, IVLEN)) < 0)
	return(-1);
    if (len != IVLEN)
	return(-1);

    return(0);
}

/************************************************************************
 ********************           STORE_KEY            ********************
 ************************************************************************/
int
store_key(char *fname, aes256_key_t *key)
{
    FILE	*fp;
    int		len;
    char	encoded[1024];

    if ((fp = fopen(fname, "w")) == NULL)
	return(-1);
    if (chmod(fname, 0400) < 0)
	return(-1);

    len = encode(key->key, (u_int8_t *)encoded, KEYLEN, sizeof(encoded), NULL);
    encoded[len] = '\0';
    fprintf(fp, "%s\n", encoded);

    len = encode(key->iv, (u_int8_t *)encoded, IVLEN, sizeof(encoded), NULL);
    encoded[len] = '\0';
    fprintf(fp, "%s\n", encoded);

    fclose(fp);

    return(0);
}

/************************************************************************
 ********************            LOAD_KEY            ********************
 ************************************************************************/
int
load_key(char *fname, aes256_key_t *key)
{
    FILE	*fp;
    int		len, linelen;
    char	line[1024];

    if ((fp = fopen(fname, "r")) == NULL)
	return(-1);

    line[sizeof(line)-1] = '\0';
    if (fgets(line, sizeof(line)-1, fp) == NULL)
	return(-1);
    linelen = strlen(line);
    line[--linelen] = '\0';
    len = decode((u_int8_t *)line, key->key, linelen, KEYLEN, NULL);
    if (len != KEYLEN)
    {
	fclose(fp);
	return(-1);
    }

    if (fgets(line, sizeof(line)-1, fp) == NULL)
	return(-1);

    fclose(fp);
    linelen = strlen(line);
    line[--linelen] = '\0';
    len = decode((u_int8_t *)line, key->iv, linelen, IVLEN, NULL);
    if (len != IVLEN)
	return(-1);

    return(0);
}
