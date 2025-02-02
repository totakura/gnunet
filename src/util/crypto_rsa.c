/*
  This file is part of GNUnet
  Copyright (C) 2014 Christian Grothoff (and other contributing authors)

  GNUnet is free software; you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  GNUnet is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  GNUnet; see the file COPYING.  If not, If not, see <http://www.gnu.org/licenses/>
*/

/**
 * @file util/crypto_rsa.c
 * @brief Chaum-style Blind signatures based on RSA
 * @author Sree Harsha Totakura <sreeharsha@totakura.in>
 * @author Christian Grothoff
 */
#include "platform.h"
#include <gcrypt.h>
#include "gnunet_crypto_lib.h"

#define LOG(kind,...) GNUNET_log_from (kind, "util", __VA_ARGS__)


/**
 * The private information of an RSA key pair.
 */
struct GNUNET_CRYPTO_rsa_PrivateKey
{
  /**
   * Libgcrypt S-expression for the RSA private key.
   */
  gcry_sexp_t sexp;
};


/**
 * The public information of an RSA key pair.
 */
struct GNUNET_CRYPTO_rsa_PublicKey
{
  /**
   * Libgcrypt S-expression for the RSA public key.
   */
  gcry_sexp_t sexp;
};


/**
 * @brief an RSA signature
 */
struct GNUNET_CRYPTO_rsa_Signature
{
  /**
   * Libgcrypt S-expression for the RSA signature.
   */
  gcry_sexp_t sexp;
};


/**
 * @brief RSA blinding key
 */
struct GNUNET_CRYPTO_rsa_BlindingKey
{
  /**
   * Random value used for blinding.
   */
  gcry_mpi_t r;
};


/**
 * Extract values from an S-expression.
 *
 * @param array where to store the result(s)
 * @param sexp S-expression to parse
 * @param topname top-level name in the S-expression that is of interest
 * @param elems names of the elements to extract
 * @return 0 on success
 */
static int
key_from_sexp (gcry_mpi_t *array,
               gcry_sexp_t sexp,
               const char *topname,
               const char *elems)
{
  gcry_sexp_t list;
  gcry_sexp_t l2;
  const char *s;
  unsigned int i;
  unsigned int idx;

  if (! (list = gcry_sexp_find_token (sexp, topname, 0)))
    return 1;
  l2 = gcry_sexp_cadr (list);
  gcry_sexp_release (list);
  list = l2;
  if (! list)
    return 2;
  idx = 0;
  for (s = elems; *s; s++, idx++)
  {
    if (! (l2 = gcry_sexp_find_token (list, s, 1)))
    {
      for (i = 0; i < idx; i++)
      {
        gcry_free (array[i]);
        array[i] = NULL;
      }
      gcry_sexp_release (list);
      return 3;                 /* required parameter not found */
    }
    array[idx] = gcry_sexp_nth_mpi (l2, 1, GCRYMPI_FMT_USG);
    gcry_sexp_release (l2);
    if (! array[idx])
    {
      for (i = 0; i < idx; i++)
      {
        gcry_free (array[i]);
        array[i] = NULL;
      }
      gcry_sexp_release (list);
      return 4;                 /* required parameter is invalid */
    }
  }
  gcry_sexp_release (list);
  return 0;
}


/**
 * Create a new private key. Caller must free return value.
 *
 * @param len length of the key in bits (i.e. 2048)
 * @return fresh private key
 */
struct GNUNET_CRYPTO_rsa_PrivateKey *
GNUNET_CRYPTO_rsa_private_key_create (unsigned int len)
{
  struct GNUNET_CRYPTO_rsa_PrivateKey *ret;
  gcry_sexp_t s_key;
  gcry_sexp_t s_keyparam;

  GNUNET_assert (0 ==
                 gcry_sexp_build (&s_keyparam,
                                  NULL,
                                  "(genkey(rsa(nbits %d)))",
                                  len));
  GNUNET_assert (0 ==
                 gcry_pk_genkey (&s_key,
                                 s_keyparam));
  gcry_sexp_release (s_keyparam);
#if EXTRA_CHECKS
  GNUNET_assert (0 ==
                 gcry_pk_testkey (s_key));
#endif
  ret = GNUNET_new (struct GNUNET_CRYPTO_rsa_PrivateKey);
  ret->sexp = s_key;
  return ret;
}


