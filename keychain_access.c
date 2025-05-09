/*
 * Copyright (C) 2008 Torsten Becker <torsten.becker@gmail.com>.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * keychain_access.c, created on 31-Oct-2008.
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>

void kca_print_handling_error(SecItemClass);
void kca_print_status_error(const char *, OSStatus);

/**
 *  @param p_password NULL here means no password.
 */
int kca_print_private_key(SecKeyRef p_keyItem, const char *p_password) {
  // SecKeychainItemFreeContent(); each time after a CopyContent

  // const CSSM_KEY *cssmKeyPtr;
  //
  // status = SecKeyGetCSSMKey(
  //    (SecKeyRef)itemRef, &cssmKeyPtr);
  //
  // printf("status: %d size: %lu data: %s size: %i\n",
  //     status, cssmKeyPtr->KeyData.Length, attrz[0].data,
  //     cssmKeyPtr->KeyHeader.LogicalKeySizeInBits);

  CFDataRef exportKey;

  if (p_password != NULL) {
    exportKey = CFDataCreate(kCFAllocatorDefault, (unsigned char *)p_password,
                             strlen(p_password));
  } else {
    exportKey = CFDataCreate(kCFAllocatorDefault, (unsigned char *)"12345", 5);
  }

  SecItemImportExportKeyParameters keyParams = {
      .flags = 0, // kSecKeySecurePassphrase
      .passphrase = exportKey,
      .version = SEC_KEY_IMPORT_EXPORT_PARAMS_VERSION,
  };

  CFDataRef exportedData;
  OSStatus status;

  status = SecItemExport(p_keyItem, kSecFormatWrappedPKCS8, kSecItemPemArmour,
                         &keyParams, &exportedData);

  if (status != errSecSuccess) {
    kca_print_status_error("Export error", status);
    return 1;
  }

  // If the user did set a password, just print the key
  if (p_password != NULL) {
    write(fileno(stdout), CFDataGetBytePtr(exportedData),
          CFDataGetLength(exportedData));

    return 0;
  }

  // It no password was given, use openssl to create a key with no password...

  int opensslPipe[2];
  if (pipe(opensslPipe) != 0) {
    perror("pipe(2) error");
    return 1;
  }

  FILE *fp;
  fp = fdopen(opensslPipe[0], "r");
  if (fp == NULL) {
    perror("fdopen(3) error");
    return 1;
  }

  ssize_t written;
  written = write(opensslPipe[1], CFDataGetBytePtr(exportedData),
                  CFDataGetLength(exportedData));

  if (written < CFDataGetLength(exportedData)) {
    perror("write(2) error");
    return 1;
  }

  // Close pipe, so OpenSSL sees an end
  close(opensslPipe[1]);

  // Init OpenSSL
  ERR_load_crypto_strings();
  OpenSSL_add_all_algorithms();

  // Read key through this pipe
  X509_SIG *p8;
  p8 = PEM_read_PKCS8(fp, NULL, NULL, NULL);

  // Try to decrypt
  PKCS8_PRIV_KEY_INFO *p8inf;
  p8inf = PKCS8_decrypt(p8, "12345", 5);

  X509_SIG_free(p8);

  EVP_PKEY *pkey;

  if (p8inf == NULL) {
    fprintf(stderr, "Error decrypting key\n");
    ERR_print_errors_fp(stderr);
    return 1;
  }

  if ((pkey = EVP_PKCS82PKEY(p8inf)) == NULL) {
    fprintf(stderr, "Error converting key\n");
    ERR_print_errors_fp(stderr);
    return 1;
  }

  PKCS8_PRIV_KEY_INFO_free(p8inf);

  PEM_write_PrivateKey(stdout, pkey, NULL, NULL, 0, NULL, NULL);

  return 0;
}

