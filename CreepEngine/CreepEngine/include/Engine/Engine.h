#pragma once

#include "Game.h"
#include "../../Source/framework.h"
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <d3d11.h>

// Forward declaration do TCC
struct TCCState;

namespace Creep
{
    struct EngineConfig
    {
        const char* title = "Creep Engine";
        i32  width = 1280;
        i32  height = 720;
        bool vsync = true;
        bool resizable = true;
        f32  fixedTimeStep = 1.0f / 60.0f;
    };

    struct ScriptStage
    {
        TCCState* tccContext = nullptr;
        void (*OnStart)() = nullptr;
        void (*OnUpdate)() = nullptr;
        void (*OnRender)() = nullptr;
        void (*OnShutdown)() = nullptr;
    };

    // Variáveis Globais da Engine (Declaradas como extern)
    extern std::mutex g_scriptMutex;
    extern ScriptStage* g_currentScript;
    extern std::atomic<bool> IsRunning;

    // Ponteiros globais do DirectX 11
    extern ID3D11Device* g_device;
    extern ID3D11DeviceContext* g_context;

    // Funções da Engine (Declaradas no .h, implementadas no .cpp)
    void InicializarGPU(ID3D11Device** outDevice, ID3D11DeviceContext** outContext);
    std::string LerArquivoScript(const std::string& caminho);
    ScriptStage* CompileCode(const std::string& code);
    void ThreadMonitorDeHotReload(std::string caminhoArquivo);
    bool Engine_Start(ScriptStage& script, const std::string& code);
    void Engine_Shutdown(ScriptStage& script);
    int Run(Game& game, const EngineConfig& config = EngineConfig{});
    template<typename T>
    void ExecInGpu(int num_threads, const std::string& hlsl_code, std::vector<T>& data_cpu);
}