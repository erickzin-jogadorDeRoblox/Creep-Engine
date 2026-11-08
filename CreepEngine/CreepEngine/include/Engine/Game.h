#pragma once
#include "Types.h"

namespace Creep
{
    // O usuário herda desta classe e implementa o seu jogo.
    class Game
    {
    public:
        virtual ~Game() = default;

        virtual void OnStart() {}
        virtual void OnUpdate(Creep::i32 deltaTime) {}
        virtual void OnRender() {}
        virtual void OnShutdown() {}
    };
}