int kca_print_public_key(SecKeyRef p_keyItem) {
  CFDataRef exportedData = NULL;
  OSStatus status;

  SecItemImportExportKeyParameters keyParams = {
      .flags = 0,
      .version = SEC_KEY_IMPORT_EXPORT_PARAMS_VERSION,
  };

  status =
      SecItemExport(p_keyItem, 0, kSecItemPemArmour, &keyParams, &exportedData);

  if (status != errSecSuccess || exportedData == NULL) {
    kca_print_status_error("Exporting public key failed", status);
    return 1;
  }

  char *pemBytes = (char *)CFDataGetBytePtr(exportedData);

  // Search for the first newline to know where the key data starts
  char *firstNewLine = index(pemBytes, '\n');

  if (firstNewLine == NULL) {
    // This should not happen in practice, but just in case...
  reformat_panic:
    fprintf(stderr, "keychain_access: Panic while reformating pubkey.\n");
    return 1;
  }

  int beginDiff = firstNewLine - pemBytes;
  if (beginDiff < 0) {
    goto reformat_panic;
  }

  // Search for the end marker to know where the key data ends
  char *endMarker =
      strnstr(pemBytes, "\n-----END ", CFDataGetLength(exportedData));

  if (endMarker == NULL) {
    goto reformat_panic;
  }

  int endDiff = endMarker - pemBytes;
  if (endDiff < 0) {
    goto reformat_panic;
  }

  // Just print what is between the previous markers with 2 new markers around
  // them, this new markers are acutally compatible with openssl now.
  printf("-----BEGIN PUBLIC KEY-----");
  fflush(stdout);

  write(fileno(stdout), CFDataGetBytePtr(exportedData) + beginDiff,
        endDiff - beginDiff);

  puts("\n-----END PUBLIC KEY-----");

  return 0;
}

int kca_print_key(const char *keyName, const char *keyPassword) {
  char errorMessage[1024];
  int result = 0;

  CFStringRef attrLabel = CFStringCreateWithCString(
    kCFAllocatorDefault,
    keyName,
    kCFStringEncodingUTF8
  );
  if (attrLabel == NULL) {
    kca_print_status_error("CFStringCreateWithCString", 0);
    result = 1;
    goto cleanup_none;
  }

  CFDictionaryRef searchQuery = CFDictionaryCreate(
    kCFAllocatorDefault,
    (const void *[]){
      kSecClass,
      kSecMatchLimit,
      kSecAttrLabel,
    },
    (const void *[]){
      kSecClassKey,
      kSecMatchLimitOne,
      attrLabel,
    },
    3,
    &kCFTypeDictionaryKeyCallBacks,
    &kCFTypeDictionaryValueCallBacks
  );
  if (searchQuery == NULL) {
    kca_print_status_error("CFDictionaryCreate", 0);
    result = 1;
    goto cleanup_attr_label;
  }

  CFTypeRef searchResult = NULL;
  OSStatus status = SecItemCopyMatching(searchQuery, &searchResult);
  if (status != errSecSuccess) {
    snprintf(errorMessage, 1023, "Search for item named %s failed", keyName);
    kca_print_status_error(errorMessage, status);
    result = 1;
    goto cleanup_search_query;
  }

  if (CFGetTypeID(searchResult) != SecKeyGetTypeID()) {
    kca_print_status_error("CFGetTypeID", 0);
    result = 1;
    goto cleanup_search_result;
  }

  CFDictionaryRef keyAttributes = SecKeyCopyAttributes((SecKeyRef)searchResult);

  CFTypeRef attrKeyClass = NULL;
  if (!CFDictionaryGetValueIfPresent(keyAttributes, kSecAttrKeyClass, &attrKeyClass)) {
    kca_print_status_error("CFDictionaryGetValueIfPresent", 0);
    result = 1;
    goto cleanup_key_attributes;
  }

  if (attrKeyClass == kSecAttrKeyClassPrivate) {
    kca_print_private_key((SecKeyRef)searchResult, keyPassword);
  } else if (attrKeyClass == kSecAttrKeyClassPublic) {
    kca_print_public_key((SecKeyRef)searchResult);
  } else {
    kca_print_status_error("invalid type", 0);
    result = 1;
  }

  CFRelease(attrKeyClass);

cleanup_key_attributes:
  CFRelease(keyAttributes);

cleanup_search_result:
  CFRelease(searchResult);

cleanup_search_query:
  CFRelease(searchQuery);

cleanup_attr_label:
  CFRelease(attrLabel);

cleanup_none:
  return result;
}

