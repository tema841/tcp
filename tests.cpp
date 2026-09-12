#include "sha256.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
int main(){std::ofstream f("test_sha.bin",std::ios::binary);f<<"abc";f.close();assert(crypto::file_sha256("test_sha.bin")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");std::remove("test_sha.bin");std::cout<<"All unit tests passed.\n";return 0;}
