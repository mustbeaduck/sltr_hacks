#include "stdio.h"
#include "Windows.h"
#include "winuser.h"
#include "string.h"
#include "stdint.h"
#include "stdlib.h"
#include "stdbool.h"
#include "inttypes.h"
#include <d3dx9core.h>
#include <wchar.h>

typedef unsigned int uint;

LPD3DXFONT g_Directx_Font;
HINSTANCE dllHandle = NULL;
HRESULT (__stdcall * ogEndScene)(LPDIRECT3DDEVICE9 pDevice) = NULL;

const wchar_t * CARD_NAMES[] = {L"2♣", L"3♣", L"4♣", L"5♣", L"6♣", L"7♣", L"8♣", L"9♣", L"10♣", L"J♣", L"Q♣", L"K♣", L"A♣", L"2♦", L"3♦", L"4♦", L"5♦",
					 L"6♦", L"7♦", L"8♦", L"9♦", L"10♦", L"J♦", L"Q♦", L"K♦", L"A♦", L"2♠", L"3♠", L"4♠", L"5♠", L"6♠", L"7♠", L"8♠", L"9♠",
					 L"10♠", L"J♠", L"Q♠", L"K♠", L"A♠", L"2♥", L"3♥", L"4♥", L"5♥", L"6♥", L"7♥", L"8♥", L"9♥", L"10♥", L"J♥", L"Q♥", L"K♥", L"A♥"};

typedef struct Mark {
    uint x;
    uint y;
    uint value;
    uint isRed;
} Mark;

uint cardCount = 0;
Mark CARDS[52]; 

void markACard(int x, int y, D3DCOLOR Colour, LPD3DXFONT m_font, int ind)
{
    RECT Position = {x,y,0,0};
    m_font->lpVtbl->DrawTextW(m_font, 0, CARD_NAMES[ind], 2, &Position, DT_NOCLIP, Colour);
}

void drawTheMarks(LPDIRECT3DDEVICE9 pDevice)
{	
    for ( int i = 0; i < cardCount; i+=1 ) {

        if ( CARDS[i].isRed ) {
            markACard(CARDS[i].x, CARDS[i].y, D3DCOLOR_XRGB(255, 77, 77), g_Directx_Font, CARDS[i].value);
        } else {
            markACard(CARDS[i].x, CARDS[i].y, D3DCOLOR_XRGB(0, 0, 0), g_Directx_Font, CARDS[i].value);
        }
    }
}				  

