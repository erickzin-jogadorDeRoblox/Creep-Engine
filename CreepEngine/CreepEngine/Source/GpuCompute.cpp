#include "../include/Engine/GpuCompute.h"

class GpuCompute {
private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

public:
    GpuCompute(ID3D11Device* device, ID3D11DeviceContext* context)
        : m_device(device), m_context(context) {
    }

    // Função principal solicitada, altamente flexível e genérica para qualquer tipo de dado
    template <typename T>
    void ExecInGPU(int num_threads, const std::string& hlsl_code, std::vector<T>& data_cpu) {
        if (num_threads <= 0 || data_cpu.empty()) return;

        UINT byte_size = static_cast<UINT>(data_cpu.size() * sizeof(T));

        // 1. Criar o Structured Buffer na GPU (com suporte a Leitura/Escrita - RWStructuredBuffer)
        ID3D11Buffer* gpu_buffer = CreateStructuredBuffer(byte_size, data_cpu.data());
        ID3D11UnorderedAccessView* uav = CreateUAV(gpu_buffer, data_cpu.size());
        const std::string& code = "RWStructuredBuffer<float> OutputData : register(u0); \r\n"
            "\r\n"
            "[numthreads(64, 1, 1)] \r\n" 
            "void main(uint3 DTid : SV_DispatchThreadID)\r\n"
            "{\r\n"
            + hlsl_code + "\r\n"
            "}\r\n";

        // 2. Compilar o Compute Shader em tempo de execução (JIT)
        ID3D11ComputeShader* compute_shader = CompileShader(code);

        // 3. Configurar os estágios e disparar o Compute Shader
        m_context->CSSetShader(compute_shader, nullptr, 0);
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

        // Calcula automaticamente os grupos de threads (ex: 64 threads por grupo)
        int threads_per_group = 64;
        int num_groups = (num_threads + threads_per_group - 1) / threads_per_group;

        // Executa na GPU!
        m_context->Dispatch(num_groups, 1, 1);

        // 4. Ler os dados de volta da GPU para a CPU (usando Staging Buffer seguro)
        ReadBackData(gpu_buffer, byte_size, data_cpu.data());

        // 5. Limpeza de recursos locais desta execução
        if (uav) uav->Release();
        if (gpu_buffer) gpu_buffer->Release();
        if (compute_shader) compute_shader->Release();
    }

private:
    ID3D11Buffer* CreateStructuredBuffer(UINT byte_size, void* initial_data) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = byte_size;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(float); // Ajustável conforme a struct

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = initial_data;

        ID3D11Buffer* buffer = nullptr;
        m_device->CreateBuffer(&desc, initial_data ? &initData : nullptr, &buffer);
        return buffer;
    }

    ID3D11UnorderedAccessView* CreateUAV(ID3D11Buffer* buffer, size_t element_count) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = static_cast<UINT>(element_count);

        ID3D11UnorderedAccessView* uav = nullptr;
        m_device->CreateUnorderedAccessView(buffer, &desc, &uav);
        return uav;
    }

    ID3D11ComputeShader* CompileShader(const std::string& code) {
        ID3DBlob* blob = nullptr;
        ID3DBlob* error_blob = nullptr;

        HRESULT hr = D3DCompile(
            code.c_str(), code.size(),
            nullptr, nullptr, nullptr,
            "main", "cs_5_0",
            0, 0, &blob, &error_blob
        );

        if (FAILED(hr)) {
            if (error_blob) {
                std::string err_msg = static_cast<char*>(error_blob->GetBufferPointer());
                error_blob->Release();
                throw std::runtime_error("Erro ao compilar HLSL: " + err_msg);
            }
        }

        ID3D11ComputeShader* shader = nullptr;
        m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
        blob->Release();
        return shader;
    }

    void ReadBackData(ID3D11Buffer* gpu_buffer, UINT byte_size, void* output_data) {
        // Cria um Staging Buffer para leitura segura na CPU
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = byte_size;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ID3D11Buffer* staging_buffer = nullptr;
        m_device->CreateBuffer(&desc, nullptr, &staging_buffer);

        // Copia os dados da VRAM para a RAM temporária de leitura
        m_context->CopyResource(staging_buffer, gpu_buffer);

        // Mapeia e copia para o vector do usuário
        D3D11_MAPPED_SUBRESOURCE mapped;
        m_context->Map(staging_buffer, 0, D3D11_MAP_READ, 0, &mapped);
        memcpy(output_data, mapped.pData, byte_size);
        m_context->Unmap(staging_buffer, 0);

        staging_buffer->Release();
    }
};