/**
 * Free memory occupied by the private key.
 *
 * @param key pointer to the memory to free
 */
void
GNUNET_CRYPTO_rsa_private_key_free (struct GNUNET_CRYPTO_rsa_PrivateKey *key)
{
  gcry_sexp_release (key->sexp);
  GNUNET_free (key);
}


/**
 * Encode the private key in a format suitable for
 * storing it into a file.
 *
 * @param key the private key
 * @param[out] buffer set to a buffer with the encoded key
 * @return size of memory allocated in @a buffer
 */
size_t
GNUNET_CRYPTO_rsa_private_key_encode (const struct GNUNET_CRYPTO_rsa_PrivateKey *key,
                                      char **buffer)
{
  size_t n;
  char *b;

  n = gcry_sexp_sprint (key->sexp,
                        GCRYSEXP_FMT_DEFAULT,
                        NULL,
                        0);
  b = GNUNET_malloc (n);
  GNUNET_assert ((n - 1) ==     /* since the last byte is \0 */
                 gcry_sexp_sprint (key->sexp,
                                   GCRYSEXP_FMT_DEFAULT,
                                   b,
                                   n));
  *buffer = b;
  return n;
}


/**
 * Decode the private key from the data-format back
 * to the "normal", internal format.
 *
 * @param buf the buffer where the private key data is stored
 * @param len the length of the data in @a buf
 * @return NULL on error
 */
struct GNUNET_CRYPTO_rsa_PrivateKey *
GNUNET_CRYPTO_rsa_private_key_decode (const char *buf,
                                      size_t len)
{
  struct GNUNET_CRYPTO_rsa_PrivateKey *key;
  key = GNUNET_new (struct GNUNET_CRYPTO_rsa_PrivateKey);
  if (0 !=
      gcry_sexp_new (&key->sexp,
                     buf,
                     len,
                     0))
  {
    LOG (GNUNET_ERROR_TYPE_WARNING,
         "Decoded private key is not valid\n");
    GNUNET_free (key);
    return NULL;
  }
  if (0 != gcry_pk_testkey (key->sexp))
  {
    LOG (GNUNET_ERROR_TYPE_WARNING,
         "Decoded private key is not valid\n");
    GNUNET_CRYPTO_rsa_private_key_free (key);
    return NULL;
  }
  return key;
}


/**
 * Extract the public key of the given private key.
 *
 * @param priv the private key
 * @retur NULL on error, otherwise the public key
 */
struct GNUNET_CRYPTO_rsa_PublicKey *
GNUNET_CRYPTO_rsa_private_key_get_public (const struct GNUNET_CRYPTO_rsa_PrivateKey *priv)
{
  struct GNUNET_CRYPTO_rsa_PublicKey *pub;
  gcry_mpi_t ne[2];
  int rc;
  gcry_sexp_t result;

  rc = key_from_sexp (ne, priv->sexp, "public-key", "ne");
  if (0 != rc)
    rc = key_from_sexp (ne, priv->sexp, "private-key", "ne");
  if (0 != rc)
    rc = key_from_sexp (ne, priv->sexp, "rsa", "ne");
  if (0 != rc)
  {
    GNUNET_break_op (0);
    return NULL;
  }
  rc = gcry_sexp_build (&result,
                        NULL,
                        "(public-key(rsa(n %m)(e %m)))",
                        ne[0],
                        ne[1]);
  gcry_mpi_release (ne[0]);
  gcry_mpi_release (ne[1]);
  pub = GNUNET_new (struct GNUNET_CRYPTO_rsa_PublicKey);
  pub->sexp = result;
  return pub;
}


