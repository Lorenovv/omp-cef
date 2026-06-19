/*
    omp-cef Demo Gamemode
    Platform: SA-MP
    Author: Neeko
    Date: 2026
*/

#include <a_samp>
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