// stb_image single-header implementation. Only compiled in when the stb
// headers are available (vcpkg "rendering" feature); NGE_HAS_STB is
// defined by engine/CMakeLists.txt on discovery.
#ifdef NGE_HAS_STB
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#endif