/**
 * Free memory occupied by the public key.
 *
 * @param key pointer to the memory to free
 */
void
GNUNET_CRYPTO_rsa_public_key_free (struct GNUNET_CRYPTO_rsa_PublicKey *key)
{
  gcry_sexp_release (key->sexp);
  GNUNET_free (key);
}


/**
 * Encode the public key in a format suitable for
 * storing it into a file.
 *
 * @param key the private key
 * @param[out] buffer set to a buffer with the encoded key
 * @return size of memory allocated in @a buffer
 */
size_t
GNUNET_CRYPTO_rsa_public_key_encode (const struct GNUNET_CRYPTO_rsa_PublicKey *key,
                                     char **buffer)
{
  size_t n;
  char *b;

  n = gcry_sexp_sprint (key->sexp,
                        GCRYSEXP_FMT_ADVANCED,
                        NULL,
                        0);
  b = GNUNET_malloc (n);
  GNUNET_assert ((n -1) ==      /* since the last byte is \0 */
                 gcry_sexp_sprint (key->sexp,
                                   GCRYSEXP_FMT_ADVANCED,
                                   b,
                                   n));
  *buffer = b;
  return n;
}


/**
 * Compute hash over the public key.
 *
 * @param key public key to hash
 * @param hc where to store the hash code
 */
void
GNUNET_CRYPTO_rsa_public_key_hash (const struct GNUNET_CRYPTO_rsa_PublicKey *key,
                                   struct GNUNET_HashCode *hc)
{
  char *buf;
  size_t buf_size;

  buf_size = GNUNET_CRYPTO_rsa_public_key_encode (key,
                                                  &buf);
  GNUNET_CRYPTO_hash (buf,
                      buf_size,
                      hc);
  GNUNET_free (buf);
}


/**
 * Decode the public key from the data-format back
 * to the "normal", internal format.
 *
 * @param buf the buffer where the public key data is stored
 * @param len the length of the data in @a buf
 * @return NULL on error
 */
struct GNUNET_CRYPTO_rsa_PublicKey *
GNUNET_CRYPTO_rsa_public_key_decode (const char *buf,
                                     size_t len)
{
  struct GNUNET_CRYPTO_rsa_PublicKey *key;
  gcry_mpi_t n;
  int ret;

  key = GNUNET_new (struct GNUNET_CRYPTO_rsa_PublicKey);
  if (0 !=
      gcry_sexp_new (&key->sexp,
                     buf,
                     len,
                     0))
  {
    GNUNET_break_op (0);
    GNUNET_free (key);
    return NULL;
  }
  /* verify that this is an RSA public key */
  ret = key_from_sexp (&n, key->sexp, "public-key", "n");
  if (0 != ret)
    ret = key_from_sexp (&n, key->sexp, "rsa", "n");
  if (0 != ret)
  {
    /* this is no public RSA key */
    GNUNET_break (0);
    gcry_sexp_release (key->sexp);
    GNUNET_free (key);
    return NULL;
  }
  gcry_mpi_release (n);
  return key;
}


/**
 * Create a blinding key
 *
 * @param len length of the key in bits (i.e. 2048)
 * @return the newly created blinding key
 */
struct GNUNET_CRYPTO_rsa_BlindingKey *
GNUNET_CRYPTO_rsa_blinding_key_create (unsigned int len)
{
  struct GNUNET_CRYPTO_rsa_BlindingKey *blind;

  blind = GNUNET_new (struct GNUNET_CRYPTO_rsa_BlindingKey);
  blind->r = gcry_mpi_new (len);
  gcry_mpi_randomize (blind->r,
                      len,
                      GCRY_STRONG_RANDOM);
  return blind;
}


/**
 * Compare the values of two blinding keys.
 *
 * @param b1 one key
 * @param b2 the other key
 * @return 0 if the two are equal
 */
