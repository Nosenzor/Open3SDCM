# Open3SDCM
An attempt to read unencrypted 3Shape DCM.

The project will be written in C++ with the first goal to make a cli command that can convert a DCM file into STL, OBJ or PLY files.
Then as an extension a python binding can be build.

## Goals of the project :

1. Read an extract the mesh(es) geometrical information (vertices coordinates and triangles) from DCM
2. Convert to STL, PLY, OBJ
3. Read colors applied on meshes
4. Read UV mapping and Texture
4. Read extra curves

# Not part of the goals (at least at the beginning)
* Write DCMs
* Read encrypted file
  


# How to build

Run the following from the repository root:

```bash
cmake -DCMAKE_BUILD_TYPE=Release --preset ninja-release-vcpkg -S . -B ./builds/ninja-release-vcpkg
cmake --build ./builds/ninja-release-vcpkg
```