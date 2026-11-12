#include "../include/Engine/Engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <windows.h>
#include "../include/Engine/GpuCompute.h"

// --- DECLARAÇÕES DINÂMICAS DA DLL DO TCC (Sem precisar do .h estático) ---
struct TCCState;

typedef TCCState* (*tcc_new_t)();
typedef void (*tcc_delete_t)(TCCState* s);
typedef int (*tcc_set_output_type_t)(TCCState* s, int output_type);
typedef int (*tcc_add_include_path_t)(TCCState* s, const char* pathname);
typedef int (*tcc_compile_string_t)(TCCState* s, const char* buf);
typedef int (*tcc_relocate_t)(TCCState* s, void* ptr);
typedef void* (*tcc_get_symbol_t)(TCCState* s, const char* name);

// Ponteiros globais da DLL
static HMODULE hTccDll = nullptr;
static tcc_new_t _tcc_new = nullptr;
static tcc_delete_t _tcc_delete = nullptr;
static tcc_set_output_type_t _tcc_set_output_type = nullptr;
static tcc_add_include_path_t _tcc_add_include_path = nullptr;
static tcc_compile_string_t _tcc_compile_string = nullptr;
static tcc_relocate_t _tcc_relocate = nullptr;
static tcc_get_symbol_t _tcc_get_symbol = nullptr;

namespace Creep
{
    // Definição das Variáveis Globais
    std::mutex g_scriptMutex;
    ScriptStage* g_currentScript = nullptr;
    std::atomic<bool> IsRunning(true);

    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;

    static std::thread g_threadWatcher;

    extern "C" {
        void Script_Log(const char* mensagem) {
            std::cout << "[LOG DO JOGO]: " << mensagem << "\n";
        }
        void Script_DispatchCompute(int numThreads, const char* hlslCode) {
            if (!g_device || !g_context) {
                std::cerr << "[GPU Compute] Erro: Dispositivo DirectX 11 nao inicializado!\n";
                return;
             }
            std::cout << "[GPU Compute] Despachando " << numThreads << " threads para a GPU...\n";
        }
     }
    