int
GNUNET_CRYPTO_rsa_blinding_key_cmp (struct GNUNET_CRYPTO_rsa_BlindingKey *b1,
				    struct GNUNET_CRYPTO_rsa_BlindingKey *b2)
{
  return gcry_mpi_cmp (b1->r,
		       b2->r);
}


/**
 * Compare the values of two signatures.
 *
 * @param s1 one signature
 * @param s2 the other signature
 * @return 0 if the two are equal
 */
int
GNUNET_CRYPTO_rsa_signature_cmp (struct GNUNET_CRYPTO_rsa_Signature *s1,
				 struct GNUNET_CRYPTO_rsa_Signature *s2)
{
  char *b1;
  char *b2;
  size_t z1;
  size_t z2;
  int ret;

  z1 = GNUNET_CRYPTO_rsa_signature_encode (s1,
					   &b1);
  z2 = GNUNET_CRYPTO_rsa_signature_encode (s2,
					   &b2);
  if (z1 != z2)
    ret = 1;
  else
    ret = memcmp (b1,
		  b2,
		  z1);
  GNUNET_free (b1);
  GNUNET_free (b2);
  return ret;
}


/**
 * Compare the values of two public keys.
 *
 * @param p1 one public key
 * @param p2 the other public key
 * @return 0 if the two are equal
 */
int
GNUNET_CRYPTO_rsa_public_key_cmp (struct GNUNET_CRYPTO_rsa_PublicKey *p1,
				  struct GNUNET_CRYPTO_rsa_PublicKey *p2)
{
  char *b1;
  char *b2;
  size_t z1;
  size_t z2;
  int ret;

  z1 = GNUNET_CRYPTO_rsa_public_key_encode (p1,
					    &b1);
  z2 = GNUNET_CRYPTO_rsa_public_key_encode (p2,
					    &b2);
  if (z1 != z2)
    ret = 1;
  else
    ret = memcmp (b1,
		  b2,
		  z1);
  GNUNET_free (b1);
  GNUNET_free (b2);
  return ret;
}


/**
 * Compare the values of two private keys.
 *
 * @param p1 one private key
 * @param p2 the other private key
 * @return 0 if the two are equal
 */
int
GNUNET_CRYPTO_rsa_private_key_cmp (struct GNUNET_CRYPTO_rsa_PrivateKey *p1,
                                   struct GNUNET_CRYPTO_rsa_PrivateKey *p2)
{
  char *b1;
  char *b2;
  size_t z1;
  size_t z2;
  int ret;

  z1 = GNUNET_CRYPTO_rsa_private_key_encode (p1,
					    &b1);
  z2 = GNUNET_CRYPTO_rsa_private_key_encode (p2,
					    &b2);
  if (z1 != z2)
    ret = 1;
  else
    ret = memcmp (b1,
		  b2,
		  z1);
  GNUNET_free (b1);
  GNUNET_free (b2);
  return ret;
}


/**
 * Obtain the length of the RSA key in bits.
 *
 * @param key the public key to introspect
 * @return length of the key in bits
 */
unsigned int
GNUNET_CRYPTO_rsa_public_key_len (const struct GNUNET_CRYPTO_rsa_PublicKey *key)
{
  gcry_mpi_t n;
  int ret;
  unsigned int rval;

  ret = key_from_sexp (&n, key->sexp, "rsa", "n");
  if (0 != ret)
  {
    /* this is no public RSA key */
    GNUNET_break (0);
    return 0;
  }
  rval = gcry_mpi_get_nbits (n);
  gcry_mpi_release (n);
  return rval;
}


/**
 * Destroy a blinding key
 *
 * @param bkey the blinding key to destroy
 */
void
GNUNET_CRYPTO_rsa_blinding_key_free (struct GNUNET_CRYPTO_rsa_BlindingKey *bkey)
{
  gcry_mpi_release (bkey->r);
  GNUNET_free (bkey);
}


/**
 * Encode the blinding key in a format suitable for
 * storing it into a file.
 *
 * @param bkey the blinding key
 * @param[out] buffer set to a buffer with the encoded key
 * @return size of memory allocated in @a buffer
 */
