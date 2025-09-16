#include "shadower/source/source.h"
#include <dlfcn.h>

/* Function used to load source module */
create_source_t load_source(const std::string filename)
{
  /* Open the shared library */
  void* handle = dlopen(filename.c_str(), RTLD_LAZY);
  if (!handle) {
    std::cerr << "Error loading module: " + filename + " - " + dlerror() << std::endl;
    return nullptr;
  }

  /* Load the create_exploit function from the shared library */
  auto create_source = reinterpret_cast<create_source_t>(dlsym(handle, "create_source"));
  if (!create_source) {
    std::cerr << "Error loading symbol 'create_source' from " + filename + ": " + dlerror() << std::endl;
    dlclose(handle);
    return nullptr;
  }
  return create_source;
}