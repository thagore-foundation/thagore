#pragma once

namespace thagc::driver {

struct EmbeddedStdlibFile {
  const char* relative_path;
  const unsigned char* data;
  unsigned int size;
};

extern const EmbeddedStdlibFile kEmbeddedStdlibFiles[];
extern const unsigned int kEmbeddedStdlibFileCount;

}  // namespace thagc::driver