size_t
GNUNET_CRYPTO_rsa_blinding_key_encode (const struct GNUNET_CRYPTO_rsa_BlindingKey *bkey,
                                       char **buffer)
{
  size_t n;
  char *b;
  size_t rsize;

  gcry_mpi_print (GCRYMPI_FMT_USG,
                  NULL,
                  0,
                  &n,
                  bkey->r);
  b = GNUNET_malloc (n);
  GNUNET_assert (0 ==
                 gcry_mpi_print (GCRYMPI_FMT_USG,
                                 (unsigned char *) b,
                                 n,
                                 &rsize,
                                 bkey->r));
  *buffer = b;
  return n;
}


/**
 * Decode the blinding key from the data-format back
 * to the "normal", internal format.
 *
 * @param buf the buffer where the public key data is stored
 * @param len the length of the data in @a buf
 * @return NULL on error
 */
struct GNUNET_CRYPTO_rsa_BlindingKey *
GNUNET_CRYPTO_rsa_blinding_key_decode (const char *buf,
                                       size_t len)
{
  struct GNUNET_CRYPTO_rsa_BlindingKey *bkey;
  size_t rsize;

  bkey = GNUNET_new (struct GNUNET_CRYPTO_rsa_BlindingKey);
  if (0 !=
      gcry_mpi_scan (&bkey->r,
                     GCRYMPI_FMT_USG,
                     (const unsigned char *) buf,
                     len,
                     &rsize))
  {
    GNUNET_break_op (0);
    GNUNET_free (bkey);
    return NULL;
  }
  return bkey;
}


/**
 * Blinds the given message with the given blinding key
 *
 * @param hash hash of the message to sign
 * @param bkey the blinding key
 * @param pkey the public key of the signer
 * @param[out] buffer set to a buffer with the blinded message to be signed
 * @return number of bytes stored in @a buffer
 */
size_t
GNUNET_CRYPTO_rsa_blind (const struct GNUNET_HashCode *hash,
                         struct GNUNET_CRYPTO_rsa_BlindingKey *bkey,
                         struct GNUNET_CRYPTO_rsa_PublicKey *pkey,
                         char **buffer)
{
  gcry_mpi_t data;
  gcry_mpi_t ne[2];
  gcry_mpi_t r_e;
  gcry_mpi_t data_r_e;
  size_t rsize;
  size_t n;
  gcry_error_t rc;
  char *b;
  int ret;

  ret = key_from_sexp (ne, pkey->sexp, "public-key", "ne");
  if (0 != ret)
    ret = key_from_sexp (ne, pkey->sexp, "rsa", "ne");
  if (0 != ret)
  {
    GNUNET_break (0);
    *buffer = NULL;
    return 0;
  }
  if (0 != (rc = gcry_mpi_scan (&data,
                                GCRYMPI_FMT_USG,
                                (const unsigned char *) hash,
                                sizeof (struct GNUNET_HashCode),
                                &rsize)))
  {
    GNUNET_break (0);
    gcry_mpi_release (ne[0]);
    gcry_mpi_release (ne[1]);
    *buffer = NULL;
    return 0;
  }
  r_e = gcry_mpi_new (0);
  gcry_mpi_powm (r_e,
                 bkey->r,
                 ne[1],
                 ne[0]);
  data_r_e = gcry_mpi_new (0);
  gcry_mpi_mulm (data_r_e,
                 data,
                 r_e,
                 ne[0]);
  gcry_mpi_release (data);
  gcry_mpi_release (ne[0]);
  gcry_mpi_release (ne[1]);
  gcry_mpi_release (r_e);

  gcry_mpi_print (GCRYMPI_FMT_USG,
                  NULL,
                  0,
                  &n,
                  data_r_e);
  b = GNUNET_malloc (n);
  rc = gcry_mpi_print (GCRYMPI_FMT_USG,
                       (unsigned char *) b,
                       n,
                       &rsize,
                       data_r_e);
  gcry_mpi_release (data_r_e);
  *buffer = b;
  return n;
}


