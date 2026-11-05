#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_MBEDTLS_PROJECT_CONFIG_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_MBEDTLS_PROJECT_CONFIG_H_

/* 本工程的 mbedTLS 裁剪配置（对应 -DMBEDTLS_CONFIG_FILE=...）。
 *
 * 只保留「TLS 1.2 客户端 + X.509 证书校验」这一条路径所需的最小特性集：
 * 高德天气接口只需要一次短小的 HTTPS GET，多余的算法、服务端支持、
 * 基于 socket 的传输层都会白白占用 Flash 与 RAM。
 *
 * 与 SDK 自带 pico_mbedtls_config.h 的区别在于本文件**不** #include
 * "mbedtls_config.h"：mbedTLS 3.6 已把该文件移入 include/mbedtls/ 子目录，
 * SDK 的转发写法在本机工具链上会直接找不到头文件。 */

/* 部分 mbedTLS 源文件使用 INT_MAX 却没有自行包含 limits.h。 */
#include <limits.h>

/* ------------------------------------------------------------------ */
/* 熵源：RP2040 通过 ROSC 采样提供硬件随机数，没有 /dev/urandom。 */
/* ------------------------------------------------------------------ */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* ------------------------------------------------------------------ */
/* 时间：RP2040 没有电池供电的 RTC，证书有效期校验必须由 NTP 注入的时间
 * 提供。PLATFORM_TIME_ALT 允许运行时用 mbedtls_platform_set_time 替换取
 * 时间函数，避免直接依赖 newlib 的 time()（上电后是 1970 年，会让所有证书
 * 都被判为「尚未生效」）。 */
/* ------------------------------------------------------------------ */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_TIME_ALT
/* PSA 的时间戳同样要走应用侧实现（mbedtls_ms_time），裸机上没有 POSIX
 * clock_gettime，不定义它 platform_util.c 会直接 #error。 */
#define MBEDTLS_PLATFORM_MS_TIME_ALT

/* ------------------------------------------------------------------ */
/* TLS 协议栈：只做客户端，只支持 TLS 1.2。 */
/* ------------------------------------------------------------------ */
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

/* 收发缓冲直接嵌入 ssl_context，是本模块最大的两块固定内存。响应体只有
 * 一两百字节的 JSON，2048 字节的发送缓冲已经足够放完整个请求头。 */
#define MBEDTLS_SSL_OUT_CONTENT_LEN 2048
#define MBEDTLS_SSL_IN_CONTENT_LEN 4096

/* ------------------------------------------------------------------ */
/* 密钥交换：高德证书链使用 ECDSA/RSA 签名，两者都要支持。 */
/* ------------------------------------------------------------------ */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED

/* ------------------------------------------------------------------ */
/* 对称算法 */
/* ------------------------------------------------------------------ */
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_GCM_C
/* 用更小的 AES 查表，换回约 9KB RAM；性能对每 30 分钟一次的请求无所谓。 */
#define MBEDTLS_AES_FEWER_TABLES

/* ------------------------------------------------------------------ */
/* 摘要算法 */
/* ------------------------------------------------------------------ */
#define MBEDTLS_MD_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SHA256_SMALLER

/* ------------------------------------------------------------------ */
/* 证书与公钥 */
/* ------------------------------------------------------------------ */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15

/* ------------------------------------------------------------------ */
/* 随机数生成器（TLS 的 nonce / 密钥材料都由它产出） */
/* ------------------------------------------------------------------ */
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C

/* 把错误码翻译成可读字符串，串口排障时省很多时间。 */
#define MBEDTLS_ERROR_C

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_MBEDTLS_PROJECT_CONFIG_H_ */
