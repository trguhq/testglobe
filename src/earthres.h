/* Earth chunk format and resource IDs shared by C, Win16 RC, and Mac Rez.
 * Changing the chunk size also requires resplitting the binary maps and
 * updating the generator/resource chunk tables. Generated C checks its format. */
#ifndef earthres_h
#define earthres_h
#define TG_EARTH_CHUNK_SIZE 16384UL
#define TG_EARTH_32_ID 1000
#define TG_EARTH_64_ID 1010
#define TG_EARTH_128_ID 1020
#define TG_EARTH_256_ID 1030
#define TG_EARTH_256_1_ID 1031
#define TG_EARTH_512_ID 1040
#define TG_EARTH_512_1_ID 1041
#define TG_EARTH_512_2_ID 1042
#define TG_EARTH_512_3_ID 1043
#define TG_EARTH_512_4_ID 1044
#define TG_EARTH_512_5_ID 1045
#define TG_EARTH_512_6_ID 1046
#define TG_EARTH_512_7_ID 1047
#endif
