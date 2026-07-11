// cgltf single-header implementation. Only compiled in when the cgltf
// header is available (vcpkg "rendering" feature); NGE_HAS_CGLTF is
// defined by engine/CMakeLists.txt on discovery.
#ifdef NGE_HAS_CGLTF
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#endif
