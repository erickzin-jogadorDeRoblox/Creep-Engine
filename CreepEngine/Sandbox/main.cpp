#include "../CreepEngine/include/Engine/Engine.h"
#include <iostream>

class MeuJogo : public Creep::Game
{
public:
    void OnStart() override {
        std::cout << "hello world";
    }
    //void OnUpdate(Creep::i32 dt) override {}

    void OnShutdown() override {
        std::cout << "Bye!";
        system("pause");
    }
};

int main()
{
    MeuJogo Game;

    return Creep::Run(Game);
}