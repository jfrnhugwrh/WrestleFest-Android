// Host stand-in for <android/asset_manager.h> (syntax checking only).
#ifndef WRESTLEFEST_HOST_STUB_ASSET_MANAGER
#define WRESTLEFEST_HOST_STUB_ASSET_MANAGER
#include <cstdint>
#include <sys/types.h>
struct AAssetManager;
struct AAsset;
enum { AASSET_MODE_UNKNOWN = 0, AASSET_MODE_RANDOM = 1, AASSET_MODE_STREAMING = 2,
       AASSET_MODE_BUFFER = 3 };
extern "C" {
AAsset *AAssetManager_open(AAssetManager *mgr, const char *filename, int mode);
int AAsset_read(AAsset *asset, void *buf, std::size_t count);
off64_t AAsset_seek64(AAsset *asset, off64_t offset, int whence);
off64_t AAsset_getLength64(AAsset *asset);
void AAsset_close(AAsset *asset);
}
#endif