    // O "Prologue" injeta as assinaturas automaticamente no topo de todo script compilado
    std::string ObterCabecalhoPadrao() {
        return R"(
            void Script_DispatchCompute(int numThreads, const char* hlslCode);
            void Script_Log(const char* mensagem);
            
            void OnStart();
            void OnUpdate();
            void OnShutdown();
        )";
    }

    // Carrega a DLL do TCC em tempo de execução
    bool InicializarTccDll() {
        hTccDll = LoadLibraryW(L"libtcc.dll");
        if (!hTccDll) {
            std::cerr << "[Erro] Nao foi possivel encontrar a 'libtcc.dll' na pasta de saida do executavel!\n";
            return false;
        }

        _tcc_new = (tcc_new_t)GetProcAddress(hTccDll, "tcc_new");
        _tcc_delete = (tcc_delete_t)GetProcAddress(hTccDll, "tcc_delete");
        _tcc_set_output_type = (tcc_set_output_type_t)GetProcAddress(hTccDll, "tcc_set_output_type");
        _tcc_add_include_path = (tcc_add_include_path_t)GetProcAddress(hTccDll, "tcc_add_include_path");
        _tcc_compile_string = (tcc_compile_string_t)GetProcAddress(hTccDll, "tcc_compile_string");
        _tcc_relocate = (tcc_relocate_t)GetProcAddress(hTccDll, "tcc_relocate");
        _tcc_get_symbol = (tcc_get_symbol_t)GetProcAddress(hTccDll, "tcc_get_symbol");

        if (!_tcc_new || !_tcc_compile_string || !_tcc_relocate) {
            std::cerr << "[Erro] Falha ao mapear as funcoes dentro da 'libtcc.dll'!\n";
            return false;
        }

        std::cout << "[Engine] DLL do TCC carregada e mapeada com sucesso!\n";
        return true;
    }

    void DescarregarTccDll() {
        if (hTccDll) {
            FreeLibrary(hTccDll);
            hTccDll = nullptr;
        }
    }
    

    // Inicialização do DirectX 11
    void InicializarGPU(ID3D11Device** outDevice, ID3D11DeviceContext** outContext) {
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            outDevice, nullptr, outContext
        );
        if (FAILED(hr)) {
            std::cerr << "[Erro] Falha ao criar o dispositivo DirectX 11!\n";
        }
        else {
            std::cout << "[Engine] DirectX 11 inicializado com sucesso!\n";
        }
    }

    // Leitor de arquivos de script
    std::string LerArquivoScript(const std::string& caminho) {
        std::ifstream arquivo(caminho);
        if (!arquivo.is_open()) {
            std::cerr << "[Erro] Nao foi possivel abrir o arquivo: " << caminho << "\n";
            return "";
        }
        std::stringstream buffer;
        buffer << arquivo.rdbuf();
        return buffer.str();
    }

    // Compilação isolada para o Hot-Reload usando os ponteiros da DLL
    ScriptStage* CompileCode(const std::string& codigoDoUsuario) {
        TCCState* novoTcc = _tcc_new();
        if (!novoTcc) return nullptr;

        _tcc_set_output_type(novoTcc, 1); // TCC_OUTPUT_MEMORY

        // 1. Junta o cabeçalho padrão da engine com o código escrito pelo usuário
        std::string codigoCompleto = ObterCabecalhoPadrao() + "\n" + codigoDoUsuario;

        if (_tcc_compile_string(novoTcc, codigoCompleto.c_str()) < 0) {
            std::cerr << "[TCC] Erro de compilacao no script!\n";
            _tcc_delete(novoTcc);
            return nullptr;
        }

        // 2. IMPORTANTE: Antes de relocar, você precisa mapear os símbolos da C++ para o TCC
        _tcc_add_symbol(novoTcc, "Script_Log", (void*)Script_Log);
        _tcc_add_symbol(novoTcc, "Script_DispatchCompute", (void*)Script_DispatchCompute);

        if (_tcc_relocate(novoTcc, (void*)1) < 0) {
            std::cerr << "[TCC] Erro na relocacao!\n";
            _tcc_delete(novoTcc);
            return nullptr;
        }

        ScriptStage* novoScript = new ScriptStage();
        novoScript->tccContext = novoTcc;
        novoScript->OnStart = (void (*)())_tcc_get_symbol(novoTcc, "OnStart");
        novoScript->OnUpdate = (void (*)())_tcc_get_symbol(novoTcc, "OnUpdate");
        novoScript->OnShutdown = (void (*)())_tcc_get_symbol(novoTcc, "OnShutdown");

        return novoScript;
    }
}

    // Thread de monitoramento de alteração em disco (Hot-Reload)
    void ThreadMonitorDeHotReload(std::string caminhoArquivo) {
        if (!std::filesystem::exists(caminhoArquivo)) return;
        auto ultimaModificacao = std::filesystem::last_write_time(caminhoArquivo);

        while (IsRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            try {
                if (!std::filesystem::exists(caminhoArquivo)) continue;
                auto modificacaoAtual = std::filesystem::last_write_time(caminhoArquivo);

                if (modificacaoAtual != ultimaModificacao) {
                    ultimaModificacao = modificacaoAtual;
                    std::cout << "\n[Hot-Reload] Alteracao detectada no script! Recompilando...\n";

                    std::string novoCodigo = LerArquivoScript(caminhoArquivo);
                    if (novoCodigo.empty()) continue;

                    ScriptStage* scriptNovo = CompileCode(novoCodigo);

                    if (scriptNovo != nullptr) {
                        ScriptStage* scriptAntigo = nullptr;

                        {
                            std::lock_guard<std::mutex> lock(g_scriptMutex);
                            scriptAntigo = g_currentScript;
                            g_currentScript = scriptNovo;
                        }

                        if (scriptNovo->OnStart) {
                            scriptNovo->OnStart();
                        }

                        if (scriptAntigo) {
                            if (scriptAntigo->tccContext) {
                                _tcc_delete(scriptAntigo->tccContext);
                            }
                            delete scriptAntigo;
                            std::cout << "[Hot-Reload] Script atualizado com sucesso em tempo de execucao!\n";
                        }
                    }
                }
            }
            catch (...) {
                // Previne crash caso o arquivo esteja sendo salvo no momento exato
            }
        }
    }

    // Inicialização do Script (Start)
    bool Engine_Start(ScriptStage& script, const std::string& code) {
        script.tccContext = _tcc_new();
        if (!script.tccContext) return false;

        _tcc_set_output_type(script.tccContext, 1);

        if (_tcc_compile_string(script.tccContext, code.c_str()) < 0) {
            std::cerr << "[Erro] Falha ao compilar codigo inicial do script!\n";
            _tcc_delete(script.tccContext);
            return false;
        }

        if (_tcc_relocate(script.tccContext, (void*)1) < 0) {
            std::cerr << "[Erro] Falha na relocacao do TCC!\n";
            _tcc_delete(script.tccContext);
            return false;
        }

        script.OnStart = (void (*)())_tcc_get_symbol(script.tccContext, "OnStart");
        script.OnUpdate = (void (*)())_tcc_get_symbol(script.tccContext, "OnUpdate");
        script.OnRender = (void (*)())_tcc_get_symbol(script.tccContext, "OnRender");
        script.OnShutdown = (void (*)())_tcc_get_symbol(script.tccContext, "OnShutdown");

        if (script.OnStart) {
            script.OnStart();
        }

        return true;
    }

    // Encerramento da Engine
    void Engine_Shutdown(ScriptStage& script) {
        if (script.OnShutdown) {
            script.OnShutdown();
        }

        if (script.tccContext) {
            _tcc_delete(script.tccContext);
        }

        IsRunning = false;
        if (g_threadWatcher.joinable()) {
            g_threadWatcher.join();
        }

        std::lock_guard<std::mutex> lock(g_scriptMutex);
        if (g_currentScript && g_currentScript != &script) {
            if (g_currentScript->tccContext) {
                _tcc_delete(g_currentScript->tccContext);
            }
            delete g_currentScript;
            g_currentScript = nullptr;
        }

        DescarregarTccDll();
    }

    // Função principal de Execução da Engine (Run)
    int Run(Game& game, const EngineConfig& config) {
        // 1. Carrega a DLL do TCC dinamicamente
        if (!InicializarTccDll()) {
            return -1;
        }

        // 2. Inicializa o DirectX 11
        InicializarGPU(&g_device, &g_context);

        // 3. Carrega o script inicial do usuário
        std::string caminhoScript = "../../Sandbox/main.c";
        std::string codigoInicial = LerArquivoScript(caminhoScript);
        if (codigoInicial.empty()) {
            std::cerr << "[Erro] Nao foi possivel carregar o script em: " << caminhoScript << "\n";
            DescarregarTccDll();
            return -1;
        }

        ScriptStage scriptPrincipal;
        if (!Engine_Start(scriptPrincipal, codigoInicial)) {
            DescarregarTccDll();
            return -1;
        }

        g_currentScript = &scriptPrincipal;

        // 4. Dispara a thread de Hot-Reload
        g_threadWatcher = std::thread(ThreadMonitorDeHotReload, caminhoScript);

        std::cout << "[Creep Engine] Rodando loop principal...\n";

        // --- LOOP PRINCIPAL ---
        while (IsRunning) {
            // Executa a lógica atual com proteção de thread
            {
                std::lock_guard<std::mutex> lock(g_scriptMutex);
                if (g_currentScript && g_currentScript->OnUpdate) {
                    g_currentScript->OnUpdate();
                }
            }
        }

        // --- ENCERRAMENTO ---
        Engine_Shutdown(scriptPrincipal);
        std::cout << "[Creep Engine] Desligada com sucesso.\n";

        return 0;
    }
}