/**
 * Convert the data specified in the given purpose argument to an
 * S-expression suitable for signature operations.
 *
 * @param ptr pointer to the data to convert
 * @param size the size of the data
 * @return converted s-expression
 */
static gcry_sexp_t
data_to_sexp (const void *ptr, size_t size)
{
  gcry_mpi_t value;
  gcry_sexp_t data;

  value = NULL;
  data = NULL;
  GNUNET_assert (0 ==
                 gcry_mpi_scan (&value,
                                GCRYMPI_FMT_USG,
                                ptr,
                                size,
                                NULL));
  GNUNET_assert (0 ==
                 gcry_sexp_build (&data,
                                  NULL,
                                  "(data (flags raw) (value %M))",
                                  value));
  gcry_mpi_release (value);
  return data;
}


/**
 * Sign the given message.
 *
 * @param key private key to use for the signing
 * @param msg the message to sign
 * @param msg_len number of bytes in @a msg to sign
 * @return NULL on error, signature on success
 */
struct GNUNET_CRYPTO_rsa_Signature *
GNUNET_CRYPTO_rsa_sign (const struct GNUNET_CRYPTO_rsa_PrivateKey *key,
			const void *msg,
			size_t msg_len)
{
  struct GNUNET_CRYPTO_rsa_Signature *sig;
  struct GNUNET_CRYPTO_rsa_PublicKey *public_key;
  gcry_sexp_t result;
  gcry_sexp_t data;

  data = data_to_sexp (msg,
                       msg_len);
  if (0 !=
      gcry_pk_sign (&result,
                    data,
                    key->sexp))
  {
    GNUNET_break (0);
    return NULL;
  }

  /* verify signature (guards against Lenstra's attack with fault injection...) */
  public_key = GNUNET_CRYPTO_rsa_private_key_get_public (key);
  if (0 !=
      gcry_pk_verify (result,
                      data,
                      public_key->sexp))
  {
    GNUNET_break (0);
    GNUNET_CRYPTO_rsa_public_key_free (public_key);
    gcry_sexp_release (data);
    gcry_sexp_release (result);
    return NULL;
  }
  GNUNET_CRYPTO_rsa_public_key_free (public_key);

  /* return signature */
  gcry_sexp_release (data);
  sig = GNUNET_new (struct GNUNET_CRYPTO_rsa_Signature);
  sig->sexp = result;
  return sig;
}


/**
 * Free memory occupied by signature.
 *
 * @param sig memory to freee
 */
void
GNUNET_CRYPTO_rsa_signature_free (struct GNUNET_CRYPTO_rsa_Signature *sig)
{
  gcry_sexp_release (sig->sexp);
  GNUNET_free (sig);
}


/**
 * Encode the given signature in a format suitable for storing it into a file.
 *
 * @param sig the signature
 * @param[out] buffer set to a buffer with the encoded key
 * @return size of memory allocated in @a buffer
 */
size_t
GNUNET_CRYPTO_rsa_signature_encode (const struct GNUNET_CRYPTO_rsa_Signature *sig,
				    char **buffer)
{
  size_t n;
  char *b;

  n = gcry_sexp_sprint (sig->sexp,
                        GCRYSEXP_FMT_ADVANCED,
                        NULL,
                        0);
  b = GNUNET_malloc (n);
  GNUNET_assert ((n - 1) ==     /* since the last byte is \0 */
                 gcry_sexp_sprint (sig->sexp,
                                   GCRYSEXP_FMT_ADVANCED,
                                   b,
                                   n));
  *buffer = b;
  return n;
}


/**
 * Decode the signature from the data-format back to the "normal", internal
 * format.
 *
 * @param buf the buffer where the public key data is stored
 * @param len the length of the data in @a buf
 * @return NULL on error
 */
