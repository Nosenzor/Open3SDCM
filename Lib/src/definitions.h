#pragma once
#include <array>

namespace Open3SDCM
{
  struct Vertex
  {
    union
    {
      std::array<float, 3> data;
      struct
      {
        float x;
        float y;
        float z;
      };
    };

    // Constructors for convenience
    Vertex() : x(0.0f), y(0.0f), z(0.0f) {}
    Vertex(float x, float y, float z) : x(x), y(y), z(z) {}

    // Array-like access
    float& operator[](int index) { return data[index]; }
    const float& operator[](int index) const { return data[index]; }
  };

  struct Triangle
  {
    size_t v1{0};
    size_t v2{0};
    size_t v3{0};
  };
}// namespace Open3SDCM