#pragma once
#include <string>
#include <vector>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdexcept>

class GpuCompute {
public:
    GpuCompute(ID3D11Device* device, ID3D11DeviceContext* context);

    template <typename T>
    void ExecInGPU(int num_threads, const std::string& hlsl_code, std::vector<T>& data_cpu);
};