struct GNUNET_CRYPTO_rsa_Signature *
GNUNET_CRYPTO_rsa_signature_decode (const char *buf,
                                    size_t len)
{
  struct GNUNET_CRYPTO_rsa_Signature *sig;
  int ret;
  gcry_mpi_t s;

  sig = GNUNET_new (struct GNUNET_CRYPTO_rsa_Signature);
  if (0 !=
      gcry_sexp_new (&sig->sexp,
                     buf,
                     len,
                     0))
  {
    GNUNET_break_op (0);
    GNUNET_free (sig);
    return NULL;
  }
  /* verify that this is an RSA signature */
  ret = key_from_sexp (&s, sig->sexp, "sig-val", "s");
  if (0 != ret)
    ret = key_from_sexp (&s, sig->sexp, "rsa", "s");
  if (0 != ret)
  {
    /* this is no RSA Signature */
    GNUNET_break_op (0);
    gcry_sexp_release (sig->sexp);
    GNUNET_free (sig);
    return NULL;
  }
  gcry_mpi_release (s);
  return sig;
}


/**
 * Duplicate the given public key
 *
 * @param key the public key to duplicate
 * @return the duplicate key; NULL upon error
 */
struct GNUNET_CRYPTO_rsa_PublicKey *
GNUNET_CRYPTO_rsa_public_key_dup (const struct GNUNET_CRYPTO_rsa_PublicKey *key)
{
  struct GNUNET_CRYPTO_rsa_PublicKey *dup;
  gcry_sexp_t dup_sexp;
  size_t erroff;

  /* check if we really are exporting a public key */
  dup_sexp = gcry_sexp_find_token (key->sexp, "public-key", 0);
  GNUNET_assert (NULL != dup_sexp);
  gcry_sexp_release (dup_sexp);
  /* copy the sexp */
  GNUNET_assert (0 == gcry_sexp_build (&dup_sexp, &erroff, "%S", key->sexp));
  dup = GNUNET_new (struct GNUNET_CRYPTO_rsa_PublicKey);
  dup->sexp = dup_sexp;
  return dup;
}


/**
 * Unblind a blind-signed signature.  The signature should have been generated
 * with #GNUNET_CRYPTO_rsa_sign() using a hash that was blinded with
 * #GNUNET_CRYPTO_rsa_blind().
 *
 * @param sig the signature made on the blinded signature purpose
 * @param bkey the blinding key used to blind the signature purpose
 * @param pkey the public key of the signer
 * @return unblinded signature on success, NULL on error
 */
struct GNUNET_CRYPTO_rsa_Signature *
GNUNET_CRYPTO_rsa_unblind (struct GNUNET_CRYPTO_rsa_Signature *sig,
                           struct GNUNET_CRYPTO_rsa_BlindingKey *bkey,
                           struct GNUNET_CRYPTO_rsa_PublicKey *pkey)
{
  gcry_mpi_t n;
  gcry_mpi_t s;
  gcry_mpi_t r_inv;
  gcry_mpi_t ubsig;
  int ret;
  struct GNUNET_CRYPTO_rsa_Signature *sret;

  ret = key_from_sexp (&n, pkey->sexp, "public-key", "n");
  if (0 != ret)
    ret = key_from_sexp (&n, pkey->sexp, "rsa", "n");
  if (0 != ret)
  {
    GNUNET_break_op (0);
    return NULL;
  }
  ret = key_from_sexp (&s, sig->sexp, "sig-val", "s");
  if (0 != ret)
    ret = key_from_sexp (&s, sig->sexp, "rsa", "s");
  if (0 != ret)
  {
    gcry_mpi_release (n);
    GNUNET_break_op (0);
    return NULL;
  }
  r_inv = gcry_mpi_new (0);
  if (1 !=
      gcry_mpi_invm (r_inv,
                     bkey->r,
                     n))
  {
    GNUNET_break_op (0);
    gcry_mpi_release (n);
    gcry_mpi_release (r_inv);
    gcry_mpi_release (s);
    return NULL;
  }
  ubsig = gcry_mpi_new (0);
  gcry_mpi_mulm (ubsig, s, r_inv, n);
  gcry_mpi_release (n);
  gcry_mpi_release (r_inv);
  gcry_mpi_release (s);

