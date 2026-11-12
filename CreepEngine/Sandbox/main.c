#include <stdio.h>

// Contador estático de frames do script
static int g_frameCount = 0;

void OnStart() {
    printf("[Creep Engine] Script C inicializado com sucesso!\n");
    g_frameCount = 0;
}

void OnUpdate() {
    g_frameCount++;

    // Processa dados na GPU a cada frame usando a automação de HLSL da engine
    // A engine já injeta o buffer e as numthreads por trás dos panos!
    Script_DispatchCompute(64, "OutputData[DTid.x] = (float)(DTid.x * 2.5f);");

    printf("[Frame %d] Computacao na GPU executada com sucesso.\n", g_frameCount);

    // Condição de parada: Atingiu o frame 60, chama o encerramento integrado
    if (g_frameCount >= 60) {
        printf("[Creep Engine] Frame 60 atingido. Chamando OnShutdown...\n");
        OnShutdown();
    }
}

void OnRender() {
    // Opcional para desenho
}

void OnShutdown() {
    printf("[Creep Engine] Jogo encerrado pelo script com seguranca.\n");
}