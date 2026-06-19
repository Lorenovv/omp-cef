/*
    omp-cef Demo Gamemode
    Platform: open.mp
    Author: Neeko
    Date: 2026
*/

#include <open.mp>
#include <cef>

#include "../../shared/cef_demo"

main()
{
    print("omp-cef demo gamemode");
    print(" by Neeko, 2026");
    print(" ");
}

public OnGameModeInit()
{
    CefDemo_Initialize();
    return 1;
}