#include "dx11.h"
#include "engine.h"
#include "render.h"
#include "menu.h"
#include "aimbot.h"
#include "hook.h"

typedef LRESULT(WINAPI* pWndProc)(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
typedef HRESULT(WINAPI* pPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef HRESULT(WINAPI* pResize)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);


ID3D11Device* g_pDevice = nullptr;//��Ϸ��D3Dָ��
ID3D11DeviceContext* g_pDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_RenderTargetView = nullptr;

pPresent g_Present = nullptr;//��Ϸ��ԭ������ַ
pWndProc g_WndProc = nullptr;
pResize g_Resize = nullptr;


DWORD64* SwapVTable = nullptr;//���������

HWND g_hWnd = 0;


void GetDx11Ptr(IDXGISwapChain* pSwapChain);//��������
HRESULT WINAPI Initialize(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);

HRESULT WINAPI hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
 
    if (Options.OpenMenu)
    {
        ImGui::Begin(u8"InSert��ʾ������");

        ImGui::SetWindowSize(ImVec2(310, 330), ImGuiCond_Once);

        if (ImGui::BeginTabBar("tab1"))
        {
            Player();
            Esp();
            Aimbot();

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    UWorld* World = GetWorld();
    if (World && World->GameState)
    {
        TArray<APlayerState*>& Actors = World->GameState->PlayerArray;

        for (size_t i = 0; i < Actors.Count; i++)
        {
            APlayerState* PlayerState = Actors.Data[i];
            if (!PlayerState || !PlayerState->PawnPrivate)
                continue;

            AActor* Actor = PlayerState->PawnPrivate;
            AActor* Player = World->OwningGameInstance->GetPlayer();
            if (!Player || Player->PlayerState->Team == PlayerState->Team)
                continue;

            if (!(Actor->HealthStatsComponent->bIsAlive))
                continue;


            bool IsVisible = Actor->IsVisible(Player, BoneFNames::Head);
            ImColor Color = IsVisible ? ImColor(255, 255, 0) : ImColor(255, 0, 0);

            if (!IsVisible || Options.bulletTrack)
            {
                Aimbot::GetInstance().Push(Player, Actor);
            }

            //��ʼ����Ⱦ����
            Render RenderObject(Player, Actor);

            //���ƹ���
            if (Options.DrawBone)
                RenderObject.DrawBone(Color);

            //����2D����Ӧ����
            if (Options.DrawBox2D)
                RenderObject.DrawBox2D(Color);

            if (Options.DrawBox3D)
                RenderObject.DrawBox3D(Color);

            if (Options.DrawRadar)
                RenderObject.DrawRadar(Color);

            if (Options.DrawSnapLine)
                RenderObject.DrawSnapLine(Color);

            if (Options.DrawLosLine)
                RenderObject.DrawLosLine(Color);

            if (Options.DrawBlood)
                RenderObject.DrawBlood(ImColor(0, 255, 0));

            if (Options.DrawRange)
                ImGui::GetForegroundDrawList()->AddCircle({ Width,Height }, Options.SilentRange, ImColor(255, 255, 255));

            AItem_Weapon_General* Weapon = Player->PlayerState->InventoryComponent->CurrentWeapon;
            if (Options.NoDiff)
            {
                Weapon->AccuracyHip = 99999.f;
                Weapon->AccuracySight = 99999.f;
                Weapon->SpreadShot = 0.f;
            }

            if (Options.MaxAmmo)
            {
                Weapon->MaxMagazineAmmo = 99999;
            }

            if (Options.NoRecoil)
            {
                Weapon->WeaponUpRecoil = 0.f;
                Weapon->WeaponRecoilLift = 0.f;
                Weapon->WeaponBackwardRecoil = 0.f;
                Weapon->WeaponRecoilAlphaPerShot = 0.f;
            }

            if (Options.FullAuto)
            {
                Weapon->WeaponShootingType = EWeaponShootingType::AUTO;
            }

            if (Options.MaxStamina)
            {
                Player->HealthStatsComponent->Stamina = 100.f;
            }

            if (Options.MuzzleVelocityMultiplier)
            {
                Weapon->MuzzleVelocityMultiplier = 10.f;
            }

            Weapon->Mobility = Options.Mobility;

        }

        Aimbot::GetInstance().Clear(); //�����һ������������׼������ĵ���

        if (Options.OpenAimbot && Options.MemoryAimbot && GetAsyncKeyState(VK_CAPITAL))
        {
            Aimbot::GetInstance().MemoryAimbot();
        }
    }
    

    ImGui::Render();
    g_pDeviceContext->OMSetRenderTargets(1, &g_RenderTargetView, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return g_Present(pSwapChain, SyncInterval, Flags);
}

void WriteVtb(int Idx, void* Address)
{
    DWORD Protect;
    VirtualProtect(SwapVTable, 1024, PAGE_EXECUTE_READWRITE, &Protect);
    SwapVTable[Idx] = (DWORD64)Address;
    VirtualProtect(SwapVTable, 1024, Protect, &Protect);
}

HRESULT WINAPI hkResize(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    if (g_pDevice != nullptr)//����,����ֹδ����ReImGui����֮ǰ�ٴ�ִ�е��µı���
    {
        g_pDevice->Release();
        g_pDevice = nullptr;
        g_RenderTargetView->Release();

        ImGui_ImplDX11_Shutdown();

        WriteVtb(8, Initialize);
    }

    return g_Resize(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

    switch (msg)
    {
    case WM_KEYUP:
        if (wParam == VK_INSERT)
            Options.OpenMenu = !Options.OpenMenu;
        break;
    }

    if (Options.OpenMenu && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    return CallWindowProcA(g_WndProc, hWnd, msg, wParam, lParam);
}

HRESULT WINAPI Initialize(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    static bool Is = false;

    if (!Is)
    {
        Is = true;
        g_WndProc = (WNDPROC)SetWindowLongPtrA(g_hWnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);//�ӹ���Ϣ����

        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;

        io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 17.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());

        ImGui_ImplWin32_Init(g_hWnd);

        EngineInit(g_hWnd);
        HookInitialize();
    }


    GetDx11Ptr(pSwapChain);
    ImGui_ImplDX11_Init(g_pDevice, g_pDeviceContext);

    WriteVtb(8, hkPresent);

    return g_Present(pSwapChain, SyncInterval, Flags);
}

void GetDx11Ptr(IDXGISwapChain* pSwapChain)
{
    g_pSwapChain = pSwapChain;//ȡ�ý�����ָ��
    g_pSwapChain->GetDevice(__uuidof(g_pDevice), (void**)&g_pDevice);//ȡ���豸ָ��
    g_pDevice->GetImmediateContext(&g_pDeviceContext);//ȡ��������ָ��

    ID3D11Texture2D* renderTarget = nullptr;
    g_pSwapChain->GetBuffer(0, __uuidof(renderTarget), (void**)&renderTarget);
    g_pDevice->CreateRenderTargetView(renderTarget, nullptr, &g_RenderTargetView);
    renderTarget->Release();//����Ϊ��ȡ��Ҫ�ĸ�ָ��
}

bool DX11Hook()
{
    g_hWnd = FindWindowA("UnrealWindow", NULL);

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, NULL, &featureLevel, NULL) != S_OK)
        return false;

    SwapVTable = (*(DWORD64**)g_pSwapChain);

    DWORD Protect;
    VirtualProtect(SwapVTable, 1024, PAGE_EXECUTE_READWRITE, &Protect);

    g_Present = (pPresent)SwapVTable[8];//ȡ����ϷPresent��ַ
    WriteVtb(8, Initialize);
    g_Resize = (pResize)SwapVTable[13];//ȡ����ϷResizeBuffers��ַ
    WriteVtb(13, hkResize);

    g_pSwapChain->Release();

    return true;
}