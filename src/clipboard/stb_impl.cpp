// stb_image/stb_image_write are single-header libraries that need exactly
// one translation unit defining these macros before including them, to
// emit the actual implementation (every other file just gets declarations).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
