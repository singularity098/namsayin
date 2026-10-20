Khronos OpenGL headers (MIT licensed, see the notice at the top of each file).

MSVC has no glext.h. DPF downloads one at configure time, which is unreliable, so the build uses these copies instead
(see CMakeLists.txt). Copied from Mesa: GL_GLEXT_VERSION 20220530.