void kca_print_help(FILE *p_fp, const char *p_arg0) {
  fprintf(
      p_fp,
      "Usage: %s [-vh] [-p <password>] <key_name>\n"
      "Options:\n"
      "  -p <password>   Encrypt exported private keys with <password>.\n"
      "                  The default is to export them without a password.\n"
      "  -h              Show this information.\n"
      "  -v              Print current version number.\n"
      "  <key_name>      The name of the keychain item you want to access.\n"
      "                  Has to be a public or private key.\n",
      p_arg0);
}

void kca_print_version(void) {
#ifndef KCA_VERSION
#define KCA_VERSION "v0"
#endif
#ifndef KCA_REV
#define KCA_REV "n/a"
#endif

  printf("keychain_access " KCA_VERSION " (" KCA_REV ")\n");
}

int main(int p_argc, char **p_argv) {
  int option;
  const char *keyPassword = NULL;

  // TODO:
  // -t for "type"
  // -a to limit to a certain attribute
  // -o to specify output format
  // --pem
  // -k keyname
  // -p pwname for searching a password

  const char *arg0 = "keychain_access";
  if (p_argc >= 1) {
    arg0 = p_argv[0];
  }

  while ((option = getopt(p_argc, p_argv, "vhp:")) != -1) {
    switch (option) {
    case 'h':
      kca_print_help(stdout, arg0);
      return 0;

    case 'v':
      kca_print_version();
      return 0;

    case 'p':
      keyPassword = optarg;
      break;

    case '?':
    default:
      kca_print_help(stderr, arg0);
      return 1;
    }
  }

  int argcAfter = p_argc - optind;
  char *keyName = *(p_argv + optind);

  if (argcAfter > 1) {
    fprintf(stderr, "%s: Too many key names given.\n", arg0);
    kca_print_help(stderr, arg0);
    return 1;
  }

  if (argcAfter < 1) {
    fprintf(stderr, "%s: Missing key name.\n", arg0);
    kca_print_help(stderr, arg0);
    return 1;
  }

  return kca_print_key(keyName, keyPassword);
}

void kca_print_handling_error(SecItemClass itemClass) {
  printf("Handling ");

  switch (itemClass) {
  case kSecInternetPasswordItemClass:
    printf("kSecInternetPasswordItemClass");
    break;
  case kSecGenericPasswordItemClass:
    printf("kSecGenericPasswordItemClass");
    break;
  case kSecCertificateItemClass:
    printf("kSecCertificateItemClass");
    break;
  case CSSM_DL_DB_RECORD_SYMMETRIC_KEY:
    printf("CSSM_DL_DB_RECORD_SYMMETRIC_KEY");
    break;
  case CSSM_DL_DB_RECORD_ALL_KEYS:
    printf("CSSM_DL_DB_RECORD_ALL_KEYS");
    break;
  case CSSM_DL_DB_RECORD_PUBLIC_KEY:
    printf("CSSM_DL_DB_RECORD_PUBLIC_KEY");
    break;
  case CSSM_DL_DB_RECORD_PRIVATE_KEY:
    printf("CSSM_DL_DB_RECORD_PRIVATE_KEY");
    break;
  default:
    printf("unknown item class (%u)", (unsigned int)itemClass);
  }

  printf(" is not yet implemented.\n");
}

void kca_print_status_error(const char *prefix, OSStatus status) {
  CFStringRef string = SecCopyErrorMessageString(status, NULL);
  char *bytes = malloc(sizeof(char) * 1024);

  if (CFStringGetCString(string, bytes, 1024, kCFStringEncodingUTF8) == true) {
    fprintf(stderr, "keychain_access: %s: (%d) %s\n", prefix, (int)status,
            bytes);
  } else {
    fprintf(stderr, "keychain_access: %s: %d\n", prefix, (int)status);
  }

  free(bytes);
  CFRelease(string);
}