__stdcall HRESULT endSceneWrapper(LPDIRECT3DDEVICE9 pDevice)
{
    /* using same font crashes the game on window resize (:
    the proper way to do it would be catching resize event and updating a font there
    + u could properly rescale the marks to new coordinates but im lazy*/
    D3DXCreateFontA(pDevice, 16, 0, 0, 0, 0, DEFAULT_CHARSET, OUT_CHARACTER_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Consolas", &g_Directx_Font);

    drawTheMarks(pDevice);

    g_Directx_Font->lpVtbl->Release(g_Directx_Font);
    return ogEndScene(pDevice);
}


static void getThemCards(uintptr_t baseAddr)
{				     
    uintptr_t tableStacks = *( uintptr_t * ) (baseAddr + 0xbafa8); // i could do one-liners but it looks like cancer
    tableStacks = *( uintptr_t * )(tableStacks + 0xa8); //replacing litterals with macro names? lol what for? :)

    cardCount = 0;
    uint stackOffset = 0;

    // card coordinates are not affected by scale
    // since they always stay the same might as well do that
    uint x = 10;
    uint startY = 130;
    uint pixelOffsetX = 85;
    uint pixelOffsetY = 15;

    for ( int i = 1; i < 8; i += 1 ) {

        printf("\n\nSTACK #%d\n", i);
        uintptr_t cardArray = *( uintptr_t * ) (tableStacks + stackOffset);
        //in case it was launched mid game use actual stack card counters
        uint * cardAmnt = (uint *) (cardArray + 0x130);
        //cards in the stack
        cardArray = *( uintptr_t * ) (cardArray + 0x140);

        uint y = startY;
        uint cardOffset = 0;
        for ( int j = 0; j < *cardAmnt; j+=1 ) {

            uintptr_t card = *( uintptr_t * )(cardArray + cardOffset);

            //debug info:
            uintptr_t cardNameStr = *( uintptr_t * ) (card + 0x38);
            printf("card: [ %ls ]\n", cardNameStr);

            CARDS[cardCount].value = *( uint * ) (card + sizeof(uintptr_t));
            CARDS[cardCount].x = x;
            CARDS[cardCount].y = y;
                    
            int col = CARDS[cardCount].value / 13;
            CARDS[cardCount].isRed = ( col == 0 || col == 2 ) ? 0 : 1;
                    
            cardOffset += sizeof(uintptr_t);
            y += pixelOffsetY;
            cardCount += 1;
        }
		
        stackOffset += sizeof(uintptr_t);
        x += pixelOffsetX;
	}
}

/*
								HOOK STUFF
*/
static uintptr_t setTheHookUp(LPDIRECT3DDEVICE9 pDevice)
{
    //prevent BeginStateBlock from cucking my callback
    uintptr_t backupAddr = *( uintptr_t * )pDevice - sizeof(uintptr_t);
    //GetModuleHandleA("d3d9.dll") + 0x186470
    backupAddr = *( uintptr_t * )backupAddr + 0x150; 

    DWORD dwOld = 0;

    uintptr_t fuq = (uintptr_t)endSceneWrapper;
    VirtualProtect((void*)backupAddr, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &dwOld);
    memcpy((void *)backupAddr, &fuq, sizeof(uintptr_t));
    VirtualProtect((void *)backupAddr, sizeof(uintptr_t), dwOld, &dwOld);

    //save the address of original EndScene
    ogEndScene = (HRESULT  (__stdcall * )(IDirect3DDevice9 *))pDevice->lpVtbl->EndScene;
    //provide a better endscene cause old one obviously sucks (:
    pDevice->lpVtbl->EndScene = endSceneWrapper;

    return backupAddr;
}

static void dropTheHook(LPDIRECT3DDEVICE9 pDevice, uintptr_t f)
{
    DWORD dwOld = 0;

    //TODO: find a proper cast for memccpy
    uintptr_t fuq = (uintptr_t)ogEndScene;
    VirtualProtect((void*)f, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &dwOld);
    memcpy((void *)f, &fuq, sizeof(uintptr_t));
    VirtualProtect((void *)f, sizeof(uintptr_t), dwOld, &dwOld);
    //restore the endScene
    pDevice->lpVtbl->EndScene = ogEndScene;
}

void whatever_it_is()
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);

    uintptr_t baseAddr = (uintptr_t)GetModuleHandleA(0);

    uintptr_t renderManagaer = *( uintptr_t * ) (baseAddr + 0xbb010);
    IDirect3DDevice9 * pDevice = *( IDirect3DDevice9 ** ) (renderManagaer + 0x50);


    getThemCards(baseAddr);

    uintptr_t addr = setTheHookUp(pDevice);
    // waiting for a user input to terminate
    system("pause");
    //restore EndScene addresses	
    dropTheHook(pDevice, addr); 

    FreeConsole(); 
    FreeLibraryAndExitThread(dllHandle, 0);
}

bool __stdcall DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved){
    if (fdwReason == DLL_PROCESS_ATTACH){
        dllHandle = hinstDLL;
        CloseHandle(CreateThread(NULL, (SIZE_T)NULL, (LPTHREAD_START_ROUTINE)whatever_it_is, NULL, 0, NULL));
    return true; 
    }

    return false;
} 

#define JUST_IN_CASE "IAMTOOYOUNGFORAJAILPLEASEMICROSOFTDONTSUEMEIDONTHAVEANYMONEYWHATSOEVER"