  sret = GNUNET_new (struct GNUNET_CRYPTO_rsa_Signature);
  GNUNET_assert (0 ==
                 gcry_sexp_build (&sret->sexp,
                                  NULL,
                                  "(sig-val (rsa (s %M)))",
                                  ubsig));
  gcry_mpi_release (ubsig);
  return sret;
}


/**
 * Verify whether the given hash corresponds to the given signature and the
 * signature is valid with respect to the given public key.
 *
 * @param hash hash of the message to verify to match the @a sig
 * @param sig signature that is being validated
 * @param public_key public key of the signer
 * @returns #GNUNET_OK if ok, #GNUNET_SYSERR if invalid
 */
int
GNUNET_CRYPTO_rsa_verify (const struct GNUNET_HashCode *hash,
                          const struct GNUNET_CRYPTO_rsa_Signature *sig,
                          const struct GNUNET_CRYPTO_rsa_PublicKey *public_key)
{
  gcry_sexp_t data;
  int rc;

  data = data_to_sexp (hash,
                       sizeof (struct GNUNET_HashCode));
  rc = gcry_pk_verify (sig->sexp,
                       data,
                       public_key->sexp);
  gcry_sexp_release (data);
  if (0 != rc)
  {
    LOG (GNUNET_ERROR_TYPE_WARNING,
         _("RSA signature verification failed at %s:%d: %s\n"),
         __FILE__,
         __LINE__,
         gcry_strerror (rc));
    return GNUNET_SYSERR;
  }
  return GNUNET_OK;
}


/**
 * Duplicate the given private key
 *
 * @param key the private key to duplicate
 * @return the duplicate key; NULL upon error
 */
struct GNUNET_CRYPTO_rsa_PrivateKey *
GNUNET_CRYPTO_rsa_private_key_dup (const struct GNUNET_CRYPTO_rsa_PrivateKey *key)
{
  struct GNUNET_CRYPTO_rsa_PrivateKey *dup;
  gcry_sexp_t dup_sexp;
  size_t erroff;

  /* check if we really are exporting a private key */
  dup_sexp = gcry_sexp_find_token (key->sexp, "private-key", 0);
  GNUNET_assert (NULL != dup_sexp);
  gcry_sexp_release (dup_sexp);
  /* copy the sexp */
  GNUNET_assert (0 == gcry_sexp_build (&dup_sexp, &erroff, "%S", key->sexp));
  dup = GNUNET_new (struct GNUNET_CRYPTO_rsa_PrivateKey);
  dup->sexp = dup_sexp;
  return dup;
}


/**
 * Duplicate the given private key
 *
 * @param key the private key to duplicate
 * @return the duplicate key; NULL upon error
 */
struct GNUNET_CRYPTO_rsa_Signature *
GNUNET_CRYPTO_rsa_signature_dup (const struct GNUNET_CRYPTO_rsa_Signature *sig)
{
  struct GNUNET_CRYPTO_rsa_Signature *dup;
  gcry_sexp_t dup_sexp;
  size_t erroff;
  gcry_mpi_t s;
  int ret;

  /* verify that this is an RSA signature */
  ret = key_from_sexp (&s, sig->sexp, "sig-val", "s");
  if (0 != ret)
    ret = key_from_sexp (&s, sig->sexp, "rsa", "s");
  GNUNET_assert (0 == ret);
  gcry_mpi_release (s);
  /* copy the sexp */
  GNUNET_assert (0 == gcry_sexp_build (&dup_sexp, &erroff, "%S", sig->sexp));
  dup = GNUNET_new (struct GNUNET_CRYPTO_rsa_Signature);
  dup->sexp = dup_sexp;
  return dup;
}


/* end of util/rsa.c */
