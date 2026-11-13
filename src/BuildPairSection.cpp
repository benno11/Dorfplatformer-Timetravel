#include "BuildPairIntegrity.h"
// Reserved, read-only PE data. The build signer fills this after both links.
// Exporting it keeps /OPT:REF and --gc-sections from removing the DLL slot.
#if defined(_MSC_VER)
#pragma section(".dfpair", read)
#define DF_PAIR_SECTION __declspec(allocate(".dfpair"))
#else
#define DF_PAIR_SECTION __attribute__((section(".dfpair"),used))
#endif
#if defined(PLATFORMER_GAME_LIBRARY) && PLATFORMER_GAME_LIBRARY
#define DF_PAIR_EXPORT __declspec(dllexport)
#else
#define DF_PAIR_EXPORT
#endif
extern "C" {
DF_PAIR_SECTION DF_PAIR_EXPORT extern const unsigned char dfBuildPairSeal[BuildPair::sealSize] = {'D','F','P','A','I','R','0','0'};
}
