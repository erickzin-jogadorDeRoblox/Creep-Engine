#pragma once
#include "Game.h"
#include "../../Source/framework.h"
extern "C" {
// era pra ter um compilador aqui, só q a lib nn funciona
// #include <tcc.h>
}

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

    // Variaveis da Engine
    extern std::mutex g_scriptMutex;
    extern ScriptStage* g_currentScript;
    extern std::atomic<bool> IsRunning;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    // Funções da Engine

    static void InicializarGPU(ID3D11Device** outDevice, ID3D11DeviceContext** outContext) {
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            outDevice, nullptr, outContext
        );
    }

    
    static std::string LerArquivoScript(const std::string& caminho) {
        std::ifstream arquivo(caminho);
        if (!arquivo.is_open()) return "";
        std::stringstream buffer;
        buffer << arquivo.rdbuf();
        return buffer.str();
    }
    static ScriptStage* CompileCode(const std::string& code) {
        TCCState* novoTcc = tcc_new();
        if (!novoTcc) return nullptr;

        tcc_set_output_type(novoTcc, TCC_OUTPUT_MEMORY);

        if (tcc_compile_string(novoTcc, codigoC.c_str()) < 0) {
            tcc_delete(novoTcc);
            return nullptr;
        }

        if (tcc_relocate(novoTcc, TCC_RELOCATE_AUTO) < 0) {
            tcc_delete(novoTcc);
            return nullptr;
        }

        ScriptState* novoScript = new ScriptState();
        novoScript->tccContext = novoTcc;
        script.OnStart = (void (*)())tcc_get_symbol(script.tccContext, "OnStart");
        script.OnUpdate = (void (*)())tcc_get_symbol(script.tccContext, "OnUpdate");
        script.OnRender = (void (*)())tcc_get_symbol(script.tccContext, "OnRender");
        script.OnShutdown = (void (*)())tcc_get_symbol(script.tccContext, "OnShutdown");

        return novoScript;
    }
    static void ThreadMonitorDeHotReload(std::string caminhoArquivo) {
        auto ultimaModificacao = std::filesystem::last_write_time(caminhoArquivo);

        while (g_engineRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Checa a cada meio segundo

            try {
                if (!std::filesystem::exists(caminhoArquivo)) continue;
                auto modificacaoAtual = std::filesystem::last_write_time(caminhoArquivo);


                // Se o arquivo foi modificado pelo usuário no editor
                if (modificacaoAtual != ultimaModificacao) {
                    ultimaModificacao = modificacaoAtual;

                    // 1. Lê o novo código do arquivo
                    std::string novoCodigo = LerArquivoScript(caminhoArquivo);
                    if (novoCodigo.empty()) continue;

                    // 2. Compila PRIMEIRO (enquanto o jogo roda solto com o código antigo)
                    ScriptState* scriptNovo = CompilarNovoScript(novoCodigo);

                    if (scriptNovo != nullptr) {

                        ScriptState* scriptAntigo = nullptr;

                        // 3. Troca atômica e segura dos ponteiros
                        {
                            std::lock_guard<std::mutex> lock(g_scriptMutex);
                            scriptAntigo = g_currentScript; // Guarda o ponteiro antigo
                            g_currentScript = scriptNovo;  // Joga o novo na ativa
                        }

                        // Se houver uma função de reinicialização no novo script, chama
                        if (scriptNovo->OnStart) {
                            scriptNovo->OnStart();
                        }

                        // 4. Apaga o código antigo da memória de forma limpa
                        if (scriptAntigo) {
                            if (scriptAntigo->tccContext) {
                                tcc_delete(scriptAntigo->tccContext);
                            }
                            delete scriptAntigo;
                        }
                    }
                }
            }
            catch (...) {
                // Previne crash caso o arquivo esteja temporariamente bloqueado ao salvar
            }
        }
    }
    static bool Engine_Start(ScriptStage& script, const std::string& code) {

        // Cria o TCC
        script.tccContext = tcc_new();
        if (!script.tccContext) return false;

        tcc_set_output_type(script.tccContext, TCC_OUTPUT_MEMORY);

        // Compila a string de código do usuário na hora
        if (tcc_compile_string(script.tccContext, codigoC.c_str()) < 0) {
            tcc_delete(script.tccContext);
            return false;
        }

        // Reloca para a memória RAM se tornar executável
        if (tcc_relocate(script.tccContext, TCC_RELOCATE_AUTO) < 0) {
            tcc_delete(script.tccContext);
            return false;
        }

        // Pega os ponteiros das funções padrão do script 
        script.OnStart = (void (*)())tcc_get_symbol(script.tccContext, "OnStart");
        script.OnUpdate = (void (*)())tcc_get_symbol(script.tccContext, "OnUpdate");
        script.OnRender = (void (*)())tcc_get_symbol(script.tccContext, "OnRender");
        script.OnShutdown = (void (*)())tcc_get_symbol(script.tccContext, "OnShutdown");

        // Executa o Start do usuário imediatamente
        if (script.OnStart) {
            script.OnStart();
        }

        return true;
    }
    static void Engine_Shutdown(ScriptDoUsuario& script) {
        script.OnShutdown();
        if (script.tccContext) {
            tcc_delete(script.tccContext);
        }

        IsRunning = false;
        if (threadWatcher.joinable()) {
            threadWatcher.join();
        }

        {
            std::lock_guard<std::mutex> lock(g_scriptMutex);
            if (g_currentScript) {
                if (g_currentScript->tccContext) tcc_delete(g_currentScript->tccContext);
                delete g_currentScript;
            }
        }
    }
    static int Run(Game& game, const EngineConfig& config = EngineConfig{}) {
        std::string main = LerArquivoScript("../../Sandbox/main.cpp");
        std::thread threadWatcher(ThreadMonitorDeHotReload, main);

        ScriptDoUsuario Script;

        if (!Engine_Start(Script, main)) {
            return -1;
        }

        // --- LOOP PRINCIPAL DO JOGO/ENGINE ---
        bool IsRunning = true;
        int frameCount = 0;

        while (IsRunning) {
            // Roda a lógica da CPU compilada pelo TCC
            if (meuScript.OnUpdate) {
                meuScript.OnUpdate(Creep::i32);
            }
        }

        // --- FASE DE ENCERRAMENTO ---
        Engine_Shutdown(Script);

        return 0;
    }

}