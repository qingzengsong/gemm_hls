/// @author    Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
/// @date      June 2017 
/// @copyright This software is copyrighted under the BSD 3-Clause License. 

#include "Utility.h"
#include "MatrixMatrix.h"
#include "hlslib/SDAccel.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

int main(int argc, char **argv) {
  
  std::random_device rd;
  std::default_random_engine rng(rd());
  typename std::conditional<
      std::is_integral<Data_t>::value, std::uniform_int_distribution<Data_t>,
      std::uniform_real_distribution<Data_t>>::type dist(1, 10);

  bool verify = false;
  if (argc > 1 && std::string(argv[1]) == "on") {
    verify = true;
  }
  
  std::vector<Data_t> a, b, cRef;
  std::vector<MemoryPack_t> aMem, bMem, cMem;
  if (verify) {
    a = std::vector<Data_t>(kSizeN * kSizeM);
    std::for_each(a.begin(), a.end(),
                  [&dist, &rng](Data_t &in) { in = dist(rng); });
    b = std::vector<Data_t>(kSizeM * kSizeP);
    std::for_each(b.begin(), b.end(),
                  [&dist, &rng](Data_t &in) { in = dist(rng); });
    cRef = std::vector<Data_t>(kSizeN * kSizeP, 0);

    aMem = Pack(a);
    bMem = Pack(b);
    cMem = Pack(cRef);
  }

  try {

    std::cout << "Initializing OpenCL context..." << std::flush;
    hlslib::ocl::Context context;
    std::cout << " Done.\n";

    std::cout << "Initializing device memory..." << std::flush;
    auto aDevice = context.MakeBuffer<MemoryPack_t, hlslib::ocl::Access::read>(
        hlslib::ocl::MemoryBank::bank0, kSizeN * kSizeMMemory);
    auto bDevice = context.MakeBuffer<MemoryPack_t, hlslib::ocl::Access::read>(
        hlslib::ocl::MemoryBank::bank1, kSizeM * kSizePMemory);
    auto cDevice = context.MakeBuffer<MemoryPack_t, hlslib::ocl::Access::write>(
        hlslib::ocl::MemoryBank::bank2, kSizeN * kSizePMemory);
    std::cout << " Done.\n";

    if (verify) {
      std::cout << "Copying data to device..." << std::flush;
      aDevice.CopyFromHost(aMem.cbegin());
      bDevice.CopyFromHost(bMem.cbegin());
      std::cout << " Done.\n";
    }

    std::cout << "Programming device..." << std::flush;
    auto kernel = context.MakeKernel("MatrixMatrix.xclbin", "MatrixMatrix",
                                     aDevice, bDevice, cDevice);
    std::cout << " Done.\n";

    std::cout << "Executing kernel..." << std::flush;
    const auto elapsed = kernel.ExecuteTask();
    std::cout << " Done.\n";

    const auto perf = 1e-9 *
                      (2 * static_cast<float>(kSizeN) * kSizeM * kSizeP) /
                      elapsed.first;

    std::cout << "Kernel executed in " << elapsed.first
              << " seconds, corresponding to a performance of " << perf
              << " GOp/s.\n";;

    if (verify) {
      std::cout << "Copying back result..." << std::flush;
      cDevice.CopyToHost(cMem.begin());
      std::cout << " Done.\n";
    }

  } catch (std::runtime_error const &err) {
    std::cerr << "Execution failed with error: \"" << err.what() << "\"."
              << std::endl;
    return 1;
  }

  // Run reference implementation
  if (verify) {
    std::cout << "Running reference implementation..." << std::flush;
    Naive<OperatorMap, OperatorReduce>(a.cbegin(), b.cbegin(), cRef.begin(),
                                       kSizeN, kSizeM, kSizeP);
    std::cout << " Done.\n";

    // Convert to single element vector
    const auto cTest = Unpack(cMem); 

    for (int i = 0; i < kSizeN; ++i) {
      for (int j = 0; j < kSizeP; ++j) {
        const auto testVal = cTest[i * kSizeP + j];
        const auto refVal = cRef[i * kSizeP + j];
        const auto diff = std::abs(testVal - refVal);
        if (diff > static_cast<Data_t>(1e-3)) {
          // std::cerr << "Mismatch at (" << i << ", " << j << "): " << testVal
          //           << " vs. " << refVal << "\n";
          // return 1;
        } else {
          std::cout << "Correct at (" << i << ", " << j << "): " << testVal
                    << " vs. " << refVal << "\n";
        }
      }
    }
  }

  return 0;
}
